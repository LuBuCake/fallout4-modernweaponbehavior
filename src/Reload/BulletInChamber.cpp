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

#include "BulletInChamber.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t ReloadWeaponIndex = 0xEF;

    constexpr REL::ID     CanReloadCallerID{ 1231976, 2231475 };
    constexpr REL::ID     CanReloadID{ 1089596, 2231479 };
    constexpr std::size_t ScanSize = 0x800;

    constexpr std::uint8_t CallOpcode = 0xE8;
    constexpr std::size_t  CallInstructionSize = 5;

    struct CanReloadState
    {
        bool*       CanReload;
        RE::Actor** Actor;
    };

    struct Decision
    {
        const RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
        std::uint32_t                          CountAfter{ 0 };
        bool                                   ExtraRound{ false };
    };

    std::mutex              Lock;
    std::optional<Decision> LastDecision;

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] RE::TESAmmo* AmmoOf(const RE::TESObjectWEAP::InstanceData& Instance, const RE::TESObjectWEAP& Base)
    {
        return Instance.ammo ? Instance.ammo : Base.weaponData.ammo;
    }

    [[nodiscard]] bool Eligible(RE::TESObjectWEAP* Base, RE::TESObjectWEAP::InstanceData* Instance)
    {
        if (!Base || !Instance || Base->weaponData.type.get() != RE::WEAPON_TYPE::kGun)
            return false;

        if (Instance->ammoCapacity < 2 || Instance->ammoCapacity == (std::numeric_limits<std::uint16_t>::max)() ||
            Instance->flags.any(RE::WEAPON_FLAGS::kChargingReload))
        {
            return false;
        }

        const auto Ammo = AmmoOf(*Instance, *Base);

        if (!Ammo || Ammo->data.health != 0 || RE::TESAmmo::GetReloadsWithAmmoRef(Ammo))
            return false;

        if (Settings::BulletInChamberTacticalOnly())
        {
            const auto Tactical = WeaponConfig::MatchTactical(Base, Instance);

            if (!Tactical || !Tactical->FirstPerson)
                return false;
        }

        return !WeaponConfig::MatchLoop(Base, Instance) && !WeaponConfig::Excluded(WeaponConfig::Feature::BulletInChamber, Base, Instance);
    }

    struct ReloadWeapon
    {
        static bool Thunk(RE::PlayerCharacter* Player, const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& Weapon, std::uint32_t EquipIndexAsUInt32)
        {
            if (!Settings::BulletInChamber())
                return Original(Player, Weapon, EquipIndexAsUInt32);

            const auto Equipped = Core::GetEquippedWeapon(Player);

            if (!Equipped || Equipped->Base != Weapon.object || !Eligible(Equipped->Base, Equipped->Instance))
                return Original(Player, Weapon, EquipIndexAsUInt32);

            const std::scoped_lock Guard{ Lock };

            const auto Loaded = Equipped->Data->ammoCount;
            const bool SameReload = LastDecision && LastDecision->Instance == Equipped->Instance && LastDecision->CountAfter == Loaded;
            const bool ExtraRound = SameReload ? LastDecision->ExtraRound : Loaded > 0;

            const auto Capacity = Equipped->Instance->ammoCapacity;

            if (ExtraRound)
                Equipped->Instance->ammoCapacity = static_cast<std::uint16_t>(Capacity + 1);

            const auto Result = Original(Player, Weapon, EquipIndexAsUInt32);
            Equipped->Instance->ammoCapacity = Capacity;

            const auto After = Core::GetEquippedWeapon(Player);
            const auto CountAfter = After && After->Data ? After->Data->ammoCount : 0;
            LastDecision = Decision{ Equipped->Instance, CountAfter, ExtraRound };

            if (Settings::VerboseLog())
            {
                REX::INFO("Bullet in the chamber: {} -> {} of {}{}{}", Loaded, CountAfter, Capacity,
                    ExtraRound ? " (+1 chambered)" : "", SameReload ? " (same reload)" : "");
            }

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct CanReload
    {
        static bool Thunk(CanReloadState* State, RE::EquippedItem* Item)
        {
            const auto VisitNext = Original(State, Item);

            if (!Settings::BulletInChamber() || !State || !Item || *State->CanReload)
                return VisitNext;

            const auto Player = RE::PlayerCharacter::GetSingleton();

            if (!State->Actor || *State->Actor != Player || !Item->item.object || !Item->data)
                return VisitNext;

            const auto Base = Item->item.object->As<RE::TESObjectWEAP>();

            if (!Base)
                return VisitNext;

            auto Instance = static_cast<RE::TESObjectWEAP::InstanceData*>(Item->item.instanceData.get());

            if (!Instance)
                Instance = &Base->weaponData;

            if (!Eligible(Base, Instance))
                return VisitNext;

            const auto Data = static_cast<RE::EquippedWeaponData*>(Item->data.get());

            if (Data->ammoCount != Instance->ammoCapacity)
                return VisitNext;

            std::uint32_t OwnedIncludingLoaded = 0;
            Player->GetItemCount(OwnedIncludingLoaded, AmmoOf(*Instance, *Base), false);

            if (OwnedIncludingLoaded <= Data->ammoCount)
                return VisitNext;

            *State->CanReload = true;

            return false;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCall(std::uintptr_t Begin, std::uintptr_t Target)
    {
        std::optional<std::uintptr_t> Found;

        for (std::size_t i = 0; i + CallInstructionSize <= ScanSize; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != CallOpcode)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(At + 1);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + CallInstructionSize) + Displacement) != Target)
                continue;

            if (Found)
                return std::nullopt;

            Found = At;
        }

        return Found;
    }
}

namespace BulletInChamber
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[0] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(Player);

            if (Live != Vtable.address())
            {
                REX::ERROR("PlayerCharacter vtable mismatch (id {:X}, live {:X}). Bullet in the chamber disabled.", Vtable.address(), Live);
                return;
            }
        }

        const auto Caller = REL::Relocation<std::uintptr_t>{ CanReloadCallerID }.address();
        const auto Target = REL::Relocation<std::uintptr_t>{ CanReloadID }.address();
        const auto Site = FindCall(Caller, Target);

        if (!Site)
        {
            REX::WARN("The reload check was not found (another mod may have it). Bullet in the chamber disabled.");
            return;
        }

        ReloadWeapon::Original = Vtable.write_vfunc(ReloadWeaponIndex, ReloadWeapon::Thunk);
        CanReload::Original = REL::GetTrampoline().write_call<5>(*Site, CanReload::Thunk);
        REX::INFO("Bullet in the chamber installed (reload check at {:X}, chaining to {:X})", *Site, CanReload::Original.address());
    }

    bool Applies(RE::TESObjectWEAP* Base, RE::TESObjectWEAP::InstanceData* Instance)
    {
        return Settings::BulletInChamber() && Eligible(Base, Instance);
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (Tag.empty() || !(TagIs(Tag, "reloadStateEnter"sv) || TagIs(Tag, "reloadStateExit"sv) || TagIs(Tag, "reloadEnd"sv)))
            return;

        const std::scoped_lock Guard{ Lock };
        LastDecision.reset();
    }

    void Reset()
    {
        const std::scoped_lock Guard{ Lock };
        LastDecision.reset();
    }
}
