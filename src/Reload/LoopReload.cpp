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
#include "Hooks/ClipGeneratorHook.h"
#include "Settings/Settings.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto StaleAfter = std::chrono::seconds(5);
    constexpr std::uint32_t MinimumShellsForLoop = 2;
    constexpr float NotPending = -1.0f;

    struct LoopState
    {
        RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
        std::uint16_t                    RealCapacity{ 0 };
        std::uint32_t                    LoadedAtStart{ 0 };
        std::uint32_t                    ShellsToAdd{ 0 };
        std::uint32_t                    Inserted{ 0 };
        Clock::time_point                LastShell{};
        float                            Delay{ 0.0f };
        float                            PendingFor{ NotPending };

        [[nodiscard]] bool Pending() const
        {
            return PendingFor >= 0.0f;
        }

        [[nodiscard]] bool Active() const
        {
            return Instance != nullptr;
        }
    };

    std::mutex        Lock;
    LoopState         State;
    std::atomic<bool> StopRequested{ false };
    std::atomic<bool> Pending{ false };
    std::atomic<bool> TickAvailable{ false };

    std::vector<RE::BSFixedString> LoopEvents;

    constexpr std::array InterruptTags{
        "reloadStateExit"sv,
        "pipboyOpened"sv,
        "weaponSwing"sv,
        "throwEnd"sv,
        "weaponDraw"sv,
        "impactLandEnd"sv,
    };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] bool TryBegin()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const auto Weapon = Core::GetEquippedWeapon(Player);

        if (!Weapon)
            return false;

        const auto Rule = WeaponConfig::MatchLoop(Weapon->Base, Weapon->Instance);

        if (!Rule)
            return false;

        const auto Ammo = Weapon->Instance->ammo ? Weapon->Instance->ammo : Weapon->Data->ammo;

        if (!Ammo)
            return false;

        std::uint32_t OwnedIncludingLoaded = 0;
        Player->GetItemCount(OwnedIncludingLoaded, Ammo, false);

        const std::uint32_t Loaded = Weapon->Data->ammoCount;
        const std::uint32_t Capacity = Weapon->Instance->ammoCapacity;
        const std::uint32_t Room = Capacity > Loaded ? Capacity - Loaded : 0;
        const std::uint32_t Reserve = OwnedIncludingLoaded > Loaded ? OwnedIncludingLoaded - Loaded : 0;
        const std::uint32_t Shells = (std::min)(Room, Reserve);

        if (Shells < MinimumShellsForLoop)
        {
            if (Settings::VerboseLog())
                REX::INFO("Reload of {} shell(s): no loop needed", Shells);

            return false;
        }

        State = LoopState{
            .Instance = Weapon->Instance,
            .RealCapacity = Weapon->Instance->ammoCapacity,
            .LoadedAtStart = Loaded,
            .ShellsToAdd = Shells,
            .Delay = Rule->Delay.value_or(0.0f),
        };
        StopRequested = false;

        REX::INFO("Loop reload started: {}/{} loaded, {} shell(s) to insert, loop delay {:.3f}s", Loaded, Capacity, Shells, State.Delay);

        return true;
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

        State.Instance->ammoCapacity = State.RealCapacity;
        REX::INFO("Loop reload ended ({}): {}/{} shell(s) inserted", Reason, State.Inserted, State.ShellsToAdd);

        State = {};
        StopRequested = false;
        Pending = false;
    }

    void SendLoopEventsOutsideLock()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
            return;

        bool Accepted = false;

        for (const auto& Event : LoopEvents)
            Accepted |= Player->NotifyAnimationGraphImpl(Event);

        if (!Accepted)
            REX::WARN("The animation graph accepted none of the loop events; the reload ends after this pass");
    }

    [[nodiscard]] Reload::Action OnReloadComplete()
    {
        const auto Now = Clock::now();

        if (State.Active())
        {
            const auto Weapon = Core::GetEquippedWeapon(RE::PlayerCharacter::GetSingleton());

            if (!Weapon || Weapon->Instance != State.Instance)
                Finish("weapon changed");
            else if (Now - State.LastShell > StaleAfter)
                Finish("stale");
        }

        if (!State.Active() && !TryBegin())
            return Reload::Action::None;

        State.LastShell = Now;

        if (State.Inserted < State.ShellsToAdd)
            ++State.Inserted;

        State.Instance->ammoCapacity = static_cast<std::uint16_t>(State.LoadedAtStart + State.Inserted);

        const bool More = State.Inserted < State.ShellsToAdd;

        if (Settings::VerboseLog())
        {
            REX::INFO("Shell {}/{}{}", State.Inserted, State.ShellsToAdd,
                More ? (StopRequested ? " (stop requested)" : "") : " (last)");
        }

        if (!More)
            return Reload::Action::None;

        if (StopRequested)
        {
            if (Settings::VerboseLog())
                REX::INFO("Stop requested: next pass not asked for");

            return Reload::Action::None;
        }

        if (const auto Remaining = Hooks::RemainingReloadCompletes(); Remaining && *Remaining > 0)
            return Reload::Action::None;

        return Reload::Action::RequestNextPass;
    }
}

namespace Reload
{
    void Initialize()
    {
        LoopEvents.clear();
        LoopEvents.emplace_back("MWB_ReloadCroppedLoop");
    }

    Action BeforeGameHandles(const RE::BSFixedString& Tag, const void* Source)
    {
        if (Tag.empty())
            return Action::None;

        const std::scoped_lock Guard{ Lock };

        const bool IsReloadComplete = TagIs(Tag, "reloadComplete"sv);
        const bool IsReloadEnd = TagIs(Tag, "reloadEnd"sv);
        const bool IsInterrupt = std::ranges::any_of(InterruptTags, [&](std::string_view Name) { return TagIs(Tag, Name); });

        if (Settings::VerboseLog() && (IsReloadComplete || ((IsReloadEnd || IsInterrupt) && State.Active())))
            REX::INFO("Event '{}' from source {:X}", Tag.c_str(), reinterpret_cast<std::uintptr_t>(Source));

        if (IsReloadComplete)
            return OnReloadComplete();

        if (IsReloadEnd || IsInterrupt)
            Finish(Tag.c_str());

        return Action::None;
    }

    void AfterGameHandles(Action Requested)
    {
        if (Requested != Action::RequestNextPass)
            return;

        {
            const std::scoped_lock Guard{ Lock };

            if (!State.Active())
                return;

            if (State.Delay > 0.0f && TickAvailable)
            {
                State.PendingFor = State.Delay;
                Pending = true;

                return;
            }
        }

        SendLoopEventsOutsideLock();
    }

    void Tick(float Delta)
    {
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

    void SetTickAvailable(bool Available)
    {
        TickAvailable = Available;
    }

    void RequestStop()
    {
        const std::scoped_lock Guard{ Lock };

        if (State.Active() && State.Inserted > 0 && State.Inserted < State.ShellsToAdd)
        {
            if (!StopRequested.exchange(true))
                REX::INFO("Stop requested at shell {}/{}", State.Inserted, State.ShellsToAdd);
        }
    }

    void Abort(std::string_view Reason)
    {
        const std::scoped_lock Guard{ Lock };
        Finish(Reason);
    }
}
