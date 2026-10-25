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

#include "Settings.h"

#include <SimpleIni.h>
#undef ERROR

namespace
{
    REX::TIniSetting<bool> VerboseLogSetting{ "General", "bVerboseLog", false };
    REX::TIniSetting<bool> BulletInChamberSetting{ "BulletInChamber", "bEnabled", false };
    REX::TIniSetting<bool> BulletInChamberTacticalOnlySetting{ "BulletInChamber", "bTacticalReloadOnly", false };
    REX::TIniSetting<bool> DropUnspentAmmoSetting{ "DropUnspentAmmo", "bEnabled", false };
    REX::TIniSetting<bool> ManualReloadSetting{ "ManualReload", "bEnabled", false };
    REX::TIniSetting<bool> SlideLockSetting{ "SlideLock", "bEnabled", true };
    REX::TIniSetting<bool> FireModeSwitchSetting{ "FireModeSwitch", "bEnabled", true };
    REX::TIniSetting<bool> WeaponHUDSetting{ "WeaponHUD", "bEnabled", true };
    REX::TIniSetting<bool> AmmoCheckEveryGunSetting{ "AmmoCheck", "bEveryGun", false };
    REX::TIniSetting<bool> ThirdPersonSprintFOVSetting{ "ThirdPersonSprintFOV", "bEnabled", true };
    REX::TIniSetting<bool> AimWobbleVanillaScopesSetting{ "AimWobble", "bVanillaScopes", true };
    REX::TIniSetting<bool> AimWobble3DScopesSetting{ "AimWobble", "b3DScopes", true };
    REX::TIniSetting<bool> AimWobbleNoScopeSetting{ "AimWobble", "bNoScope", false };
    REX::TIniSetting<bool> HoldBlockSetting{ "HoldBlock", "bEnabled", false };
    REX::TIniSetting<bool> FireWhileSprintingSetting{ "FireWhileSprinting", "bEnabled", false };
    REX::TIniSetting<bool> SightedFireJiggleSetting{ "SightedFireJiggle", "bEnabled", true };
    REX::TIniSetting<bool> FullEquipOnSwitchSetting{ "FullEquipOnSwitch", "bEnabled", false };
    REX::TIniSetting<bool> TacticalSprintSetting{ "TacticalSprint", "bEnabled", false };
    REX::TIniSetting<bool> SlideSetting{ "Slide", "bEnabled", false };

    REX::TIniSetting<float> TacticalSprintSpeedSetting{ "TacticalSprint", "fSpeedMult", 1.3f };
    REX::TIniSetting<float> TacticalSprintActionPointsSetting{ "TacticalSprint", "fActionPointsMult", 1.3f };
    REX::TIniSetting<float> TacticalSprintAnimationSetting{ "TacticalSprint", "fAnimationSpeedMult", 1.25f };
    REX::TIniSetting<float> SlideDurationSetting{ "Slide", "fDuration", 0.6f };
    REX::TIniSetting<float> SlideCommittedTimeSetting{ "Slide", "fCommittedTime", 0.05f };
    REX::TIniSetting<float> SlideSpeedSetting{ "Slide", "fSpeedMult", 1.25f };

    constexpr float MinimumMult = 1.0f;
    constexpr float MaximumMult = 3.0f;
    constexpr float MinimumSlideDuration = 0.1f;
    constexpr float MaximumSlideDuration = 2.0f;

    constexpr auto BaseFile = "Data/F4SE/Plugins/MWB.ini";
    constexpr auto CustomFile = "Data/F4SE/Plugins/MWB_Custom.ini";
    constexpr auto McmFile = "Data/MCM/Settings/ModernWeaponBehavior.ini";

    struct McmBool
    {
        const char*             Section;
        const char*             Key;
        REX::TIniSetting<bool>* Setting;
    };

    const std::array McmBools
    {
        McmBool{ "General", "bVerboseLog", &VerboseLogSetting },
        McmBool{ "BulletInChamber", "bEnabled", &BulletInChamberSetting },
        McmBool{ "BulletInChamber", "bTacticalReloadOnly", &BulletInChamberTacticalOnlySetting },
        McmBool{ "DropUnspentAmmo", "bEnabled", &DropUnspentAmmoSetting },
        McmBool{ "ManualReload", "bEnabled", &ManualReloadSetting },
        McmBool{ "SlideLock", "bEnabled", &SlideLockSetting },
        McmBool{ "FireModeSwitch", "bEnabled", &FireModeSwitchSetting },
        McmBool{ "WeaponHUD", "bEnabled", &WeaponHUDSetting },
        McmBool{ "AmmoCheck", "bEveryGun", &AmmoCheckEveryGunSetting },
        McmBool{ "ThirdPersonSprintFOV", "bEnabled", &ThirdPersonSprintFOVSetting },
        McmBool{ "AimWobble", "bVanillaScopes", &AimWobbleVanillaScopesSetting },
        McmBool{ "AimWobble", "b3DScopes", &AimWobble3DScopesSetting },
        McmBool{ "AimWobble", "bNoScope", &AimWobbleNoScopeSetting },
        McmBool{ "HoldBlock", "bEnabled", &HoldBlockSetting },
        McmBool{ "FireWhileSprinting", "bEnabled", &FireWhileSprintingSetting },
        McmBool{ "SightedFireJiggle", "bEnabled", &SightedFireJiggleSetting },
        McmBool{ "FullEquipOnSwitch", "bEnabled", &FullEquipOnSwitchSetting },
        McmBool{ "TacticalSprint", "bEnabled", &TacticalSprintSetting },
        McmBool{ "Slide", "bEnabled", &SlideSetting },
    };

    struct McmFloat
    {
        const char*              Section;
        const char*              Key;
        REX::TIniSetting<float>* Setting;
    };

    const std::array McmFloats
    {
        McmFloat{ "TacticalSprint", "fSpeedMult", &TacticalSprintSpeedSetting },
        McmFloat{ "TacticalSprint", "fActionPointsMult", &TacticalSprintActionPointsSetting },
        McmFloat{ "TacticalSprint", "fAnimationSpeedMult", &TacticalSprintAnimationSetting },
        McmFloat{ "Slide", "fDuration", &SlideDurationSetting },
        McmFloat{ "Slide", "fCommittedTime", &SlideCommittedTimeSetting },
        McmFloat{ "Slide", "fSpeedMult", &SlideSpeedSetting },
    };

    bool FireModeSwitchAtStartup{ true };

    void ReadFiles()
    {
        REX::FIniSettingStore::GetSingleton()->Load();

        CSimpleIniA McmIni;
        McmIni.SetUnicode(true);

        if (!std::filesystem::exists(McmFile) || McmIni.LoadFile(McmFile) < 0)
            return;

        for (const auto& [Section, Key, Setting] : McmBools)
        {
            if (McmIni.GetValue(Section, Key))
                Setting->SetValue(McmIni.GetBoolValue(Section, Key, Setting->GetValue()));
        }

        for (const auto& [Section, Key, Setting] : McmFloats)
        {
            if (McmIni.GetValue(Section, Key))
                Setting->SetValue(static_cast<float>(McmIni.GetDoubleValue(Section, Key, Setting->GetValue())));
        }
    }

    void LogValues(std::string_view When)
    {
        REX::INFO("Settings ({}): verboseLog={}, bulletInChamber={} (tactical reload only {}), dropUnspentAmmo={}, manualReload={}, slideLock={}, fireModeSwitch={}, weaponHUD={}, ammoCheckEveryGun={}, "
                  "thirdPersonSprintFOV={}, aimWobble(vanillaScopes={}, 3DScopes={}, noScope={}), holdBlock={}, fireWhileSprinting={}, sightedFireJiggle={}, "
                  "fullEquipOnSwitch={}, tacticalSprint={} (speed x{:.2f}, action points x{:.2f}, animation x{:.2f}), slide={} (duration {:.2f}s, "
                  "committed {:.2f}s, speed x{:.2f})",
            When, VerboseLogSetting.GetValue(), BulletInChamberSetting.GetValue(), BulletInChamberTacticalOnlySetting.GetValue(), DropUnspentAmmoSetting.GetValue(), ManualReloadSetting.GetValue(),
            SlideLockSetting.GetValue(), FireModeSwitchSetting.GetValue(), WeaponHUDSetting.GetValue(), AmmoCheckEveryGunSetting.GetValue(), ThirdPersonSprintFOVSetting.GetValue(),
            AimWobbleVanillaScopesSetting.GetValue(), AimWobble3DScopesSetting.GetValue(), AimWobbleNoScopeSetting.GetValue(), HoldBlockSetting.GetValue(), FireWhileSprintingSetting.GetValue(),
            SightedFireJiggleSetting.GetValue(), FullEquipOnSwitchSetting.GetValue(), TacticalSprintSetting.GetValue(),
            Settings::TacticalSprintSpeed(), Settings::TacticalSprintActionPoints(), Settings::TacticalSprintAnimation(), SlideSetting.GetValue(),
            Settings::SlideDuration(), Settings::SlideCommittedTime(), Settings::SlideSpeed());
    }
}

namespace Settings
{
    void Load()
    {
        REX::FIniSettingStore::GetSingleton()->Init(BaseFile, CustomFile);
        ReadFiles();
        FireModeSwitchAtStartup = FireModeSwitchSetting.GetValue();
        LogValues("startup");
    }

    std::vector<std::string_view> Reload()
    {
        ReadFiles();
        LogValues("reloaded");

        std::vector<std::string_view> Pending;

        if (FireModeSwitchSetting.GetValue() != FireModeSwitchAtStartup)
            Pending.push_back("Fire mode switch");

        return Pending;
    }

    bool VerboseLog()
    {
        return VerboseLogSetting.GetValue();
    }

    bool BulletInChamber()
    {
        return BulletInChamberSetting.GetValue();
    }

    bool BulletInChamberTacticalOnly()
    {
        return BulletInChamberTacticalOnlySetting.GetValue();
    }

    bool DropUnspentAmmo()
    {
        return DropUnspentAmmoSetting.GetValue();
    }

    bool ManualReload()
    {
        return ManualReloadSetting.GetValue();
    }

    bool SlideLock()
    {
        return SlideLockSetting.GetValue();
    }

    bool FireModeSwitch()
    {
        return FireModeSwitchAtStartup;
    }

    bool WeaponHUD()
    {
        return WeaponHUDSetting.GetValue();
    }

    bool AmmoCheckEveryGun()
    {
        return AmmoCheckEveryGunSetting.GetValue();
    }

    bool ThirdPersonSprintFOV()
    {
        return ThirdPersonSprintFOVSetting.GetValue();
    }

    bool AimWobbleVanillaScopes()
    {
        return AimWobbleVanillaScopesSetting.GetValue();
    }

    bool AimWobble3DScopes()
    {
        return AimWobble3DScopesSetting.GetValue();
    }

    bool AimWobbleNoScope()
    {
        return AimWobbleNoScopeSetting.GetValue();
    }

    bool HoldBlock()
    {
        return HoldBlockSetting.GetValue();
    }

    bool FireWhileSprinting()
    {
        return FireWhileSprintingSetting.GetValue();
    }

    bool SightedFireJiggle()
    {
        return SightedFireJiggleSetting.GetValue();
    }

    bool FullEquipOnSwitch()
    {
        return FullEquipOnSwitchSetting.GetValue();
    }

    bool TacticalSprint()
    {
        return TacticalSprintSetting.GetValue();
    }

    bool Slide()
    {
        return SlideSetting.GetValue();
    }

    float TacticalSprintSpeed()
    {
        return std::clamp(TacticalSprintSpeedSetting.GetValue(), MinimumMult, MaximumMult);
    }

    float TacticalSprintActionPoints()
    {
        return std::clamp(TacticalSprintActionPointsSetting.GetValue(), MinimumMult, MaximumMult);
    }

    float TacticalSprintAnimation()
    {
        return std::clamp(TacticalSprintAnimationSetting.GetValue(), MinimumMult, MaximumMult);
    }

    float SlideDuration()
    {
        return std::clamp(SlideDurationSetting.GetValue(), MinimumSlideDuration, MaximumSlideDuration);
    }

    float SlideCommittedTime()
    {
        return std::clamp(SlideCommittedTimeSetting.GetValue(), 0.0f, SlideDuration());
    }

    float SlideSpeed()
    {
        return std::clamp(SlideSpeedSetting.GetValue(), MinimumMult, MaximumMult);
    }
}
