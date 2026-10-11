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

namespace Settings
{
    void Load();

    [[nodiscard]] std::vector<std::string_view> Reload();

    void InstallMenuWatcher();

    [[nodiscard]] bool VerboseLog();

    [[nodiscard]] bool BulletInChamber();

    [[nodiscard]] bool DropUnspentAmmo();

    [[nodiscard]] bool ManualReload();

    [[nodiscard]] bool SlideLock();

    [[nodiscard]] bool FireModeSwitch();

    [[nodiscard]] bool WeaponHUD();

    [[nodiscard]] bool ThirdPersonSprintFOV();

    [[nodiscard]] bool AimWobbleVanillaScopes();

    [[nodiscard]] bool AimWobble3DScopes();

    [[nodiscard]] bool AimWobbleNoScope();

    [[nodiscard]] bool HoldBlock();

    [[nodiscard]] bool FireWhileSprinting();
}
