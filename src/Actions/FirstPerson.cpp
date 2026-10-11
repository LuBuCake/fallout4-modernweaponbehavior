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

#include "FirstPerson.h"
#include "Core/EquippedWeapon.h"

namespace
{
    std::vector<RE::BSFixedString> Events;
}

namespace FirstPerson
{
    void Initialize()
    {
        Events.clear();

        for (const auto Name : { "MWB_Inspect"sv, "MWB_AmmoCheck"sv, "MWB_SpecialAttack"sv, "MWB_SwitchFireModeStart"sv })
            Events.emplace_back(Name);
    }

    std::optional<std::string> WeaponBusy(RE::PlayerCharacter* Player)
    {
        if (!Core::GetEquippedWeapon(Player))
            return "no drawn weapon"s;

        switch (Player->gunState)
        {
            case RE::GUN_STATE::kReloading:
                return "reloading"s;
            case RE::GUN_STATE::kThrowing:
                return "throwing"s;
            case RE::GUN_STATE::kSighted:
            case RE::GUN_STATE::kFireSighted:
                return "aiming"s;
            case RE::GUN_STATE::kFire:
                return "firing"s;
            default:
                break;
        }

        if (Player->meleeAttackState != 0)
            return "in a melee attack"s;

        return std::nullopt;
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (std::ranges::find(Events, Event) == Events.end())
            return false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !Player->Is3rdPersonVisible())
            return false;

        REX::INFO("First person: {} refused, third person", Event.c_str());

        return true;
    }
}
