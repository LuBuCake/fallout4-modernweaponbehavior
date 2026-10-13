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

namespace WeaponConfig
{
    enum class Feature
    {
        LoopedReload,
        SprintTransitions,
        SightedReload,
        TacticalReload,
        Inspect,
        AmmoCheck,
        SpecialAttack,
        SlideLock,
        FireModeSwitch,
        BulletInChamber,
        DropUnspentAmmo,
        AimDownSight,

        Count
    };

    struct LoopRule
    {
        std::optional<float> Delay;
    };

    struct SprintRule
    {
        bool Start{ true };
        bool Stop{ true };
        bool Equip{ false };

        bool operator==(const SprintRule&) const = default;
    };

    struct SightedRule
    {
        bool operator==(const SightedRule&) const = default;
    };

    struct TacticalRule
    {
        bool FirstPerson{ true };
        bool ThirdPerson{ false };

        bool operator==(const TacticalRule&) const = default;
    };

    struct ActionRule
    {
        bool              Empty{ false };
        RE::BSFixedString Bone;
    };

    struct BoneOffset
    {
        RE::BSFixedString    Name;
        std::array<float, 3> Offset{};
        std::array<float, 3> Rotation{};
    };

    struct SlideLockRule
    {
        std::vector<BoneOffset> Bones;
        bool                    DisableInEmptyReloads{ false };
        float                   BlendIn{ 0.0f };
        float                   BlendOut{ 0.0f };
    };

    struct FireModeRule
    {
        std::vector<BoneOffset> AutoBones;
        std::vector<BoneOffset> SingleBones;
        float                   Blend{ 0.0f };
    };

    void Load();

    [[nodiscard]] RE::TESForm* LookupPluginForm(std::string_view Plugin, std::uint32_t FormID);

    [[nodiscard]] bool Excluded(Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);

    [[nodiscard]] std::optional<LoopRule>      MatchLoop(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] std::optional<SprintRule>    MatchSprint(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] bool                         MatchSighted(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] bool                         MatchAimDownSight(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] std::optional<TacticalRule>  MatchTactical(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] std::optional<FireModeRule>  MatchFireMode(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] std::optional<SlideLockRule> MatchSlideLock(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);

    enum class WeaponAction
    {
        Inspect,
        AmmoCheck,
        SpecialAttack,
    };

    [[nodiscard]] std::optional<ActionRule> MatchAction(WeaponAction Action, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
}
