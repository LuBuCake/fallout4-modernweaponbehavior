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

#pragma once

namespace BehaviorNames
{
    enum class VariableType : std::int8_t
    {
        Bool = 0,
        Int32 = 3,
        Real = 4
    };

    struct Variable
    {
        std::string_view Name;
        VariableType     Type;
        std::int32_t     InitialValue;
    };

    inline constexpr std::array Events
    {
        "MWB_UseAlchemyItem"sv,
        "MWB_WeaponFireAmmo"sv,
        "MWB_Event"sv,
        "MWB_CastSpell"sv,
        "MWB_SwitchFireMode"sv,
        "TrdP"sv,
        "MWB_ReloadCroppedLoop"sv,
        "reloadToIdleStart"sv,
        "MWB_ReloadToIdleStartSlave"sv,
        "MWB_ReloadReserveStartSlave"sv,
        "MWB_Reload"sv,
        "MWB_ReloadReserveStart"sv,
        "MWB_SprintStartDone"sv,
        "MWB_SprintStopDone"sv,
        "MWB_EquipSprintingEnter"sv,
        "MWB_EquipSprintingWalkEnter"sv,
        "MWB_CombatEnter"sv,
        "MWB_SprintStopEnter"sv,
        "MWB_SprintStopPlay"sv,
        "MWB_EquipSprintingDone"sv,
        "MWB_ReloadSightedEnd"sv,
        "MWB_ReloadSightedEnter"sv,
        "MWB_ReloadSightedLeave"sv,
        "MWB_Inspect"sv,
        "MWB_InspectDone"sv,
        "MWB_AmmoCheck"sv,
        "MWB_AmmoCheckDone"sv,
        "MWB_SpecialAttack"sv,
        "MWB_SpecialAttackDone"sv,
        "dyn_MWB_LeftArm"sv,
        "dyn_MWB_LeftArmLocked"sv,
        "dyn_MWB_RightArm"sv,
        "dyn_MWB_RightArmLocked"sv,
        "dyn_MWB_LeftArmWithCamera"sv,
        "dyn_MWB_LeftArmWithCameraLocked"sv,
        "dyn_MWB_RightArmWithCamera"sv,
        "dyn_MWB_RightArmWithCameraLocked"sv,
        "MWB_ArmStop"sv,
        "MWB_ArmLockOn"sv,
        "MWB_ArmLockOff"sv,
        "MWB_ArmOutDone"sv,
        "MWB_CombatExit"sv,
        "MWB_LeftArmEnd"sv,
        "MWB_LeftArmLockedEnd"sv,
        "MWB_RightArmEnd"sv,
        "MWB_RightArmLockedEnd"sv,
        "MWB_LeftArmWithCameraEnd"sv,
        "MWB_LeftArmWithCameraLockedEnd"sv,
        "MWB_RightArmWithCameraEnd"sv,
        "MWB_RightArmWithCameraLockedEnd"sv,
        "MWB_InspectEmpty"sv,
        "MWB_InspectEmptyDone"sv,
        "MWB_AmmoCheckEmpty"sv,
        "MWB_AmmoCheckEmptyDone"sv,
        "MWB_SwitchFireModeStart"sv,
        "MWB_SwitchFireModeDone"sv,
        "MWB_SwitchFireModeExit"sv,
        "MWB_ScopeSteady"sv,
        "MWB_AimDownSightInDone"sv,
        "MWB_AimDownSightOutDone"sv,
        "MWB_SprintTacticalStartPlay"sv,
        "MWB_SprintTacticalPlay"sv,
        "MWB_SprintTacticalStartDone"sv,
        "MWB_SprintTacticalStopDone"sv,
        "MWB_SlidePlay"sv,
        "MWB_SlideEnd"sv,
        "MWB_SlideDone"sv,
        "MWB_SightedReloadStart"sv,
        "MWB_LeftArmLockOn"sv,
        "MWB_LeftArmLockOff"sv,
        "MWB_RightArmLockOn"sv,
        "MWB_RightArmLockOff"sv,
        "MWB_LeftArmFree"sv,
        "MWB_RightArmFree"sv,
        "MWB_RightArmOutDone"sv,
        "MWB_BoltChargeStart"sv,
        "MWB_BoltSightedEnter"sv,
        "MWB_BoltSightedLeave"sv,
        "MWB_BoltSightedEnd"sv,
        "MWB_LeftArmRestartable"sv,
        "MWB_RightArmRestartable"sv,
        "MWB_SprintJumpStopDone"sv
    };

    inline constexpr std::array Variables
    {
        Variable{ "iMWB_SprintCamera"sv, VariableType::Int32, 1 },
        Variable{ "fLocomotionSprintPlaybackSpeed"sv, VariableType::Real, std::bit_cast<std::int32_t>(1.0f) },
        Variable{ "iMWB_HoldBlock"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_ReloadStartState"sv, VariableType::Int32, 3 },
        Variable{ "iMWB_FireLastReady"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SprintStartState"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SprintStop"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_EquipStartState"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SightedReload"sv, VariableType::Int32, 0 },
        Variable{ "bMWB_ReloadSighted"sv, VariableType::Bool, 0 },
        Variable{ "bMWB_InReload"sv, VariableType::Bool, 0 },
        Variable{ "iMWB_AimLock"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_ScopeSteady"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SightedFireJiggle"sv, VariableType::Int32, 1 },
        Variable{ "iMWB_AimDownSight"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SprintTacticalLoop"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_FireLastSighted"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SightedReloadStart"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltSightedStart"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltDue"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltSwap"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltReady"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltPlugin"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_BoltInSights"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SprintJumpStop"sv, VariableType::Int32, 0 },
        Variable{ "iMWB_SprintJumpLand"sv, VariableType::Int32, 0 }
    };
}
