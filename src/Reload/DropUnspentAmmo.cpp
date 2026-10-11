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

#include "DropUnspentAmmo.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Reload/BulletInChamber.h"
#include "Settings/Settings.h"

namespace
{
    std::atomic<bool> Pending{ false };
    std::atomic<bool> Dropped{ false };

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

        if (Instance->ammoCapacity < 2 || Instance->flags.any(RE::WEAPON_FLAGS::kChargingReload))
            return false;

        const auto Ammo = AmmoOf(*Instance, *Base);

        if (!Ammo || Ammo->data.health != 0 || RE::TESAmmo::GetReloadsWithAmmoRef(Ammo))
            return false;

        return !WeaponConfig::MatchLoop(Base, Instance) && !WeaponConfig::Excluded(WeaponConfig::Feature::DropUnspentAmmo, Base, Instance);
    }

    void Drop(RE::PlayerCharacter* Player)
    {
        const auto Weapon = Core::GetEquippedWeapon(Player);

        if (!Weapon || !Weapon->Data || !Eligible(Weapon->Base, Weapon->Instance))
            return;

        const auto Loaded = Weapon->Data->ammoCount;
        const std::uint32_t Kept = BulletInChamber::Applies(Weapon->Base, Weapon->Instance) ? 1 : 0;

        if (Loaded <= Kept)
            return;

        const auto Ammo = AmmoOf(*Weapon->Instance, *Weapon->Base);
        std::uint32_t Owned = 0;
        Player->GetItemCount(Owned, Ammo, false);
        const auto Count = (std::min)(Loaded - Kept, Owned);

        if (Count == 0)
            return;

        Weapon->Data->ammoCount = Loaded - Count;
        Dropped = Weapon->Data->ammoCount == 0;
        const RE::BGSObjectInstance Object{ Ammo, nullptr };
        Player->DropObject(Object, nullptr, static_cast<std::int32_t>(Count), nullptr, nullptr);

        if (Settings::VerboseLog())
        {
            REX::INFO("Drop unspent ammo: {} of {:08X} dropped from weapon {:08X}{}", Count, Ammo->GetFormID(), Weapon->Base->GetFormID(),
                Kept ? " (the chambered round stays)" : "");
        }
    }
}

namespace DropUnspentAmmo
{
    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        if (Accepted && TagIs(Event, "reloadStart"sv) && Settings::DropUnspentAmmo())
            Pending = true;
    }

    void Tick(RE::PlayerCharacter* Player)
    {
        if (Pending.exchange(false) && Player && Settings::DropUnspentAmmo())
            Drop(Player);
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (!Tag.empty() && (TagIs(Tag, "reloadStateExit"sv) || TagIs(Tag, "reloadEnd"sv)))
            Dropped = false;
    }

    bool MagazineDropped()
    {
        return Dropped.load(std::memory_order_relaxed);
    }

    void Reset()
    {
        Pending = false;
        Dropped = false;
    }
}
