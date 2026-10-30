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
#include "Weapon/FireLast.h"
#include "Settings/Settings.h"
#include "Weapon/WeaponBones.h"

namespace
{
    constexpr auto SlideLockLayer = WeaponBones::Layer::SlideLock;

    bool Seen{ false };
    RE::TESObjectWEAP* LastBase{ nullptr };
    RE::TESObjectWEAP::InstanceData* LastInstance{ nullptr };
    std::uint32_t LastGeneration{ 0 };
    std::optional<WeaponConfig::SlideLockRule> Rule;
    float Progress{ 0.0f };
    bool WasLocked{ false };
    bool WasHeld{ false };

    std::atomic<bool> ReloadActive{ false };

    constexpr std::uint32_t SoundUsageFlags = 0x10;
    constexpr int           MaxNodeDepth = 64;

    std::vector<RE::NiPointer<RE::NiAVObject>> CulledNodes;

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view TagView{ Tag.c_str() };

        return TagView.size() == Name.size() && _strnicmp(TagView.data(), Name.data(), TagView.size()) == 0;
    }

    void PlaySound(RE::PlayerCharacter* Player, const RE::BGSSoundDescriptorForm* Sound, bool Locked)
    {
        if (!Sound)
            return;

        const auto Audio = RE::BSAudioManager::GetSingleton();
        RE::BSSoundHandle Handle;

        if (!Audio || !Audio->GetSoundHandle(Handle, Sound, 0.0f, SoundUsageFlags) || !Handle.IsValid())
        {
            REX::WARN("Slide lock: the {} sound {:08X} could not be played", Locked ? "lock" : "release", Sound->GetFormID());
            return;
        }

        Handle.SetPosition(Player->GetPosition());
        const bool Played = Handle.Play();

        if (Settings::VerboseLog())
            REX::INFO("Slide lock: {} sound {:08X} {}", Locked ? "lock" : "release", Sound->GetFormID(), Played ? "played" : "refused");
    }

    void CullMatching(RE::NiAVObject* Object, const std::vector<RE::BSFixedString>& Names, int Depth)
    {
        if (!Object || Depth > MaxNodeDepth)
            return;

        if (!Object->GetAppCulled() && std::ranges::find(Names, Object->name) != Names.end())
        {
            Object->SetAppCulled(true);
            CulledNodes.emplace_back(Object);

            if (Settings::VerboseLog())
                REX::INFO("Slide lock: '{}' hidden", Object->name.c_str());
        }

        if (const auto Node = Object->IsNode())
        {
            for (auto& Child : Node->children)
                CullMatching(Child.get(), Names, Depth + 1);
        }
    }

    void UpdateCulledNodes(RE::PlayerCharacter* Player, const std::vector<RE::BSFixedString>& Hidden)
    {
        std::erase_if(CulledNodes, [&](const RE::NiPointer<RE::NiAVObject>& Object)
        {
            if (std::ranges::find(Hidden, Object->name) != Hidden.end())
                return false;

            Object->SetAppCulled(false);

            if (Settings::VerboseLog())
                REX::INFO("Slide lock: '{}' shown", Object->name.c_str());

            return true;
        });

        if (Hidden.empty() || !Player)
            return;

        CullMatching(Player->firstPerson3D.get(), Hidden, 0);
        CullMatching(Player->Get3D(false), Hidden, 0);
    }

    void ShowCulledNodes()
    {
        for (const auto& Object : CulledNodes)
            Object->SetAppCulled(false);

        CulledNodes.clear();
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
        const bool WeaponChanged = !Seen || Base != LastBase || Instance != LastInstance;
        const bool Changed = WeaponChanged || LastGeneration != WeaponConfig::Generation();

        if (Changed)
        {
            const bool Had = Rule.has_value();
            Seen = true;
            LastBase = Base;
            LastInstance = Instance;
            LastGeneration = WeaponConfig::Generation();
            Rule = Base ? WeaponConfig::MatchSlideLock(Base, Instance) : std::nullopt;

            if (WeaponChanged)
                ReloadActive = false;

            if (Rule)
                REX::INFO("Slide lock on for {:08X}", Base->GetFormID());
            else if (Had)
                REX::INFO("Slide lock off");

            WeaponBones::SetBones(SlideLockLayer, Rule ? Rule->Bones : std::vector<WeaponConfig::BoneOffset>{}, Base ? Base->GetFormID() : 0u);
        }

        const bool Empty = Weapon && Weapon->Data && Weapon->Data->ammoCount == 0 && !DropUnspentAmmo::MagazineDropped() && !FireLast::Playing();
        const bool Held = Rule && Empty;
        const bool Locked = Held && !(Rule->DisableInEmptyReloads && ReloadActive.load(std::memory_order_relaxed));

        if (Held != WasHeld)
        {
            WasHeld = Held;

            if (!Changed && Rule)
                PlaySound(Player, Held ? Rule->LockSound : Rule->ReleaseSound, Held);
        }

        if (Locked != WasLocked)
        {
            WasLocked = Locked;

            if (Settings::VerboseLog())
                REX::INFO("Slide lock: {}", Locked ? "held back (magazine empty)" : Held ? "bones released for the reload" : "released");
        }

        static const std::vector<RE::BSFixedString> NoNodes;
        UpdateCulledNodes(Player, !Rule ? NoNodes : Held ? Rule->HideNodes : Rule->ShowNodes);

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
        ShowCulledNodes();
        WeaponBones::SetBones(SlideLockLayer, {}, 0);
        WeaponBones::SetWeight(SlideLockLayer, 0.0f);
        Seen = false;
        LastBase = nullptr;
        LastInstance = nullptr;
        Rule.reset();
        Progress = 0.0f;
        WasLocked = false;
        WasHeld = false;
        ReloadActive = false;
    }
}
