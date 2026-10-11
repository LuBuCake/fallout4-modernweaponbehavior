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

#include "SprintTransitions.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::int32_t WPNSprintStartStateID = 2;
    constexpr std::int32_t EquipWalking = 0;
    constexpr std::int32_t EquipSprinting = 1;
    constexpr std::int32_t EquipSprintingClip = 2;

    constexpr float ReleaseAfterCombat = 0.3f;
    constexpr float HoldTimeout = 5.0f;
    constexpr float SprintSettled = 0.25f;

    struct Wanted
    {
        std::int32_t StartState{ 0 };
        std::int32_t Stop{ 0 };

        bool operator==(const Wanted&) const = default;
    };

    struct Hold
    {
        bool SkipStart{ false };
        bool StopPending{ false };
        float Age{ 0.0f };
        float ReleaseIn{ -1.0f };
    };

    std::mutex Lock;
    Hold CurrentHold;

    float SprintingFor{ 0.0f };

    RE::TESObjectWEAP* LastBase{ nullptr };
    RE::TESObjectWEAP::InstanceData* LastInstance{ nullptr };
    std::optional<WeaponConfig::SprintRule> LastRule;
    bool WarnedMissing{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] const std::optional<WeaponConfig::SprintRule>& RuleFor(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        const auto Base = Weapon ? Weapon->Base : nullptr;
        const auto Instance = Weapon ? Weapon->Instance : nullptr;

        if (Base != LastBase || Instance != LastInstance)
        {
            LastBase = Base;
            LastInstance = Instance;

            if (Settings::VerboseLog())
            {
                REX::INFO("Sprint transitions: weapon {:08X}{}; equipped items: {}", Base ? Base->GetFormID() : 0u,
                    Weapon && Weapon->Data ? " (drawn)" : "", Core::DescribeEquippedItems(RE::PlayerCharacter::GetSingleton()));
            }

            const auto Rule = Base ? WeaponConfig::MatchSprint(Base, Instance) : std::nullopt;

            if (Rule != LastRule && (Rule || LastRule))
            {
                if (Rule)
                {
                    REX::INFO("Sprint transitions on for {:08X}: start {}, stop {}, equip {}", Base->GetFormID(),
                        Rule->Start ? "on" : "off", Rule->Stop ? "on" : "off", Rule->Equip ? "on" : "off");
                }
                else
                {
                    REX::INFO("Sprint transitions off");
                }
            }

            LastRule = Rule;
        }

        return LastRule;
    }

    [[nodiscard]] Wanted WantedFor(const std::optional<WeaponConfig::SprintRule>& Rule, const Hold& HoldState, bool Sprinting)
    {
        if (!Rule)
            return {};

        return {
            .StartState = Rule->Start && !HoldState.SkipStart && !Sprinting ? WPNSprintStartStateID : 0,
            .Stop = Rule->Stop ? 1 : 0,
        };
    }

    bool Sync(RE::PlayerCharacter* Player, const RE::BSFixedString& Variable, std::int32_t WantedValue)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
            return false;

        if (Current != WantedValue)
        {
            Player->SetGraphVariableInt(Variable, WantedValue);

            if (Settings::VerboseLog())
                REX::INFO("Sprint transitions: {} {} -> {}", Variable.c_str(), Current, WantedValue);
        }

        return true;
    }

    void Send(const char* Event)
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const bool Accepted = Player && Player->NotifyAnimationGraphImpl(RE::BSFixedString{ Event });

        if (Settings::VerboseLog())
            REX::INFO("Sprinting equip: sent {} ({})", Event, Accepted ? "accepted" : "refused");
    }

    void Release(std::string_view Why)
    {
        if ((CurrentHold.SkipStart || CurrentHold.StopPending) && Settings::VerboseLog())
            REX::INFO("Sprinting equip: holds released ({})", Why);

        CurrentHold = {};
    }
}

namespace Sprint
{
    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (Tag.empty())
            return;

        const char* Answer = nullptr;

        {
            std::scoped_lock Guard{ Lock };
            const bool Enabled = LastRule.has_value();

            if (TagIs(Tag, "MWB_EquipSprintingEnter"))
            {
                CurrentHold = { .SkipStart = true };
            }
            else if (TagIs(Tag, "MWB_EquipSprintingWalkEnter"))
            {
                CurrentHold = { .SkipStart = true, .StopPending = true };
            }
            else if (TagIs(Tag, "MWB_CombatEnter"))
            {
                if (CurrentHold.StopPending && Enabled && LastRule->Stop)
                    Answer = "MWB_SprintStopPlay";

                CurrentHold.StopPending = false;

                if (CurrentHold.SkipStart)
                    CurrentHold.ReleaseIn = ReleaseAfterCombat;
            }
            else if (TagIs(Tag, "MWB_SprintStopEnter"))
            {
                CurrentHold.StopPending = false;
            }
            else
            {
                return;
            }

            if (Settings::VerboseLog())
                REX::INFO("Sprinting equip: {} (skip start {}, stop pending {})", Tag.c_str(), CurrentHold.SkipStart, CurrentHold.StopPending);
        }

        if (Answer)
            Send(Answer);
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        std::optional<Core::EquippedWeapon> Weapon;

        if (!Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        static const RE::BSFixedString StartVariable{ "iMWB_SprintStartState" };
        static const RE::BSFixedString StopVariable{ "iMWB_SprintStop" };
        static const RE::BSFixedString EquipStartVariable{ "iMWB_EquipStartState" };
        static const RE::BSFixedString SyncSprintVariable{ "iSyncSprintState" };

        std::int32_t Sprinting = 0;
        Player->GetGraphVariableImplInt(SyncSprintVariable, Sprinting);

        Wanted WantedValues;
        bool EquipClip = false;

        {
            std::scoped_lock Guard{ Lock };

            if (CurrentHold.SkipStart || CurrentHold.StopPending)
            {
                CurrentHold.Age += Delta;

                if (CurrentHold.ReleaseIn >= 0.0f)
                {
                    CurrentHold.ReleaseIn -= Delta;

                    if (CurrentHold.ReleaseIn <= 0.0f)
                        Release("the equip is over");
                }
                else if (CurrentHold.Age > HoldTimeout)
                {
                    Release("timeout");
                }
            }

            const auto& Rule = RuleFor(Weapon);
            SprintingFor = Sprinting == 1 ? SprintingFor + Delta : 0.0f;
            WantedValues = WantedFor(Rule, CurrentHold, SprintingFor >= SprintSettled);
            EquipClip = Rule && Rule->Equip;
        }

        const auto EquipStart = Sprinting != 1 ? EquipWalking : EquipClip ? EquipSprintingClip : EquipSprinting;
        Sync(Player, EquipStartVariable, EquipStart);

        bool Known = Sync(Player, StartVariable, WantedValues.StartState);
        Known = Sync(Player, StopVariable, WantedValues.Stop) && Known;

        if (!Known && WantedValues != Wanted{} && !WarnedMissing)
        {
            REX::WARN("The player's animation graph does not know the MWB sprint variables. In first person, sprint "
                      "transitions need Modern Weapon Behavior's GunBehavior, RootBehavior and weapon wrapping behaviors, "
                      "winning over other mods'");
            WarnedMissing = true;
        }
    }
}
