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
        ManualReload,
        Slide,
        FireLast,

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
        bool TacticalStart{ false };
        bool Tactical{ false };
        bool TacticalStop{ false };

        bool operator==(const SprintRule&) const = default;
    };

    struct FireLastRule
    {
        bool Ready{ true };
        bool Sighted{ true };

        bool operator==(const FireLastRule&) const = default;
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

        const RE::BGSSoundDescriptorForm* LockSound{ nullptr };
        const RE::BGSSoundDescriptorForm* ReleaseSound{ nullptr };
        std::vector<RE::BSFixedString>    HideNodes;
        std::vector<RE::BSFixedString>    ShowNodes;
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
    [[nodiscard]] bool                         MatchSlide(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
    [[nodiscard]] std::optional<FireLastRule>  MatchFireLast(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);
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

    [[nodiscard]] std::uint32_t Generation();

    [[nodiscard]] std::string_view FeatureKey(Feature TargetFeature);

    [[nodiscard]] std::optional<Feature> FeatureFromKey(std::string_view Key);

    enum class FeatureState
    {
        NotApplicable,
        On,
        ExcludedByPlayer,
        ExcludedByMod,
        OptionOff,
    };

    enum class SourceKind
    {
        None,
        Weapon,
        Keyword,
        FormList,
        EveryGun,
        PlayerOption,
    };

    struct FeatureReport
    {
        Feature      Which{ Feature::Count };
        FeatureState State{ FeatureState::NotApplicable };
        SourceKind   Kind{ SourceKind::None };
        std::string  File;
        std::string  Name;
    };

    [[nodiscard]] std::vector<FeatureReport> Report(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);

    [[nodiscard]] std::vector<const RE::BGSKeyword*> TargetKeywords(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);

    bool TogglePlayerExclusion(const RE::TESForm* Target, Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance);

    struct PlayerExclusion
    {
        std::string          Plugin;
        std::uint32_t        LocalID{ 0 };
        bool                 Keyword{ false };
        const RE::TESForm*   Form{ nullptr };
        std::vector<Feature> Features;
    };

    [[nodiscard]] std::vector<PlayerExclusion> PlayerExclusions();

    bool RemovePlayerExclusion(std::size_t Index, std::optional<Feature> TargetFeature);
}
