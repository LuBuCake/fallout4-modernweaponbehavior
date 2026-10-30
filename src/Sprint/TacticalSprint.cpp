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

    std::atomic<bool> Running{ false };
    std::atomic<bool> OwnLoop{ false };
    RE::BSFixedString PendingEvent;
    float             PendingFor{ 0.0f };

    [[nodiscard]] bool InFirstPerson(RE::PlayerCharacter* Player)
    {
        return !Player->Is3rdPersonVisible();
    }

    [[nodiscard]] bool Sprinting(const RE::PlayerCharacter* Player)
    {
        return (static_cast<std::uint16_t>(Player->moveMode) & SprintingBit) != 0;
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

        if (!Player || !Player->sprintToggled || !Sprinting(Player) || !InFirstPerson(Player) || Player->IsJumping())
            return false;

        Running = true;
        PendingFor = 0.0f;
        PendingEvent = {};

        const auto Rule = Sprint::CurrentRule();
        OwnLoop = Rule && Rule->Tactical;

        if (Rule)
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

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
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

        if (!InFirstPerson(Player))
        {
            End("third person");
            return;
        }

        if (Player->IsJumping())
        {
            End("jump, back to the normal sprint");
            return;
        }

        if (PendingEvent.empty())
            return;

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
        PendingEvent = {};
        PendingFor = 0.0f;
    }
}
