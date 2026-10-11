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

#include "Config/WeaponConfig.h"

namespace WeaponBones
{
    enum class Layer
    {
        SlideLock,
        FireModeAuto,
        FireModeSingle,

        Count
    };

    [[nodiscard]] float Smooth(float X);

    void BeginFrame(RE::PlayerCharacter* Player);
    void EndFrame();

    void SetBones(Layer TargetLayer, const std::vector<WeaponConfig::BoneOffset>& Bones, std::uint32_t WeaponID);
    void SetWeight(Layer TargetLayer, float Weight);

    void OnGenerate(const void* Character, void* Output);

    void Reset();
}
