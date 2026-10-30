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

#include "Core.h"
#include "Actions/FirstPerson.h"
#include "Actions/WeaponActions.h"
#include "Magic/MagicAnnotations.h"
#include "Arm/ArmAnimations.h"
#include "Arm/ArmSpace.h"
#include "Config/WeaponConfig.h"
#include "Fire/AmmoFire.h"
#include "HUD/AmmoCheckDisplay.h"
#include "HUD/WeaponHUD.h"
#include "Hooks/AnimationEventHook.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Hooks/GraphGenerateHook.h"
#include "Hooks/IdleStartHook.h"
#include "Hooks/InputHandler.h"
#include "Hooks/NotifyAnimationGraphHook.h"
#include "Hooks/QueuedEventHook.h"
#include "Hooks/SubgraphAnimationHook.h"
#include "Hooks/UpdateAnimationHook.h"
#include "Melee/HoldBlock.h"
#include "Papyrus/Papyrus.h"
#include "Reload/BoltChargeResync.h"
#include "Reload/BulletInChamber.h"
#include "Reload/DropUnspentAmmo.h"
#include "Reload/LoopReload.h"
#include "Reload/ManualReload.h"
#include "Reload/SightedBoltCharge.h"
#include "Reload/SightedReload.h"
#include "Reload/TacticalReload.h"
#include "Settings/ExclusionPanel.h"
#include "Settings/Settings.h"
#include "Sprint/Slide.h"
#include "Sprint/SprintFire.h"
#include "Sprint/SprintSpeeds.h"
#include "Sprint/TacticalSprint.h"
#include "Weapon/AimAfterLoad.h"
#include "Weapon/AimDownSight.h"
#include "Weapon/FireMode.h"
#include "Weapon/FireModePose.h"
#include "Weapon/ScopeSway.h"
#include "Weapon/SlideLock.h"
#include "Weapon/WeaponBones.h"

namespace
{
    constexpr std::array KnownRuntimes
    {
        REL::Version{ 1, 10, 163, 0 },
        REL::Version{ 1, 10, 980, 0 },
        REL::Version{ 1, 10, 984, 0 },
        REL::Version{ 1, 11, 137, 0 },
        REL::Version{ 1, 11, 159, 0 },
        REL::Version{ 1, 11, 169, 0 },
        REL::Version{ 1, 11, 191, 0 },
        REL::Version{ 1, 11, 221, 0 },
        REL::Version{ 1, 11, 240, 0 },
    };
}

namespace Core
{
    bool RuntimeSupported()
    {
        const auto GameVersion = REX::FModule::GetExecutingModule().GetFileVersion();

        if (std::ranges::find(KnownRuntimes, GameVersion) != KnownRuntimes.end())
            return true;

        REX::WARN("Game version [{}] is not one this build knows. Staying inert.", GameVersion);

        return false;
    }

    void F4SELoaded()
    {
        Settings::Load();
        Papyrus::Register();
        ExclusionPanel::Register();
        AmmoFire::InstallEditorIDHook();
        MagicAnnotations::InstallEditorIDHooks();
        Hooks::InstallSubgraphAnimationHook();
    }

    void GameDataReady()
    {
        WeaponConfig::Load();
        Reload::Initialize();
        TacticalReload::Initialize();
        BoltChargeResync::Initialize();
        ArmAnimations::Initialize();
        WeaponActions::Initialize();
        FirstPerson::Initialize();
        AmmoCheckDisplay::Initialize();
        FireMode::Initialize();
        Hooks::InstallAnimationEventHook();
        Hooks::InstallActorAnimationEventHook();
        Hooks::InstallNotifyAnimationGraphHook();
        Hooks::InstallQueuedEventHook();
        Hooks::InstallIdleStartHook();
        Hooks::InstallClipGeneratorHook();
        Hooks::InstallUpdateAnimationHook();
        Hooks::InstallActorUpdateAnimationHook();
        Hooks::InstallInputHandler();
        AmmoFire::Install();
        ArmSpace::Install();
        Hooks::InstallGraphGenerateHook();
        BulletInChamber::Install();
        ManualReload::Install();
        ScopeSway::Install();
        HoldBlock::Install();
        SprintFire::Install();
        TacticalSprint::Install();
        Slide::Install();
        SprintSpeeds::Install();
        WeaponHUD::Install();
        Settings::InstallMenuWatcher();
    }

    void PreLoadGame()
    {
        Reload::Abort("loading a save");
        Hooks::ForgetClips();
        MagicAnnotations::Reset();
        SightedReload::Reset();
        SightedBoltCharge::Reset();
        AimDownSight::Reset();
        ArmAnimations::Reset();
        ArmSpace::Reset();
        SlideLock::Reset();
        FireModePose::Reset();
        WeaponBones::Reset();
        FireMode::Reset();
        BulletInChamber::Reset();
        DropUnspentAmmo::Reset();
        ScopeSway::Reset();
        HoldBlock::Reset();
        SprintFire::Reset();
        TacticalSprint::Reset();
        Slide::Reset();
        AimAfterLoad::Reset();
    }

    void PostLoadGame()
    {
        ScopeSway::DisableSeeThroughScopesHoldBreath();
        AmmoCheckDisplay::EnsureOpen();
        AimAfterLoad::Arm();
    }
}
