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

#include "SlideLock.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Reload/DropUnspentAmmo.h"
#include "Settings/Settings.h"
#include "Weapon/WeaponBones.h"

namespace
{
    constexpr auto SlideLockLayer = WeaponBones::Layer::SlideLock;

    bool Seen{ false };
    RE::TESObjectWEAP* LastBase{ nullptr };
    RE::TESObjectWEAP::InstanceData* LastInstance{ nullptr };
    std::optional<WeaponConfig::SlideLockRule> Rule;
    float Progress{ 0.0f };
    bool WasLocked{ false };

    std::atomic<bool> ReloadActive{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view TagView{ Tag.c_str() };

        return TagView.size() == Name.size() && _strnicmp(TagView.data(), Name.data(), TagView.size()) == 0;
    }
}

namespace SlideLock
{
    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        if (!Settings::SlideLock())
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
        const bool Changed = !Seen || Base != LastBase || Instance != LastInstance;

        if (Changed)
        {
            const bool Had = Rule.has_value();
            Seen = true;
            LastBase = Base;
            LastInstance = Instance;
            Rule = Base ? WeaponConfig::MatchSlideLock(Base, Instance) : std::nullopt;
            ReloadActive = false;

            if (Rule)
                REX::INFO("Slide lock on for {:08X}", Base->GetFormID());
            else if (Had)
                REX::INFO("Slide lock off");

            WeaponBones::SetBones(SlideLockLayer, Rule ? Rule->Bones : std::vector<WeaponConfig::BoneOffset>{}, Base ? Base->GetFormID() : 0u);
        }

        const bool Empty = Weapon && Weapon->Data && Weapon->Data->ammoCount == 0 && !DropUnspentAmmo::MagazineDropped();
        const bool Locked = Rule && Empty && !(Rule->DisableInEmptyReloads && ReloadActive.load(std::memory_order_relaxed));

        if (Locked != WasLocked)
        {
            WasLocked = Locked;

            if (Settings::VerboseLog())
                REX::INFO("Slide lock: {}", Locked ? "held back (magazine empty)" : "released");
        }

        const float Target = Locked ? 1.0f : 0.0f;
        const float Seconds = !Rule ? 0.0f : Locked ? Rule->BlendIn : Rule->BlendOut;

        if (Changed || Seconds <= 0.0f)
            Progress = Target;
        else if (Progress < Target)
            Progress = (std::min)(Target, Progress + Delta / Seconds);
        else if (Progress > Target)
            Progress = (std::max)(Target, Progress - Delta / Seconds);

        WeaponBones::SetWeight(SlideLockLayer, WeaponBones::Smooth(Progress));
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (Tag.empty())
            return;

        const bool Enter = TagIs(Tag, "reloadStateEnter"sv);

        if (Enter || TagIs(Tag, "reloadStateExit"sv))
            ReloadActive.store(Enter, std::memory_order_relaxed);
    }

    void Reset()
    {
        WeaponBones::SetBones(SlideLockLayer, {}, 0);
        WeaponBones::SetWeight(SlideLockLayer, 0.0f);
        Seen = false;
        LastBase = nullptr;
        LastInstance = nullptr;
        Rule.reset();
        Progress = 0.0f;
        WasLocked = false;
        ReloadActive = false;
    }
}
