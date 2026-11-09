/*
    This file is part of Modern Weapon Behavior.
    Modern Weapon Behavior is free software: you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation, either version 3 of
    the License, or (at your option) any later version.
    Modern Weapon Behavior is distributed in the hope that it will
    be useful, but WITHOUT ANY WARRANTY; without even the implied
    warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Modern Weapon Behavior. If not, see <https://www.gnu.org/licenses/>.
*/

#include "LoopReload.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Core/GunState.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Reload/SightedReload.h"
#include "Settings/Settings.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto StaleAfter = std::chrono::seconds(5);
    constexpr std::uint32_t MinimumShells = 1;
    constexpr float NotPending = -1.0f;
    constexpr auto TraceAfter = std::chrono::milliseconds(1500);
    constexpr std::size_t ReloadWeaponSlot = 0xEF;
    constexpr std::size_t MaxActorLoops = 32;
    constexpr std::uint64_t TicksBeforeDecision = 2;

    constexpr std::string_view ReloadTag = "MWB_Reload"sv;
    constexpr std::string_view InsertShellPayload = "InsertShell"sv;
    constexpr std::string_view CallLoopPayload = "CallLoop"sv;

    enum class Mark
    {
        None,
        InsertShell,
        CallLoop,
    };

    enum class Begin
    {
        Started,
        NothingToInsert,
        NoWeapon,
    };

    struct LoopState
    {
        RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
        std::uint32_t                    LoadedAtStart{ 0 };
        std::uint32_t                    ShellsToAdd{ 0 };
        std::uint32_t                    Inserted{ 0 };
        Clock::time_point                LastShell{};
        float                            Delay{ 0.0f };
        float                            PendingFor{ NotPending };
        bool                             Looped{ false };

        [[nodiscard]] bool Pending() const
        {
            return PendingFor >= 0.0f;
        }

        [[nodiscard]] bool Active() const
        {
            return Instance != nullptr;
        }
    };

    struct ActorLoop
    {
        const RE::Actor*             Actor{ nullptr };
        std::uint32_t                FormID{ 0 };
        RE::ObjectRefHandle          Handle;
        LoopState                    State;
        std::optional<std::uint32_t> Ahead;
        std::uint64_t                DecideAt{ 0 };
        bool                         CallPending{ false };
    };

    std::mutex        Lock;
    LoopState         State;
    std::atomic<bool> StopRequested{ false };
    std::atomic<bool> Pending{ false };
    std::atomic<bool> CallPending{ false };
    std::atomic<bool> TickAvailable{ false };

    std::vector<ActorLoop>     ActorLoops;
    std::atomic<std::size_t>   ActorLoopCount{ 0 };
    std::atomic<std::uint64_t> Ticks{ 0 };

    std::atomic<std::int64_t> TraceUntil{ 0 };
    bool                      TracedAimHeld{ false };

    std::vector<RE::BSFixedString> LoopEvents;

    constexpr std::array FireTags{
        "attackStart"sv,
        "attackStartAuto"sv,
        "attackRelease"sv,
        "attackReleaseCharge"sv,
    };

    constexpr std::array InterruptTags{
        "reloadStateExit"sv,
        "pipboyOpened"sv,
        "weaponSwing"sv,
        "throwEnd"sv,
        "weaponDraw"sv,
        "impactLandEnd"sv,
    };

    [[nodiscard]] bool SameText(std::string_view Text, std::string_view Name)
    {
        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] std::string_view TextOf(const RE::BSFixedString& Text)
    {
        return Text.c_str() ? std::string_view{ Text.c_str() } : std::string_view{};
    }

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        return SameText(TextOf(Tag), Name);
    }

    [[nodiscard]] bool EndsReload(const RE::BSFixedString& Tag)
    {
        return TagIs(Tag, "reloadEnd"sv) || std::ranges::any_of(InterruptTags, [&](std::string_view Name) { return TagIs(Tag, Name); });
    }

    [[nodiscard]] Mark MarkOf(const RE::BSAnimationGraphEvent& Event)
    {
        const auto Tag = TextOf(Event.tag);

        if (Tag.size() < ReloadTag.size() || !SameText(Tag.substr(0, ReloadTag.size()), ReloadTag))
            return Mark::None;

        std::string_view Payload;

        if (Tag.size() == ReloadTag.size())
            Payload = TextOf(Event.payload);
        else if (Tag[ReloadTag.size()] == '.')
            Payload = Tag.substr(ReloadTag.size() + 1);
        else
            return Mark::None;

        if (SameText(Payload, InsertShellPayload))
            return Mark::InsertShell;

        if (SameText(Payload, CallLoopPayload))
            return Mark::CallLoop;

        REX::WARN("MWB_Reload with an unknown payload '{}' (InsertShell or CallLoop)", Payload);

        return Mark::None;
    }

    [[nodiscard]] bool Tracing()
    {
        return Settings::VerboseLog() && Clock::now().time_since_epoch().count() < TraceUntil.load();
    }

    [[nodiscard]] int TracedGunState()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        return Player ? static_cast<int>(Core::GetGunState(Player)) : -1;
    }

    [[nodiscard]] bool IsPlayer(const RE::Actor* Actor)
    {
        return Actor && Actor == RE::PlayerCharacter::GetSingleton();
    }

    [[nodiscard]] Begin TryBegin(RE::Actor* Actor, LoopState& Into)
    {
        const auto Weapon = Actor ? Core::GetEquippedWeapon(Actor) : std::nullopt;

        if (!Weapon || !Weapon->Instance || !Weapon->Data)
            return Begin::NoWeapon;

        const auto Rule = WeaponConfig::MatchLoop(Weapon->Base, Weapon->Instance);
        const bool Player = IsPlayer(Actor);

        const std::uint32_t Loaded = Weapon->Data->ammoCount;
        const std::uint32_t Capacity = Weapon->Instance->ammoCapacity;
        const std::uint32_t Room = Capacity > Loaded ? Capacity - Loaded : 0;
        std::uint32_t       Shells = Room;

        if (Player)
        {
            const auto Ammo = Weapon->Instance->ammo ? Weapon->Instance->ammo : Weapon->Data->ammo;

            if (!Ammo)
                return Begin::NoWeapon;

            std::uint32_t OwnedIncludingLoaded = 0;
            Actor->GetItemCount(OwnedIncludingLoaded, Ammo, false);

            const std::uint32_t Reserve = OwnedIncludingLoaded > Loaded ? OwnedIncludingLoaded - Loaded : 0;
            Shells = (std::min)(Room, Reserve);
        }

        if (Shells < MinimumShells)
        {
            if (Settings::VerboseLog())
                REX::INFO("MWB_Reload: {:08X} has no shell to insert", Actor->GetFormID());

            return Begin::NothingToInsert;
        }

        Into = LoopState{
            .Instance = Weapon->Instance,
            .LoadedAtStart = Loaded,
            .ShellsToAdd = Shells,
            .Delay = Rule ? Rule->Delay.value_or(0.0f) : 0.0f,
            .Looped = static_cast<bool>(Rule),
        };

        if (Player)
        {
            StopRequested = false;
            REX::INFO("{} reload started: {}/{} loaded, {} shell(s) to insert, loop delay {:.3f}s", Into.Looped ? "Loop" : "Shell", Loaded, Capacity,
                Shells, Into.Delay);
            TraceUntil = (std::numeric_limits<std::int64_t>::max)();
        }
        else if (Settings::VerboseLog())
        {
            REX::INFO("MWB_Reload: {:08X} {} reload started: {}/{} loaded, {} shell(s) to insert", Actor->GetFormID(), Into.Looped ? "loop" : "shell",
                Loaded, Capacity, Shells);
        }

        return Begin::Started;
    }

    void Finish(std::string_view Reason)
    {
        if (!State.Active())
            return;

        if (State.Pending())
        {
            REX::WARN("Loop reload ended ({}) while the next pass was still waiting its delay: "
                      "the delay is longer than the loop clip allows", Reason);
        }

        REX::INFO("{} reload ended ({}): {}/{} shell(s) inserted", State.Looped ? "Loop" : "Shell", Reason, State.Inserted, State.ShellsToAdd);
        TraceUntil = (Clock::now() + TraceAfter).time_since_epoch().count();

        State = {};
        StopRequested = false;
        Pending = false;
        CallPending = false;
    }

    void SendLoopEvents(RE::Actor* Actor)
    {
        if (!Actor)
            return;

        bool Accepted = false;

        for (const auto& Event : LoopEvents)
            Accepted |= Actor->NotifyAnimationGraphImpl(Event);

        if (!Accepted)
            REX::WARN("The animation graph of {:08X} accepted none of the loop events; the reload ends after this pass", Actor->GetFormID());
    }

    void SendLoopEventsOutsideLock()
    {
        SendLoopEvents(RE::PlayerCharacter::GetSingleton());
    }

    void ReloadActor(RE::Actor* Actor, std::optional<std::uint32_t> Count)
    {
        const auto Weapon = Actor ? Core::GetEquippedWeapon(Actor) : std::nullopt;

        if (!Weapon || !Weapon->Item || !Weapon->Instance)
            return;

        const auto Capacity = Weapon->Instance->ammoCapacity;

        if (Count)
            Weapon->Instance->ammoCapacity = static_cast<std::uint16_t>((std::min)(*Count, static_cast<std::uint32_t>(Capacity)));

        using ReloadWeapon = bool (*)(RE::Actor*, const RE::BGSObjectInstance&, std::uint32_t);

        const auto VirtualTable = *reinterpret_cast<const ReloadWeapon* const*>(Actor);
        VirtualTable[ReloadWeaponSlot](Actor, Weapon->Item->item, Weapon->Item->equipIndex.index);
        Weapon->Instance->ammoCapacity = Capacity;

        if (Settings::VerboseLog())
            REX::INFO("MWB_Reload: {:08X} has {} loaded", Actor->GetFormID(), Weapon->Data ? Weapon->Data->ammoCount : 0u);
    }

    void ReloadOutsideLock(std::optional<std::uint32_t> Count)
    {
        ReloadActor(RE::PlayerCharacter::GetSingleton(), Count);
    }

    [[nodiscard]] bool Stale(RE::Actor* Actor, const LoopState& Current, Clock::time_point Now)
    {
        if (!Current.Active())
            return false;

        const auto Weapon = Core::GetEquippedWeapon(Actor);

        return !Weapon || Weapon->Instance != Current.Instance || Now - Current.LastShell > StaleAfter;
    }

    void CheckStale(Clock::time_point Now)
    {
        if (Stale(RE::PlayerCharacter::GetSingleton(), State, Now))
            Finish("weapon changed or stale");
    }

    [[nodiscard]] Reload::Action InsertShell(RE::Actor* Actor, LoopState& Current, Clock::time_point Now)
    {
        if (!Current.Active())
        {
            const auto Result = TryBegin(Actor, Current);

            if (Result == Begin::NoWeapon)
                return { .What = Reload::Step::FullReload };

            if (Result == Begin::NothingToInsert)
                return {};
        }

        Current.LastShell = Now;

        if (Current.Inserted >= Current.ShellsToAdd)
        {
            if (Settings::VerboseLog())
                REX::INFO("MWB_Reload.InsertShell: {:08X} has no shell missing", Actor->GetFormID());

            return {};
        }

        ++Current.Inserted;

        const bool Full = Current.Inserted >= Current.ShellsToAdd;

        if (Settings::VerboseLog())
            REX::INFO("Shell {}/{} for {:08X}{}", Current.Inserted, Current.ShellsToAdd, Actor->GetFormID(),
                Full ? " (last)" : IsPlayer(Actor) && StopRequested ? " (stop requested)" : "");

        return { .What = Reload::Step::InsertShell, .Count = Current.LoadedAtStart + Current.Inserted, .Full = Full };
    }

    [[nodiscard]] Reload::Action OnInsertShell()
    {
        const auto Now = Clock::now();
        CheckStale(Now);

        return InsertShell(RE::PlayerCharacter::GetSingleton(), State, Now);
    }

    [[nodiscard]] bool CallLoopApplies(RE::Actor* Actor, LoopState& Current, Clock::time_point Now)
    {
        if (!Current.Active() && TryBegin(Actor, Current) != Begin::Started)
            return false;

        Current.LastShell = Now;

        if (!Current.Looped && Settings::VerboseLog())
            REX::INFO("MWB_Reload.CallLoop: {:08X}'s weapon has no loopedReload, ignored", Actor->GetFormID());

        return Current.Looped;
    }

    [[nodiscard]] Reload::Action OnCallLoop()
    {
        const auto Now = Clock::now();
        CheckStale(Now);

        if (!CallLoopApplies(RE::PlayerCharacter::GetSingleton(), State, Now))
            return {};

        return { .What = Reload::Step::CallLoop };
    }

    [[nodiscard]] bool WantsNextPass(const LoopState& Current, std::uint32_t Ahead, std::uint32_t FormID)
    {
        if (Current.Inserted + Ahead < Current.ShellsToAdd)
            return true;

        if (Settings::VerboseLog())
            REX::INFO("MWB_Reload.CallLoop: {:08X} {}/{} inserted, {} more in this pass: no next pass", FormID, Current.Inserted, Current.ShellsToAdd,
                Ahead);

        return false;
    }

    [[nodiscard]] bool NextPassWanted()
    {
        if (!State.Active() || !State.Looped)
            return false;

        const auto Ahead = Hooks::InsertShellsAfterCallLoop(nullptr).value_or(0);

        if (!WantsNextPass(State, Ahead, RE::PlayerCharacter::GetSingleton()->GetFormID()))
            return false;

        if (StopRequested)
        {
            if (Settings::VerboseLog())
                REX::INFO("Stop requested: next pass not asked for");

            return false;
        }

        if (State.Delay > 0.0f && TickAvailable)
        {
            State.PendingFor = State.Delay;
            Pending = true;

            return false;
        }

        return true;
    }

    [[nodiscard]] ActorLoop& LoopOf(RE::Actor* Actor, const RE::ObjectRefHandle& Handle)
    {
        const auto Found = std::ranges::find(ActorLoops, static_cast<const RE::Actor*>(Actor), &ActorLoop::Actor);

        if (Found != ActorLoops.end())
            return *Found;

        if (ActorLoops.size() >= MaxActorLoops)
            ActorLoops.erase(ActorLoops.begin());

        ActorLoops.push_back({ .Actor = Actor, .FormID = Actor->GetFormID(), .Handle = Handle });
        ActorLoopCount = ActorLoops.size();

        return ActorLoops.back();
    }

    void EndActorLoop(RE::Actor* Actor, const RE::BSFixedString& Tag)
    {
        const std::scoped_lock Guard{ Lock };

        const auto Found = std::ranges::find(ActorLoops, static_cast<const RE::Actor*>(Actor), &ActorLoop::Actor);

        if (Found == ActorLoops.end())
            return;

        if (Settings::VerboseLog())
            REX::INFO("MWB_Reload: {:08X} {} reload ended ({}): {}/{} shell(s) inserted", Found->FormID, Found->State.Looped ? "loop" : "shell", TextOf(Tag),
                Found->State.Inserted, Found->State.ShellsToAdd);

        ActorLoops.erase(Found);
        ActorLoopCount = ActorLoops.size();
    }

    void ActorInsertShell(RE::Actor* Actor)
    {
        const auto Handle = Actor->GetHandle();
        const auto Now = Clock::now();

        Reload::Action Requested;

        {
            const std::scoped_lock Guard{ Lock };

            auto& Entry = LoopOf(Actor, Handle);

            if (Stale(Actor, Entry.State, Now))
                Entry.State = {};

            Requested = InsertShell(Actor, Entry.State, Now);
        }

        if (Requested.What == Reload::Step::FullReload)
        {
            ReloadActor(Actor, std::nullopt);
        }
        else if (Requested.What == Reload::Step::InsertShell)
        {
            ReloadActor(Actor, Requested.Count);

            if (Requested.Full)
                Hooks::SkipToLastInsertShell(Actor);
        }
    }

    void ActorCallLoop(RE::Actor* Actor)
    {
        const auto Handle = Actor->GetHandle();
        const auto Now = Clock::now();
        const auto Ahead = Hooks::InsertShellsAfterCallLoop(Actor);

        const std::scoped_lock Guard{ Lock };

        auto& Entry = LoopOf(Actor, Handle);

        if (Stale(Actor, Entry.State, Now))
            Entry.State = {};

        if (!CallLoopApplies(Actor, Entry.State, Now))
            return;

        Entry.Ahead = Ahead;
        Entry.CallPending = true;
        Entry.DecideAt = Ticks.load(std::memory_order_relaxed) + TicksBeforeDecision;
    }

    void TickActorLoops(float Delta)
    {
        const auto Tick = Ticks.fetch_add(1, std::memory_order_relaxed) + 1;

        if (ActorLoopCount.load(std::memory_order_relaxed) == 0)
            return;

        std::vector<RE::ObjectRefHandle> Send;

        {
            const std::scoped_lock Guard{ Lock };

            const auto Now = Clock::now();

            for (auto& Entry : ActorLoops)
            {
                if (Entry.CallPending && Tick >= Entry.DecideAt)
                {
                    Entry.CallPending = false;

                    if (WantsNextPass(Entry.State, Entry.Ahead.value_or(0), Entry.FormID))
                    {
                        if (Entry.State.Delay > 0.0f)
                            Entry.State.PendingFor = Entry.State.Delay;
                        else
                            Send.push_back(Entry.Handle);
                    }
                }
                else if (Entry.State.Pending())
                {
                    Entry.State.PendingFor -= Delta;

                    if (Entry.State.PendingFor <= 0.0f)
                    {
                        Entry.State.PendingFor = NotPending;
                        Send.push_back(Entry.Handle);
                    }
                }
            }

            std::erase_if(ActorLoops, [&](const ActorLoop& Entry)
                { return !Entry.CallPending && !Entry.State.Pending() && Now - Entry.State.LastShell > StaleAfter; });
            ActorLoopCount = ActorLoops.size();
        }

        for (const auto& Handle : Send)
        {
            const auto Reference = Handle.get();

            if (const auto Actor = Reference ? Reference->As<RE::Actor>() : nullptr)
                SendLoopEvents(Actor);
        }
    }
}

namespace Reload
{
    void Initialize()
    {
        LoopEvents.clear();
        LoopEvents.emplace_back("MWB_ReloadCroppedLoop");
    }

    Action BeforeGameHandles(const RE::BSAnimationGraphEvent& Event, const void* Source)
    {
        const auto& Tag = Event.tag;

        if (Tag.empty())
            return {};

        const std::scoped_lock Guard{ Lock };

        const auto Kind = MarkOf(Event);
        const bool IsEnd = Kind == Mark::None && EndsReload(Tag);

        if (Settings::VerboseLog() && (Kind != Mark::None || (IsEnd && State.Active())))
            REX::INFO("Event '{}' ('{}') from source {:X}", TextOf(Tag), TextOf(Event.payload), reinterpret_cast<std::uintptr_t>(Source));

        if (Kind == Mark::InsertShell)
            return OnInsertShell();

        if (Kind == Mark::CallLoop)
            return OnCallLoop();

        if (IsEnd)
            Finish(Tag.c_str());

        return {};
    }

    void AfterGameHandles(const Action& Requested)
    {
        switch (Requested.What)
        {
        case Step::None:
            return;
        case Step::FullReload:
            ReloadOutsideLock(std::nullopt);
            return;
        case Step::InsertShell:
            ReloadOutsideLock(Requested.Count);

            if (Requested.Full)
                Hooks::SkipToLastInsertShell(nullptr);

            return;
        case Step::CallLoop:
            break;
        }

        if (TickAvailable)
        {
            CallPending = true;
            return;
        }

        bool Send = false;

        {
            const std::scoped_lock Guard{ Lock };
            Send = NextPassWanted();
        }

        if (Send)
            SendLoopEventsOutsideLock();
    }

    void Tick(float Delta)
    {
        if (Tracing())
        {
            const bool Held = SightedReload::AimButtonHeld();

            if (Held != std::exchange(TracedAimHeld, Held))
                REX::INFO("Looped reload trace: aim button {} (gunState {})", Held ? "down" : "up", TracedGunState());
        }

        TickActorLoops(Delta);

        if (CallPending.exchange(false))
        {
            bool Send = false;

            {
                const std::scoped_lock Guard{ Lock };
                Send = NextPassWanted();
            }

            if (Send)
                SendLoopEventsOutsideLock();
        }

        if (!Pending.load(std::memory_order_relaxed))
            return;

        {
            const std::scoped_lock Guard{ Lock };

            if (!State.Pending())
                return;

            State.PendingFor -= Delta;

            if (State.PendingFor > 0.0f)
                return;

            State.PendingFor = NotPending;
            Pending = false;

            if (StopRequested)
            {
                if (Settings::VerboseLog())
                    REX::INFO("Stop requested during the loop delay: next pass not asked for");

                return;
            }
        }

        SendLoopEventsOutsideLock();
    }

    void OnActorEvent(RE::Actor* Actor, const RE::BSAnimationGraphEvent& Event)
    {
        if (!Actor || IsPlayer(Actor) || Event.tag.empty())
            return;

        const auto Kind = MarkOf(Event);

        if (Kind == Mark::InsertShell)
            ActorInsertShell(Actor);
        else if (Kind == Mark::CallLoop)
            ActorCallLoop(Actor);
        else if (ActorLoopCount.load(std::memory_order_relaxed) > 0 && EndsReload(Event.tag))
            EndActorLoop(Actor, Event.tag);
    }

    void SetTickAvailable(bool Available)
    {
        TickAvailable = Available;
    }

    void RequestStop()
    {
        const std::scoped_lock Guard{ Lock };

        if (State.Active() && State.Looped && State.Inserted > 0 && State.Inserted < State.ShellsToAdd)
        {
            if (!StopRequested.exchange(true))
                REX::INFO("Stop requested at shell {}/{}", State.Inserted, State.ShellsToAdd);
        }
    }

    void Abort(std::string_view Reason)
    {
        const std::scoped_lock Guard{ Lock };
        Finish(Reason);
        ActorLoops.clear();
        ActorLoopCount = 0;
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (Event.empty() || std::ranges::none_of(FireTags, [&](std::string_view Name) { return TagIs(Event, Name); }))
            return false;

        {
            const std::scoped_lock Guard{ Lock };

            if (!State.Active() || !State.Looped)
                return false;
        }

        TraceSent(Event, false, "the loop (refused by MWB)"sv);

        return true;
    }

    void TraceEvent(const RE::BSAnimationGraphEvent& Event)
    {
        if (!Tracing())
            return;

        const std::string_view Payload{ Event.payload.c_str() ? Event.payload.c_str() : "" };

        REX::INFO("Looped reload trace: graph event {}{}{}{} (gunState {})", Event.tag.c_str(), Payload.empty() ? "" : "(", Payload,
            Payload.empty() ? "" : ")", TracedGunState());
    }

    void TraceSent(const RE::BSFixedString& Event, bool Accepted, std::string_view Path)
    {
        if (!Tracing())
            return;

        REX::INFO("Looped reload trace: {} sent by {}, {} (gunState {})", Event.c_str(), Path, Accepted ? "accepted" : "refused",
            TracedGunState());
    }
}
