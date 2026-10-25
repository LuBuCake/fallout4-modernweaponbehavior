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

#include "FireModePose.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"
#include "Weapon/WeaponBones.h"

namespace
{
    using WeaponBones::Layer;

    bool Seen{ false };
    RE::TESObjectWEAP* LastBase{ nullptr };
    RE::TESObjectWEAP::InstanceData* LastInstance{ nullptr };
    std::uint32_t LastGeneration{ 0 };
    std::optional<WeaponConfig::FireModeRule> Rule;
    bool WasAutomatic{ false };
    float Progress{ 0.0f };
}

namespace FireModePose
{
    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        if (!Settings::FireModeSwitch())
        {
            if (Seen)
                Reset();

            return;
        }

        std::optional<Core::EquippedWeapon> Weapon;

        if (!Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        const auto Base = Weapon ? Weapon->Base : nullptr;
        const auto Instance = Weapon ? Weapon->Instance : nullptr;
        const bool Changed = !Seen || Base != LastBase || Instance != LastInstance || LastGeneration != WeaponConfig::Generation();

        if (Changed)
        {
            Seen = true;
            LastBase = Base;
            LastInstance = Instance;
            LastGeneration = WeaponConfig::Generation();
            Rule = Base ? WeaponConfig::MatchFireMode(Base, Instance) : std::nullopt;
            const bool Posed = Rule && (!Rule->AutoBones.empty() || !Rule->SingleBones.empty());

            if (!Posed)
                Rule.reset();

            const auto ID = Base ? Base->GetFormID() : 0u;
            WeaponBones::SetBones(Layer::FireModeAuto, Rule ? Rule->AutoBones : std::vector<WeaponConfig::BoneOffset>{}, ID);
            WeaponBones::SetBones(Layer::FireModeSingle, Rule ? Rule->SingleBones : std::vector<WeaponConfig::BoneOffset>{}, ID);

            if (Rule && Settings::VerboseLog())
                REX::INFO("Fire mode pose on for {:08X}", ID);
        }

        const bool Automatic = Instance && Instance->flags.all(RE::WEAPON_FLAGS::kAutomatic);

        if (Rule && Automatic != WasAutomatic && !Changed && Settings::VerboseLog())
            REX::INFO("Fire mode pose: to {}", Automatic ? "automatic" : "semi-automatic");

        WasAutomatic = Automatic;

        const float Target = Automatic ? 1.0f : 0.0f;
        const float Seconds = Rule ? Rule->Blend : 0.0f;

        if (Changed || Seconds <= 0.0f)
            Progress = Target;
        else if (Progress < Target)
            Progress = (std::min)(Target, Progress + Delta / Seconds);
        else if (Progress > Target)
            Progress = (std::max)(Target, Progress - Delta / Seconds);

        const float Weight = WeaponBones::Smooth(Progress);
        WeaponBones::SetWeight(Layer::FireModeAuto, Rule ? Weight : 0.0f);
        WeaponBones::SetWeight(Layer::FireModeSingle, Rule ? 1.0f - Weight : 0.0f);
    }

    void Reset()
    {
        for (const auto PoseLayer : { Layer::FireModeAuto, Layer::FireModeSingle })
        {
            WeaponBones::SetBones(PoseLayer, {}, 0);
            WeaponBones::SetWeight(PoseLayer, 0.0f);
        }

        Seen = false;
        LastBase = nullptr;
        LastInstance = nullptr;
        Rule.reset();
        WasAutomatic = false;
        Progress = 0.0f;
    }
}
