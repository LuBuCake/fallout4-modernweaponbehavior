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

#include "FullEquip.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::int32_t FullDrawState = 1;

    const RE::BSFixedString& DrawStateVariable()
    {
        static const RE::BSFixedString Variable{ "iSyncWeaponDrawState" };

        return Variable;
    }

    const RE::BSFixedString& MeleeForceEquipEvent()
    {
        static const RE::BSFixedString Event{ "g_weaponForceEquipInstant" };

        return Event;
    }

    [[nodiscard]] bool IsEquipEvent(const RE::BSFixedString& Event)
    {
        static const RE::BSFixedString Equip{ "weapEquip" };
        static const RE::BSFixedString ForceEquip{ "weapForceEquip" };

        return Event == Equip || Event == ForceEquip;
    }

    [[nodiscard]] bool ProcessingForceEquip()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const auto Process = Player ? Player->currentProcess : nullptr;

        return Process && Process->high && Process->high->processingForceEquip;
    }

    [[nodiscard]] bool IsMeleeSwitch(const RE::BSFixedString& Event)
    {
        if (Event != MeleeForceEquipEvent())
            return false;

        const bool Switch = ProcessingForceEquip();

        if (!Switch && Settings::VerboseLog())
            REX::INFO("Full equip: {} outside a weapon switch (load, menu), left as it is", Event.c_str());

        return Switch;
    }
}

namespace FullEquip
{
    std::optional<std::int32_t> Begin(RE::IAnimationGraphManagerHolder* Holder, const RE::BSFixedString& Event)
    {
        if (!Holder || !Settings::FullEquipOnSwitch() || !(IsEquipEvent(Event) || IsMeleeSwitch(Event)))
            return std::nullopt;

        std::int32_t Current = 0;

        if (!Holder->GetGraphVariableImplInt(DrawStateVariable(), Current) || Current == FullDrawState)
        {
            if (Settings::VerboseLog())
                REX::INFO("Full equip: {} left as it is (iSyncWeaponDrawState {})", Event.c_str(), Current);

            return std::nullopt;
        }

        Holder->SetGraphVariableInt(DrawStateVariable(), FullDrawState);

        if (Settings::VerboseLog())
            REX::INFO("Full equip: {} with iSyncWeaponDrawState {} -> {}", Event.c_str(), Current, FullDrawState);

        return Current;
    }

    void End(RE::IAnimationGraphManagerHolder* Holder, std::optional<std::int32_t> Previous)
    {
        if (Holder && Previous)
            Holder->SetGraphVariableInt(DrawStateVariable(), *Previous);
    }
}
