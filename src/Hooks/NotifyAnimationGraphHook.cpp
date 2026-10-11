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

#include "NotifyAnimationGraphHook.h"
#include "Actions/FirstPerson.h"
#include "Actions/WeaponActions.h"
#include "Arm/ArmAnimations.h"
#include "Arm/ArmSpace.h"
#include "Reload/BoltChargeResync.h"
#include "Reload/DropUnspentAmmo.h"
#include "Reload/TacticalReload.h"
#include "Weapon/FireMode.h"
#include "Weapon/ScopeSway.h"

namespace
{
    using Holder = RE::IAnimationGraphManagerHolder;

    constexpr std::size_t HolderVtableIndex = 5;
    constexpr std::size_t HolderOffset = 0x48;
    constexpr std::size_t NotifyAnimationGraphImplSlot = 1;

    struct NotifyAnimationGraph
    {
        static bool Thunk(Holder* This, const RE::BSFixedString& Event)
        {
            const RE::BSFixedString* SentEvent = &Event;

            if (const auto Translated = TacticalReload::Translate(Event))
            {
                if (!*Translated)
                    return false;

                SentEvent = *Translated;
            }

            if (const auto FallbackEvent = ArmAnimations::Fallback(*SentEvent))
                SentEvent = FallbackEvent;

            if (ArmAnimations::Refuse(*SentEvent))
                return false;

            if (FirstPerson::Refuse(*SentEvent))
                return false;

            if (WeaponActions::Refuse(*SentEvent))
                return false;

            if (FireMode::Refuse(*SentEvent))
                return false;

            if (ScopeSway::Refuse(*SentEvent))
                return false;

            if (const auto Replacement = WeaponActions::Replace(*SentEvent))
            {
                if (Original(This, *Replacement))
                    return true;

                WeaponActions::Refused(*SentEvent, *Replacement);
            }

            if (const auto Replacement = BoltChargeResync::Replace(*SentEvent))
            {
                if (Original(This, *Replacement))
                {
                    BoltChargeResync::Taken();
                    return true;
                }
            }

            if (const auto Replacement = TacticalReload::Replace(*SentEvent))
            {
                if (Original(This, *Replacement))
                {
                    BoltChargeResync::OnEventSent(*SentEvent, true);
                    DropUnspentAmmo::OnEventSent(*SentEvent, true);
                    return true;
                }

                TacticalReload::Refused();
            }

            const bool Accepted = Original(This, *SentEvent);
            BoltChargeResync::OnEventSent(*SentEvent, Accepted);
            DropUnspentAmmo::OnEventSent(*SentEvent, Accepted);
            ArmSpace::OnEventSent(*SentEvent, Accepted);
            FireMode::OnEventSent(*SentEvent, Accepted);

            return Accepted;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };
}

namespace Hooks
{
    void InstallNotifyAnimationGraphHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[HolderVtableIndex] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(Player) + HolderOffset);

            if (Live != Vtable.address())
            {
                REX::ERROR("Animation graph holder vtable mismatch (id {:X}, live {:X}). Tactical reload disabled.", Vtable.address(), Live);
                return;
            }
        }

        NotifyAnimationGraph::Original = Vtable.write_vfunc(NotifyAnimationGraphImplSlot, NotifyAnimationGraph::Thunk);
        REX::INFO("Animation graph notify hook installed");
    }
}
