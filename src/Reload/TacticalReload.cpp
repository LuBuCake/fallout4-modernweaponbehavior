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

#include "TacticalReload.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    std::optional<RE::BSFixedString> ReloadStart;
    std::optional<RE::BSFixedString> ReserveStart;
    std::optional<RE::BSFixedString> StartVariable;
    std::optional<RE::BSFixedString> AttackStart;
    std::optional<RE::BSFixedString> AttackStartAuto;
    std::optional<RE::BSFixedString> WeapEquip;
    std::optional<RE::BSFixedString> InjuredDownReloadStart;

    std::optional<RE::BSFixedString> TRReloadReserveStart;
    std::optional<RE::BSFixedString> AttackLastStart;
    std::optional<RE::BSFixedString> ReloadJamStart;
    std::optional<RE::BSFixedString> WeapEquipEmpty;
    std::optional<RE::BSFixedString> InjuredDownReloadReserveStart;
    std::optional<RE::BSFixedString> FireDry;
    std::optional<RE::BSFixedString> SelectionChangeStart;
    std::optional<RE::BSFixedString> SelectionChangeBackStart;

    constexpr std::int32_t ReloadState3rd = 3;
    constexpr std::int32_t ReserveState3rd = 7;

    std::atomic<bool> WarnedRefused{ false };
    std::atomic<bool> WarnedVariable{ false };

    [[nodiscard]] bool IsAutomatic(const Core::EquippedWeapon& Weapon)
    {
        const auto Data = Weapon.Instance ? Weapon.Instance : &Weapon.Base->weaponData;

        return Data->flags.all(RE::WEAPON_FLAGS::kAutomatic);
    }

    void SetStartState(RE::PlayerCharacter* Player, std::int32_t State)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(*StartVariable, Current))
        {
            if (State != ReloadState3rd && !WarnedVariable.exchange(true))
            {
                REX::WARN("Tactical reload: the third person animation graph does not know iMWB_ReloadStartState (another "
                          "mod's root or weapon wrapping wins over Modern Weapon Behavior's); the reload plays as usual. "
                          "Logged once.");
            }

            return;
        }

        if (Current != State)
            Player->SetGraphVariableInt(*StartVariable, State);
    }
}

namespace TacticalReload
{
    void Initialize()
    {
        ReloadStart.emplace("reloadStart");
        ReserveStart.emplace("MWB_ReloadReserveStart");
        StartVariable.emplace("iMWB_ReloadStartState");
        AttackStart.emplace("attackStart");
        AttackStartAuto.emplace("attackStartAuto");
        WeapEquip.emplace("WeapEquip");
        InjuredDownReloadStart.emplace("InjuredDownReloadStart");
        TRReloadReserveStart.emplace("reloadReserveStart");
        AttackLastStart.emplace("attackLastStart");
        ReloadJamStart.emplace("reloadJamStart");
        WeapEquipEmpty.emplace("weapEquipEmpty");
        InjuredDownReloadReserveStart.emplace("InjuredDownReloadReserveStart");
        FireDry.emplace("fireDry");
        SelectionChangeStart.emplace("selectionChangeStart");
        SelectionChangeBackStart.emplace("selectionChangeBackStart");
    }

    std::optional<const RE::BSFixedString*> Translate(const RE::BSFixedString& Event)
    {
        if (!AttackLastStart)
            return std::nullopt;

        const RE::BSFixedString* Translated = nullptr;

        if (Event == *AttackLastStart)
        {
            const auto Weapon = Core::GetEquippedWeapon(RE::PlayerCharacter::GetSingleton());
            Translated = Weapon && IsAutomatic(*Weapon) ? &*AttackStartAuto : &*AttackStart;
        }
        else if (Event == *TRReloadReserveStart || Event == *ReloadJamStart)
        {
            Translated = &*ReloadStart;
        }
        else if (Event == *WeapEquipEmpty)
        {
            Translated = &*WeapEquip;
        }
        else if (Event == *InjuredDownReloadReserveStart)
        {
            Translated = &*InjuredDownReloadStart;
        }
        else if (Event == *FireDry || Event == *SelectionChangeStart || Event == *SelectionChangeBackStart)
        {
            Translated = nullptr;
        }
        else
        {
            return std::nullopt;
        }

        if (Settings::VerboseLog())
            REX::INFO("Tactical reload: {} (Tactical Reload's) -> {}", Event.c_str(), Translated ? Translated->c_str() : "dropped");

        return Translated;
    }

    const RE::BSFixedString* Replace(const RE::BSFixedString& Event)
    {
        if (!ReloadStart)
            return nullptr;

        const bool InjuredDown = Event == *InjuredDownReloadStart;

        if (!InjuredDown && Event != *ReloadStart)
            return nullptr;

        const auto Player = RE::PlayerCharacter::GetSingleton();
        const bool ThirdPerson = Player && Player->Is3rdPersonVisible();

        if (InjuredDown && !ThirdPerson)
            return nullptr;

        const auto Weapon = Player ? Core::GetEquippedWeapon(Player) : std::nullopt;
        const auto Rule = Weapon ? WeaponConfig::MatchTactical(Weapon->Base, Weapon->Instance) : std::nullopt;

        const auto Loaded = Weapon ? Weapon->Data->ammoCount : 0;
        const bool Reserve = Rule && Loaded > 0 && (ThirdPerson ? Rule->ThirdPerson : Rule->FirstPerson);

        if (Rule && Settings::VerboseLog())
        {
            REX::INFO("Tactical reload: {} with {} loaded, {} person ({}) -> {}", Event.c_str(), Loaded, ThirdPerson ? "third" : "first",
                ThirdPerson ? (Rule->ThirdPerson ? "on" : "off") : (Rule->FirstPerson ? "on" : "off"), Reserve ? "reserve reload" : "unchanged");
        }

        if (ThirdPerson)
        {
            if (Player)
                SetStartState(Player, Reserve ? ReserveState3rd : ReloadState3rd);

            return nullptr;
        }

        return Reserve ? &*ReserveStart : nullptr;
    }

    void Refused()
    {
        if (!WarnedRefused.exchange(true))
        {
            REX::WARN("Tactical reload: the animation graph refused MWB_ReloadReserveStart (its root or weapon wrapping does not "
                      "declare it), reloadStart was sent instead. Logged once.");
        }
        else if (Settings::VerboseLog())
        {
            REX::INFO("Tactical reload: MWB_ReloadReserveStart refused, reloadStart sent instead");
        }
    }
}
