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

#include "WeaponActions.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    using WeaponConfig::WeaponAction;

    std::vector<std::pair<RE::BSFixedString, WeaponAction>> Events;

    struct EmptyAction
    {
        RE::BSFixedString Event;
        RE::BSFixedString Empty;
        WeaponAction      Action;
    };

    std::vector<EmptyAction> EmptyActions;

    [[nodiscard]] std::string_view NameOf(WeaponAction Action)
    {
        switch (Action)
        {
            case WeaponAction::Inspect:
                return "inspect"sv;
            case WeaponAction::AmmoCheck:
                return "ammoCheck"sv;
            case WeaponAction::SpecialAttack:
                return "specialAttack"sv;
        }

        return "?"sv;
    }
}

namespace WeaponActions
{
    void Initialize()
    {
        Events.clear();
        Events.emplace_back("MWB_Inspect", WeaponAction::Inspect);
        Events.emplace_back("MWB_AmmoCheck", WeaponAction::AmmoCheck);
        Events.emplace_back("MWB_SpecialAttack", WeaponAction::SpecialAttack);
        EmptyActions.clear();
        EmptyActions.push_back({ "MWB_Inspect", "MWB_InspectEmpty", WeaponAction::Inspect });
        EmptyActions.push_back({ "MWB_AmmoCheck", "MWB_AmmoCheckEmpty", WeaponAction::AmmoCheck });
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        const auto Known = std::ranges::find(Events, Event, &std::pair<RE::BSFixedString, WeaponAction>::first);

        if (Known == Events.end())
            return false;

        const auto Weapon = Core::GetEquippedWeapon(RE::PlayerCharacter::GetSingleton());
        const bool Allowed = Weapon && WeaponConfig::MatchAction(Known->second, Weapon->Base, Weapon->Instance).has_value();

        if (!Allowed && Settings::VerboseLog())
        {
            REX::INFO("Weapon actions: {} refused, {}", Event.c_str(),
                Weapon ? std::format("the weapon {:08X} has no \"{}\" in the JSON files", Weapon->Base->GetFormID(), NameOf(Known->second)) :
                         "no drawn weapon"s);
        }

        return !Allowed;
    }

    const RE::BSFixedString* Replace(const RE::BSFixedString& Event)
    {
        const auto Known = std::ranges::find(EmptyActions, Event, &EmptyAction::Event);

        if (Known == EmptyActions.end())
            return nullptr;

        const auto Weapon = Core::GetEquippedWeapon(RE::PlayerCharacter::GetSingleton());
        const auto Rule = Weapon ? WeaponConfig::MatchAction(Known->Action, Weapon->Base, Weapon->Instance) : std::nullopt;

        if (!Rule || !Rule->Empty)
            return nullptr;

        const auto Loaded = Weapon->Data->ammoCount;

        if (Settings::VerboseLog())
            REX::INFO("Weapon actions: {} with {} loaded -> {}", Event.c_str(), Loaded, Loaded == 0 ? Known->Empty.c_str() : "unchanged");

        return Loaded == 0 ? &Known->Empty : nullptr;
    }

    void Refused(const RE::BSFixedString& Event, const RE::BSFixedString& Replacement)
    {
        if (Settings::VerboseLog())
            REX::INFO("Weapon actions: {} refused, {} sent instead", Replacement.c_str(), Event.c_str());
    }
}
