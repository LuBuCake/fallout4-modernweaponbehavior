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

#include "EquippedWeapon.h"

namespace
{
    struct SpinLockLayout
    {
        std::uint32_t          OwningThread;
        volatile std::uint32_t LockCount;
    };

    static_assert(sizeof(SpinLockLayout) == sizeof(RE::BSSpinLock));

    [[nodiscard]] bool TryLock(RE::BSSpinLock& Lock)
    {
        auto&                          Layout = reinterpret_cast<SpinLockLayout&>(Lock);
        REX::TAtomicRef<std::uint32_t> Count{ const_cast<std::uint32_t&>(Layout.LockCount) };
        const auto                     Thread = static_cast<std::uint32_t>(REX::W32::GetCurrentThreadId());

        if (Layout.OwningThread == Thread)
        {
            ++Count;
            return true;
        }

        std::uint32_t Expected{ 0 };

        if (Count.compare_exchange_strong(Expected, 1))
        {
            Layout.OwningThread = Thread;
            return true;
        }

        return false;
    }

    [[nodiscard]] RE::MiddleHighProcessData* ProcessOf(RE::Actor* Actor)
    {
        return Actor && Actor->currentProcess ? Actor->currentProcess->middleHigh : nullptr;
    }

    [[nodiscard]] bool IsThrown(const RE::TESObjectWEAP& Weapon)
    {
        const auto Type = Weapon.weaponData.type.get();

        return Type == RE::WEAPON_TYPE::kGrenade || Type == RE::WEAPON_TYPE::kMine;
    }

    [[nodiscard]] bool HasWeaponData(const RE::EquippedItem& Item)
    {
        static REL::Relocation<std::uintptr_t> WeaponDataVtbl{ RE::VTABLE::EquippedWeaponData[0] };

        return Item.data && *reinterpret_cast<const std::uintptr_t*>(Item.data.get()) == WeaponDataVtbl.address();
    }

    [[nodiscard]] std::optional<Core::EquippedWeapon> FindWeapon(RE::MiddleHighProcessData& Process, bool WithData)
    {
        for (auto& Item : Process.equippedItems)
        {
            if (!Item.item.object)
                continue;

            const bool ItemHasWeaponData = HasWeaponData(Item);

            if (WithData != ItemHasWeaponData)
                continue;

            const auto Base = Item.item.object->As<RE::TESObjectWEAP>();

            if (!Base || IsThrown(*Base))
                continue;

            auto Instance = static_cast<RE::TESObjectWEAP::InstanceData*>(Item.item.instanceData.get());

            if (!Instance)
                Instance = &Base->weaponData;

            const auto Data = ItemHasWeaponData ? static_cast<RE::EquippedWeaponData*>(Item.data.get()) : nullptr;

            return Core::EquippedWeapon{ Base, Instance, Data, &Item };
        }

        return std::nullopt;
    }
}

namespace Core
{
    std::optional<EquippedWeapon> GetEquippedWeapon(RE::Actor* Actor)
    {
        const auto Process = ProcessOf(Actor);

        return Process ? FindWeapon(*Process, true) : std::nullopt;
    }

    bool TryGetEquippedWeapon(RE::PlayerCharacter* Player, std::optional<EquippedWeapon>& Out)
    {
        const auto Process = ProcessOf(Player);

        if (!Process)
        {
            Out.reset();
            return true;
        }

        if (!TryLock(Process->equippedItemsLock))
            return false;

        Out = FindWeapon(*Process, true);

        if (!Out)
            Out = FindWeapon(*Process, false);

        Process->equippedItemsLock.unlock();

        return true;
    }

    bool TryGetThrownIsMine(RE::PlayerCharacter* Player, bool& Mine)
    {
        const auto Process = ProcessOf(Player);

        if (!Process)
        {
            Mine = false;
            return true;
        }

        if (!TryLock(Process->equippedItemsLock))
            return false;

        Mine = false;

        for (auto& Item : Process->equippedItems)
        {
            const auto Weapon = Item.item.object ? Item.item.object->As<RE::TESObjectWEAP>() : nullptr;

            if (Weapon && IsThrown(*Weapon))
            {
                Mine = Weapon->weaponData.type.get() == RE::WEAPON_TYPE::kMine;
                break;
            }
        }

        Process->equippedItemsLock.unlock();

        return true;
    }

    std::string DescribeEquippedItems(RE::PlayerCharacter* Player)
    {
        const auto Process = ProcessOf(Player);

        if (!Process || !TryLock(Process->equippedItemsLock))
            return "(not available)";

        std::string Text;

        for (auto& Item : Process->equippedItems)
        {
            const auto Object = Item.item.object;
            const auto Weapon = Object ? Object->As<RE::TESObjectWEAP>() : nullptr;

            Text += std::format("{}[{:08X} {}{}{}]", Text.empty() ? "" : " ", Object ? Object->GetFormID() : 0u,
                Weapon ? "weapon" : "other", Weapon && IsThrown(*Weapon) ? " thrown" : "",
                HasWeaponData(Item) ? " drawn" : Item.data ? " data" : "");
        }

        Process->equippedItemsLock.unlock();

        return Text.empty() ? "(none)" : Text;
    }
}
