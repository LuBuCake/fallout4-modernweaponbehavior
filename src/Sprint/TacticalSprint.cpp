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

#include "TacticalSprint.h"
#include "Settings/Settings.h"
#include "Sprint/SprintTransitions.h"

namespace
{
    constexpr float PlayTimeout = 10.0f;

    constexpr std::uint16_t SprintingBit = 0x100;

    constexpr std::size_t ButtonEventSlot = 0x08;

    constexpr REL::ID ActorUpdateID{ 534717, 2229751 };
    constexpr REL::ID UpdateSprintingID{ 385539, 2230498 };

    constexpr std::size_t  ActorUpdateScanSize = 0x400;
    constexpr std::uint8_t CallOpcode = 0xE8;
    constexpr std::size_t  CallInstructionSize = 5;

    constexpr std::size_t TranslationRows = 4;
    constexpr std::size_t Columns = 4;

    constexpr std::array GunSprintClips{ "WPNSprint"sv };
    constexpr std::array OtherSprintClips{ "MTSprint"sv, "MTSprint_NoAnnotations"sv, "SprintForward"sv, "SprintForward_NoAnnotations"sv };
    constexpr std::array ThirdPersonSprintNodes{ "WPNSprint"sv, "WPNSprint_Jump"sv, "SprintForward"sv, "SprintForward00"sv, "SprintLeanLeft"sv, "SprintLeanLeft00"sv,
        "SprintLeanRight"sv, "SprintLeanRight00"sv, "Camera_SprintForward"sv };
    constexpr std::array ThirdPersonSprintAnimations{ "WPNSprint"sv, "SprintForward"sv, "SprintLeanLeft"sv, "SprintLeanRight"sv };

    constexpr float       DiagnosticDelay = 1.0f;
    constexpr float       SpeedSampleTime = 1.0f;
    constexpr std::size_t MaxDiagnosticClips = 24;

    std::atomic<bool> Running{ false };
    std::atomic<bool> OwnLoop{ false };
    std::atomic<bool> InThirdPerson{ false };
    RE::BSFixedString PendingEvent;
    float             PendingFor{ 0.0f };

    std::mutex               DiagnosticLock;
    std::vector<std::string> SeenClips;
    bool                     ScaledSeen{ false };
    bool                     UnscaledReported{ false };
    float                    ThirdPersonFor{ 0.0f };

    struct SpeedSample
    {
        RE::NiPoint3 Last;
        bool         Valid{ false };
        bool         ThirdPerson{ false };
        bool         Tactical{ false };
        float        Distance{ 0.0f };
        float        Time{ 0.0f };
    };

    SpeedSample Sample;

    [[nodiscard]] bool SameText(std::string_view Left, std::string_view Right)
    {
        return Left.size() == Right.size() && _strnicmp(Left.data(), Right.data(), Left.size()) == 0;
    }

    [[nodiscard]] std::string_view Stem(std::string_view File)
    {
        const auto Dot = File.find_last_of('.');

        return Dot == std::string_view::npos ? File : File.substr(0, Dot);
    }

    void ResetDiagnostics()
    {
        const std::scoped_lock Guard{ DiagnosticLock };
        SeenClips.clear();
        ScaledSeen = false;
        UnscaledReported = false;
        ThirdPersonFor = 0.0f;
    }

    void ReportUnscaled(float Delta)
    {
        if (!Settings::VerboseLog() || !InThirdPerson)
            return;

        const std::scoped_lock Guard{ DiagnosticLock };

        if (ScaledSeen || UnscaledReported)
            return;

        ThirdPersonFor += Delta;

        if (ThirdPersonFor < DiagnosticDelay)
            return;

        UnscaledReported = true;

        std::string List;

        for (const auto& Clip : SeenClips)
            List += (List.empty() ? "" : ", ") + Clip;

        REX::INFO("Tactical sprint (third person): no sprint clip scaled after {:.1f}s; player clips updated: {}", ThirdPersonFor,
            List.empty() ? "none"s : List);
    }

    [[nodiscard]] bool InFirstPerson(RE::PlayerCharacter* Player)
    {
        return !Player->Is3rdPersonVisible();
    }

    [[nodiscard]] bool Sprinting(const RE::PlayerCharacter* Player)
    {
        return (static_cast<std::uint16_t>(Player->moveMode) & SprintingBit) != 0;
    }

    void MeasureSpeed(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Settings::VerboseLog())
            return;

        const auto Position = Player->GetPosition();
        const bool ThirdPerson = !InFirstPerson(Player);
        const bool Tactical = Running;

        if (!Sprinting(Player) || Delta <= 0.0f || !Sample.Valid || Sample.ThirdPerson != ThirdPerson || Sample.Tactical != Tactical)
        {
            Sample = { Position, Sprinting(Player), ThirdPerson, Tactical, 0.0f, 0.0f };
            return;
        }

        Sample.Distance += std::hypot(Position.x - Sample.Last.x, Position.y - Sample.Last.y);
        Sample.Time += Delta;
        Sample.Last = Position;

        if (Sample.Time < SpeedSampleTime)
            return;

        REX::INFO("Sprint speed: {:.0f} units/s ({} person{})", Sample.Distance / Sample.Time, ThirdPerson ? "third" : "first", Tactical ? ", tactical" : "");
        Sample.Distance = 0.0f;
        Sample.Time = 0.0f;
    }

    void End(std::string_view Why)
    {
        if (!Running.exchange(false))
            return;

        PendingEvent = {};

        if (Settings::VerboseLog())
            REX::INFO("Tactical sprint: off, {}", Why);
    }

    bool SendPending(RE::PlayerCharacter* Player)
    {
        if (PendingEvent.empty())
            return true;

        const bool Accepted = Player->NotifyAnimationGraphImpl(PendingEvent);

        if (Accepted)
        {
            if (Settings::VerboseLog())
                REX::INFO("Tactical sprint: {} accepted", PendingEvent.c_str());

            PendingEvent = {};
        }

        return Accepted;
    }

    [[nodiscard]] bool TryStart()
    {
        if (!Settings::TacticalSprint() || Running)
            return false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !Player->sprintToggled || !Sprinting(Player))
            return false;

        Running = true;
        PendingFor = 0.0f;
        PendingEvent = {};
        InThirdPerson = !InFirstPerson(Player);
        ResetDiagnostics();

        const auto Rule = Sprint::CurrentRule();
        OwnLoop = Rule && Rule->Tactical;

        if (Rule && !InThirdPerson && !Player->IsJumping())
        {
            if (Rule->TacticalStart)
                PendingEvent = "MWB_SprintTacticalStartPlay"sv;
            else if (Rule->Tactical)
                PendingEvent = "MWB_SprintTacticalPlay"sv;
        }

        if (Settings::VerboseLog())
            REX::INFO("Tactical sprint: on{}", PendingEvent.empty() ? ", no clip of its own" : "");

        return true;
    }

    struct SprintButton
    {
        static void Thunk(RE::PlayerInputHandler* This, const RE::ButtonEvent* Event)
        {
            if (Event && Event->QJustPressed() && TryStart())
            {
                const_cast<RE::ButtonEvent*>(Event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }

            Original(This, Event);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct UpdateSprinting
    {
        static void Thunk(RE::Actor* This)
        {
            const auto Player = RE::PlayerCharacter::GetSingleton();
            const auto Values = RE::ActorValue::GetSingleton();

            if (!Running || !This || This != Player || !Values)
            {
                Original(This);
                return;
            }

            const std::array Drained{ Values->actionPoints, Values->powerArmorBattery };
            std::array<float, Drained.size()> Before{};

            for (std::size_t i = 0; i < Drained.size(); ++i)
                Before[i] = Drained[i] ? This->GetActorValue(*Drained[i]) : 0.0f;

            Original(This);

            for (std::size_t i = 0; i < Drained.size(); ++i)
            {
                if (!Drained[i])
                    continue;

                const float Drop = Before[i] - This->GetActorValue(*Drained[i]);

                if (Drop > 0.0f)
                    This->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *Drained[i], -Drop * (Settings::TacticalSprintActionPoints() - 1.0f));
            }
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCall(std::uintptr_t Begin, std::uintptr_t Target, std::size_t Size)
    {
        std::optional<std::uintptr_t> Found;

        for (std::size_t i = 0; i + CallInstructionSize <= Size; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != CallOpcode)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(At + 1);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + CallInstructionSize) + Displacement) != Target)
                continue;

            if (Found)
                return std::nullopt;

            Found = At;
        }

        return Found;
    }
}

namespace TacticalSprint
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::SprintHandler[0] };
        const auto Controls = RE::PlayerControls::GetSingleton();
        const auto Live = Controls && Controls->sprintHandler ? *reinterpret_cast<std::uintptr_t*>(Controls->sprintHandler) : 0;

        if (Live != Vtable.address())
        {
            REX::ERROR("Tactical sprint: sprint handler vtable mismatch (id {:X}, live {:X}). Tactical sprint disabled.", Vtable.address(), Live);
            return;
        }

        const auto Target = REL::Relocation<std::uintptr_t>{ UpdateSprintingID }.address();
        const auto Site = FindCall(REL::Relocation<std::uintptr_t>{ ActorUpdateID }.address(), Target, ActorUpdateScanSize);

        if (!Site)
        {
            REX::ERROR("Tactical sprint: the sprint update call in Actor::Update was not found (another mod may have it). Tactical sprint disabled.");
            return;
        }

        SprintButton::Original = Vtable.write_vfunc(ButtonEventSlot, SprintButton::Thunk);
        UpdateSprinting::Original = REL::GetTrampoline().write_call<5>(*Site, UpdateSprinting::Thunk);
        REX::INFO("Tactical sprint hooks installed (sprint update at {:X}, chaining to {:X})", *Site, UpdateSprinting::Original.address());
    }

    bool Active()
    {
        return Running;
    }

    void ScaleSpeeds(RE::Movement::MaxSpeeds& Speeds)
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Running || !Player || !Sprinting(Player))
            return;

        const float Scale = Settings::TacticalSprintSpeed();

        for (std::size_t Row = 0; Row < TranslationRows; ++Row)
        {
            for (std::size_t Column = 0; Column < Columns; ++Column)
                Speeds.speeds[Row][Column] *= Scale;
        }
    }

    float ClipTimeScale(std::string_view Clip)
    {
        if (!Running)
            return 1.0f;

        if (std::ranges::find(OtherSprintClips, Clip) != OtherSprintClips.end())
            return Settings::TacticalSprintAnimation();

        if (!OwnLoop && std::ranges::find(GunSprintClips, Clip) != GunSprintClips.end())
            return Settings::TacticalSprintAnimation();

        return 1.0f;
    }

    bool ThirdPerson()
    {
        return InThirdPerson;
    }

    float ThirdPersonPlaybackScale(std::string_view Node, std::string_view AnimationFile)
    {
        if (!Running || !InThirdPerson)
            return 1.0f;

        const auto File = Stem(AnimationFile);
        const bool ByNode = std::ranges::any_of(ThirdPersonSprintNodes, [&](std::string_view Name) { return SameText(Name, Node); });
        const bool ByFile = std::ranges::any_of(ThirdPersonSprintAnimations, [&](std::string_view Name) { return SameText(Name, File); });

        return ByNode || ByFile ? Settings::TacticalSprintSpeed() : 1.0f;
    }

    void NoteThirdPersonClip(std::string_view Node, std::string_view AnimationFile, bool Scaled, float BeforeUpdate, float AfterUpdate, float Written)
    {
        const std::scoped_lock Guard{ DiagnosticLock };

        if (Scaled)
        {
            if (ScaledSeen)
                return;

            ScaledSeen = true;
            REX::INFO("Tactical sprint (third person): {} ({}) time step x{:.2f}; playback speed {:.3f} into the update, {:.3f} after it, {:.3f} "
                      "written for the motion",
                Node, AnimationFile, Settings::TacticalSprintSpeed(), BeforeUpdate, AfterUpdate, Written);

            return;
        }

        if (ScaledSeen || SeenClips.size() >= MaxDiagnosticClips)
            return;

        auto Entry = std::format("{} ({})", Node, AnimationFile);

        if (std::ranges::find(SeenClips, Entry) == SeenClips.end())
            SeenClips.push_back(std::move(Entry));
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (Player)
            MeasureSpeed(Player, Delta);

        if (!Running || !Player)
            return;

        if (const auto Rule = Sprint::CurrentRule())
            OwnLoop = Rule->Tactical;
        else
            OwnLoop = false;

        if (!Settings::TacticalSprint())
        {
            End("option turned off");
            return;
        }

        if (!Player->sprintToggled)
        {
            End("the sprint was stopped");
            return;
        }

        InThirdPerson = !InFirstPerson(Player);
        ReportUnscaled(Delta);

        if (PendingEvent.empty())
            return;

        if (InThirdPerson || Player->IsJumping())
        {
            if (Settings::VerboseLog())
                REX::INFO("Tactical sprint: {} dropped ({})", PendingEvent.c_str(), InThirdPerson ? "third person" : "jump");

            PendingEvent = {};
            return;
        }

        PendingFor += Delta;

        if (!SendPending(Player) && PendingFor >= PlayTimeout)
        {
            if (Settings::VerboseLog())
                REX::INFO("Tactical sprint: {} never accepted, given up", PendingEvent.c_str());

            PendingEvent = {};
        }
    }

    void Reset()
    {
        Running = false;
        OwnLoop = false;
        InThirdPerson = false;
        PendingEvent = {};
        PendingFor = 0.0f;
        ResetDiagnostics();
    }
}
