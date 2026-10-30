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

    enum Side : std::size_t
    {
        Left,
        Right
    };

    struct ArmEvents
    {
        std::optional<RE::BSFixedString> LockOn;
        std::optional<RE::BSFixedString> LockOff;
        std::optional<RE::BSFixedString> Free;
    };

    struct ArmState
    {
        std::atomic<bool> Locked{ false };
        std::atomic<bool> Busy{ false };
        std::atomic<std::int64_t> PendingSince{ 0 };
    };

    constexpr std::array<std::string_view, 2> SideNames{ "left"sv, "right"sv };
    constexpr std::chrono::seconds PendingTimeout{ 3 };

    std::vector<RE::BSFixedString> WeaponActionNames;
    std::optional<RE::BSFixedString> UnloadEvent;
    std::array<ArmEvents, 2> Events;
    std::array<ArmState, 2> Arms;

    std::atomic<bool> InCombatState{ false };
    std::optional<RE::BSFixedString> CombatEnter;
    std::optional<RE::BSFixedString> CombatExit;
    std::optional<RE::BSFixedString> FallbackEvent;
    constexpr auto ArmEventPrefix = "dyn_MWB_"sv;

    [[nodiscard]] std::int64_t Now()
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    [[nodiscard]] std::optional<Side> SideOf(const RE::BSFixedString& Event)
    {
        const std::string_view Name{ Event.c_str() ? Event.c_str() : "" };

        if (!Name.starts_with(ArmEventPrefix))
            return std::nullopt;

        const auto Rest = Name.substr(ArmEventPrefix.size());

        if (Rest.starts_with("LeftArm"sv))
            return Left;

        if (Rest.starts_with("RightArm"sv))
            return Right;

        return std::nullopt;
    }

    [[nodiscard]] bool Pending(const ArmState& Arm)
    {
        const auto Since = Arm.PendingSince.load();

        return Since != 0 && Now() - Since < std::chrono::duration_cast<std::chrono::steady_clock::duration>(PendingTimeout).count();
    }
}

namespace ArmAnimations
{
    void Initialize()
    {
        WeaponActionNames.assign(WeaponActionEvents.begin(), WeaponActionEvents.end());
        UnloadEvent.emplace("AnimObjUnequip");
        Events[Left] = { RE::BSFixedString{ "MWB_LeftArmLockOn" }, RE::BSFixedString{ "MWB_LeftArmLockOff" }, RE::BSFixedString{ "MWB_LeftArmFree" } };
        Events[Right] = { RE::BSFixedString{ "MWB_RightArmLockOn" }, RE::BSFixedString{ "MWB_RightArmLockOff" }, RE::BSFixedString{ "MWB_RightArmFree" } };
        CombatEnter.emplace("MWB_CombatEnter");
        CombatExit.emplace("MWB_CombatExit");
        FallbackEvent.emplace("dyn_ActivationAllowMovement");
    }

    bool OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (!UnloadEvent)
            return false;

        if (Tag == *CombatEnter || Tag == *CombatExit)
        {
            const bool Entering = Tag == *CombatEnter;

            if (InCombatState.exchange(Entering) != Entering && Settings::VerboseLog())
                REX::INFO("Arm animation: {} the weapon state", Entering ? "in" : "out of");

            return false;
        }

        for (std::size_t i = 0; i < Arms.size(); ++i)
        {
            auto& Arm = Arms[i];
            const auto& Names = Events[i];

            if (Tag == *Names.LockOn || Tag == *Names.LockOff)
            {
                const bool NowLocked = Tag == *Names.LockOn;
                Arm.Busy = true;
                Arm.PendingSince = 0;

                if (Arm.Locked.exchange(NowLocked) != NowLocked && Settings::VerboseLog())
                    REX::INFO("Arm animation: {} arm {} the weapon actions", SideNames[i], NowLocked ? "locks" : "unlocks");

                return false;
            }

            if (Tag == *Names.Free)
            {
                Arm.Locked = false;

                if (!Arm.Busy.exchange(false))
                    return false;

                const auto& Other = Arms[1 - i];
                const bool Unload = !Other.Busy.load() && !Pending(Other);

                if (Settings::VerboseLog())
                    REX::INFO("Arm animation: {} arm free{}", SideNames[i], Unload ? ", anim objects unloaded" : ", the other arm still plays");

                return Unload;
            }
        }

        return false;
    }

    const RE::BSFixedString& Unload()
    {
        return *UnloadEvent;
    }

    bool RefuseIdle(const RE::BSFixedString& Event)
    {
        const auto ArmSide = SideOf(Event);

        if (!ArmSide || !InCombatState.load())
            return false;

        auto& Arm = Arms[*ArmSide];

        if (Arm.Busy.load() || Pending(Arm))
        {
            if (Settings::VerboseLog())
                REX::INFO("Arm animation: {} refused, the {} arm already plays one", Event.c_str(), SideNames[*ArmSide]);

            return true;
        }

        Arm.PendingSince = Now();
        return false;
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (!InCombatState.load() || (!Arms[Left].Locked.load() && !Arms[Right].Locked.load()))
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

    const RE::BSFixedString* RefusedFallback(const RE::BSFixedString& Event)
    {
        if (!FallbackEvent)
            return nullptr;

        const std::string_view Name{ Event.c_str() ? Event.c_str() : "" };

        if (!Name.starts_with(ArmEventPrefix))
            return nullptr;

        if (Settings::VerboseLog())
            REX::INFO("Arm animation: {} refused by the arm states, sent as {}", Name, FallbackEvent->c_str());

        return &*FallbackEvent;
    }

    void Reset()
    {
        InCombatState = false;

        for (auto& Arm : Arms)
        {
            Arm.Locked = false;
            Arm.Busy = false;
            Arm.PendingSince = 0;
        }
    }
}
