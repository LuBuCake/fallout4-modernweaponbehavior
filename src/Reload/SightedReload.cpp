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

#include "SightedReload.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    RE::TESObjectWEAP*               Base{ nullptr };
    RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
    bool                             Matched{ false };
    bool                             WarnedMissing{ false };

    constexpr float AimRetryInterval = 0.1f;

    float AimRetryIn{ 0.0f };
    bool  AimRequested{ false };

    std::atomic<bool> AimHeld{ false };
    std::atomic<bool> AimReleased{ false };

    std::atomic<bool> ReloadActive{ false };
    std::atomic<bool> Wanted{ false };

    bool Sync(RE::PlayerCharacter* Player, const RE::BSFixedString& Variable, std::int32_t WantedValue)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
            return false;

        if (Current != WantedValue)
        {
            Player->SetGraphVariableInt(Variable, WantedValue);

            if (Settings::VerboseLog())
                REX::INFO("Sighted reload: {} {} -> {}", Variable.c_str(), Current, WantedValue);
        }

        return true;
    }

    std::mutex Lock;
    bool       InSightedReload{ false };
    bool       ExpectEnter{ false };
    bool       RepeatedEnter{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    const RE::BSFixedString& LockVariable()
    {
        static const RE::BSFixedString Variable{ "iMWB_AimLock" };

        return Variable;
    }

    [[nodiscard]] bool SightedClipActive()
    {
        static const RE::BSFixedString Variable{ "bMWB_ReloadSighted" };
        const auto Player = RE::PlayerCharacter::GetSingleton();
        bool       Active = false;

        return Player && Player->GetGraphVariableImplBool(Variable, Active) && Active;
    }

    [[nodiscard]] bool MatchFor(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        const auto CurrentBase = Weapon ? Weapon->Base : nullptr;
        const auto CurrentInstance = Weapon ? Weapon->Instance : nullptr;

        if (CurrentBase != Base || CurrentInstance != Instance)
        {
            Base = CurrentBase;
            Instance = CurrentInstance;
            const bool CurrentMatch = CurrentBase && WeaponConfig::MatchSighted(CurrentBase, CurrentInstance);

            if (CurrentMatch != Matched)
            {
                if (CurrentMatch)
                    REX::INFO("Sighted reload on for {:08X}", CurrentBase->GetFormID());
                else
                    REX::INFO("Sighted reload off");
            }

            Matched = CurrentMatch;
        }

        return Matched;
    }
}

namespace SightedReload
{
    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        std::optional<Core::EquippedWeapon> Weapon;

        if (!Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        const std::int32_t WantedValue = MatchFor(Weapon) ? 1 : 0;
        Wanted = WantedValue != 0;

        static const RE::BSFixedString SightedVariable{ "iMWB_SightedReload" };

        if (!Sync(Player, SightedVariable, WantedValue))
        {
            if (WantedValue && !WarnedMissing)
            {
                REX::WARN("The player's animation graph does not know iMWB_SightedReload. In first person, sighted "
                          "reloads need Modern Weapon Behavior's GunBehavior, RootBehavior and weapon wrapping behaviors, "
                          "winning over other mods'");
                WarnedMissing = true;
            }

            return;
        }

        static const RE::BSFixedString InReloadVariable{ "bMWB_InReload" };
        static const RE::BSFixedString SightedClipVariable{ "bMWB_ReloadSighted" };
        bool InReload = false;
        bool SightedClip = false;
        Player->GetGraphVariableImplBool(InReloadVariable, InReload);
        Player->GetGraphVariableImplBool(SightedClipVariable, SightedClip);
        InReload = InReload && ReloadActive;

        Sync(Player, LockVariable(), WantedValue && InReload ? 1 : 0);

        if (AimReleased.exchange(false) && WantedValue && InReload && SightedClip)
        {
            static const RE::BSFixedString ReleaseEvent{ "rifleSightedEnd" };
            const bool Accepted = Player->NotifyAnimationGraphImpl(ReleaseEvent);

            if (Settings::VerboseLog())
                REX::INFO("Sighted reload: aim button up, rifleSightedEnd {}", Accepted ? "accepted" : "refused");
        }

        if (!WantedValue || !InReload || SightedClip || !AimHeld)
        {
            AimRequested = false;
            AimRetryIn = 0.0f;

            return;
        }

        if (AimRequested)
            return;

        AimRetryIn -= Delta;

        if (AimRetryIn > 0.0f)
            return;

        AimRetryIn = AimRetryInterval;

        static const RE::BSFixedString AimEvent{ "rifleSightedStart" };
        AimRequested = Player->NotifyAnimationGraphImpl(AimEvent);

        if (Settings::VerboseLog())
            REX::INFO("Sighted reload: aim held through the reload, rifleSightedStart {}", AimRequested ? "accepted" : "refused");
    }

    void SetAimButton(bool Held)
    {
        if (AimHeld.exchange(Held) == Held)
            return;

        if (!Held)
            AimReleased = true;

        if (Settings::VerboseLog())
            REX::INFO("Sighted reload: aim button {}", Held ? "down" : "up");
    }

    bool OnAnimationEvent(const RE::BSAnimationGraphEvent& Event)
    {
        const auto& Tag = Event.tag;

        if (Tag.empty())
            return false;

        const std::scoped_lock Guard{ Lock };

        const bool ReloadEnter = TagIs(Tag, "reloadStateEnter"sv);

        if (ReloadEnter || TagIs(Tag, "reloadStateExit"sv))
        {
            ReloadActive = ReloadEnter;

            if (const auto Player = RE::PlayerCharacter::GetSingleton())
                Sync(Player, LockVariable(), ReloadEnter && Wanted ? 1 : 0);
        }

        if (TagIs(Tag, "MWB_ReloadSightedEnter"sv))
        {
            RepeatedEnter = InSightedReload && SightedClipActive();
            ExpectEnter = true;
            InSightedReload = true;

            return false;
        }

        if (TagIs(Tag, "MWB_ReloadSightedLeave"sv) || TagIs(Tag, "reloadStateExit"sv))
        {
            if (InSightedReload && Settings::VerboseLog())
                REX::INFO("Sighted reload: left ({})", Tag.c_str());

            InSightedReload = false;
            ExpectEnter = false;

            return false;
        }

        if (!TagIs(Tag, "sightedState"sv))
            return false;

        if (TagIs(Event.payload, "Enter"sv))
        {
            const bool DropRepeatedEnter = ExpectEnter && RepeatedEnter;
            ExpectEnter = false;

            if (DropRepeatedEnter && Settings::VerboseLog())
                REX::INFO("Sighted reload: next pass, repeated sightedState Enter dropped");

            return DropRepeatedEnter;
        }

        if (TagIs(Event.payload, "Exit"sv) && InSightedReload)
        {
            if (SightedClipActive())
            {
                if (Settings::VerboseLog())
                    REX::INFO("Sighted reload: late sightedState Exit dropped");

                return true;
            }

            InSightedReload = false;
        }

        return false;
    }

    void Reset()
    {
        const std::scoped_lock Guard{ Lock };
        InSightedReload = false;
        ExpectEnter = false;
        RepeatedEnter = false;
        ReloadActive = false;
    }
}
