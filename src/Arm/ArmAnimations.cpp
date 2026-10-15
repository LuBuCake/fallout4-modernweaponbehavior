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

#include "ArmAnimations.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::array WeaponActionEvents{
        "attackStart"sv, "attackStart_Attack1"sv, "attackStartAuto"sv, "attackStartDual"sv, "attackStartAutoCharge"sv,
        "attackStartChargingHold"sv,
        "rifleSightedStart"sv,
        "reloadStart"sv, "MWB_ReloadReserveStart"sv, "boltChargeStart"sv,
        "meleeAttackGun"sv, "meleeAttackBayonet"sv, "meleeAttackShredder"sv,
        "meleeattackStart"sv, "meleeattackPowerStart"sv, "meleeattackBackStart"sv, "meleeattackSprintStart"sv,
        "meleeattackSneakStart"sv, "meleeAttackPowerSneakStart"sv, "meleeattackSpecial"sv, "meleeattackOnRecoilStart"sv,
        "meleeAttackRipperStart"sv, "meleeattackRipperSneakStart"sv,
        "blockStart"sv, "blockSneakStart"sv, "blockStartSneak"sv,
        "grenadeThrowStart"sv, "mineThrowStart"sv,
        "MWB_Inspect"sv, "MWB_AmmoCheck"sv, "MWB_SpecialAttack"sv,
    };

    std::vector<RE::BSFixedString> WeaponActionNames;
    std::optional<RE::BSFixedString> LockOn;
    std::optional<RE::BSFixedString> LockOff;

    std::atomic<bool> Locked{ false };

    std::atomic<bool> InCombatState{ false };
    std::optional<RE::BSFixedString> CombatEnter;
    std::optional<RE::BSFixedString> CombatExit;
    std::optional<RE::BSFixedString> FallbackEvent;
    constexpr auto ArmEventPrefix = "dyn_MWB_"sv;
}

namespace ArmAnimations
{
    void Initialize()
    {
        WeaponActionNames.assign(WeaponActionEvents.begin(), WeaponActionEvents.end());
        LockOn.emplace("MWB_ArmLockOn");
        LockOff.emplace("MWB_ArmLockOff");
        CombatEnter.emplace("MWB_CombatEnter");
        CombatExit.emplace("MWB_CombatExit");
        FallbackEvent.emplace("dyn_ActivationAllowMovement");
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (!LockOn)
            return;

        if (Tag == *CombatEnter || Tag == *CombatExit)
        {
            const bool Entering = Tag == *CombatEnter;

            if (InCombatState.exchange(Entering) != Entering && Settings::VerboseLog())
                REX::INFO("Arm animation: {} the weapon state", Entering ? "in" : "out of");

            return;
        }

        bool NowLocked;

        if (Tag == *LockOn)
            NowLocked = true;
        else if (Tag == *LockOff)
            NowLocked = false;
        else
            return;

        if (Locked.exchange(NowLocked) != NowLocked && Settings::VerboseLog())
            REX::INFO("Arm animation: weapon actions {}", NowLocked ? "locked" : "unlocked");
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (!Locked.load())
            return false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || Player->Is3rdPersonVisible())
            return false;

        if (std::ranges::find(WeaponActionNames, Event) == WeaponActionNames.end())
            return false;

        if (Settings::VerboseLog())
            REX::INFO("Arm animation: {} refused, weapon locked", Event.c_str());

        return true;
    }

    const RE::BSFixedString* Fallback(const RE::BSFixedString& Event)
    {
        if (!FallbackEvent || InCombatState.load())
            return nullptr;

        const std::string_view Name{ Event.c_str() ? Event.c_str() : "" };

        if (!Name.starts_with(ArmEventPrefix))
            return nullptr;

        if (Settings::VerboseLog())
            REX::INFO("Arm animation: {} outside the weapon state, sent as {}", Name, FallbackEvent->c_str());

        return &*FallbackEvent;
    }

    void Reset()
    {
        InCombatState = false;
        Locked = false;
    }
}
