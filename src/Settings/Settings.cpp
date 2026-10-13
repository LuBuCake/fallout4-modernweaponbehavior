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
    REX::TIniSetting<bool> DropUnspentAmmoSetting{ "DropUnspentAmmo", "bEnabled", false };
    REX::TIniSetting<bool> ManualReloadSetting{ "ManualReload", "bEnabled", false };
    REX::TIniSetting<bool> SlideLockSetting{ "SlideLock", "bEnabled", true };
    REX::TIniSetting<bool> FireModeSwitchSetting{ "FireModeSwitch", "bEnabled", true };
    REX::TIniSetting<bool> WeaponHUDSetting{ "WeaponHUD", "bEnabled", true };
    REX::TIniSetting<bool> ThirdPersonSprintFOVSetting{ "ThirdPersonSprintFOV", "bEnabled", true };
    REX::TIniSetting<bool> AimWobbleVanillaScopesSetting{ "AimWobble", "bVanillaScopes", true };
    REX::TIniSetting<bool> AimWobble3DScopesSetting{ "AimWobble", "b3DScopes", true };
    REX::TIniSetting<bool> AimWobbleNoScopeSetting{ "AimWobble", "bNoScope", false };
    REX::TIniSetting<bool> HoldBlockSetting{ "HoldBlock", "bEnabled", false };
    REX::TIniSetting<bool> FireWhileSprintingSetting{ "FireWhileSprinting", "bEnabled", false };
    REX::TIniSetting<bool> SightedFireJiggleSetting{ "SightedFireJiggle", "bEnabled", true };

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
        McmBool{ "DropUnspentAmmo", "bEnabled", &DropUnspentAmmoSetting },
        McmBool{ "ManualReload", "bEnabled", &ManualReloadSetting },
        McmBool{ "SlideLock", "bEnabled", &SlideLockSetting },
        McmBool{ "FireModeSwitch", "bEnabled", &FireModeSwitchSetting },
        McmBool{ "WeaponHUD", "bEnabled", &WeaponHUDSetting },
        McmBool{ "ThirdPersonSprintFOV", "bEnabled", &ThirdPersonSprintFOVSetting },
        McmBool{ "AimWobble", "bVanillaScopes", &AimWobbleVanillaScopesSetting },
        McmBool{ "AimWobble", "b3DScopes", &AimWobble3DScopesSetting },
        McmBool{ "AimWobble", "bNoScope", &AimWobbleNoScopeSetting },
        McmBool{ "HoldBlock", "bEnabled", &HoldBlockSetting },
        McmBool{ "FireWhileSprinting", "bEnabled", &FireWhileSprintingSetting },
        McmBool{ "SightedFireJiggle", "bEnabled", &SightedFireJiggleSetting },
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
    }

    void LogValues(std::string_view When)
    {
        REX::INFO("Settings ({}): verboseLog={}, bulletInChamber={}, dropUnspentAmmo={}, manualReload={}, slideLock={}, fireModeSwitch={}, weaponHUD={}, "
                  "thirdPersonSprintFOV={}, aimWobble(vanillaScopes={}, 3DScopes={}, noScope={}), holdBlock={}, fireWhileSprinting={}, sightedFireJiggle={}",
            When, VerboseLogSetting.GetValue(), BulletInChamberSetting.GetValue(), DropUnspentAmmoSetting.GetValue(), ManualReloadSetting.GetValue(),
            SlideLockSetting.GetValue(), FireModeSwitchSetting.GetValue(), WeaponHUDSetting.GetValue(), ThirdPersonSprintFOVSetting.GetValue(),
            AimWobbleVanillaScopesSetting.GetValue(), AimWobble3DScopesSetting.GetValue(), AimWobbleNoScopeSetting.GetValue(), HoldBlockSetting.GetValue(), FireWhileSprintingSetting.GetValue(),
            SightedFireJiggleSetting.GetValue());
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
}
