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


#include "SprintSpeeds.h"
#include "Sprint/TacticalSprint.h"

namespace
{
    constexpr std::size_t MaxSpeedsSlot = 0x0A;
    constexpr std::ptrdiff_t MovementStateOffset = 0x128;

    struct GetCurrentMaxSpeeds
    {
        static bool Thunk(const RE::IMovementState* This, RE::Movement::MaxSpeeds& Speeds)
        {
            const bool Result = Original(This, Speeds);

            if (Result)
                TacticalSprint::ScaleSpeeds(Speeds);

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };
}

namespace SprintSpeeds
{
    void Install()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
        {
            REX::ERROR("Sprint speeds: no player. The tactical sprint keeps the vanilla speed.");
            return;
        }

        const auto Live = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(Player) + MovementStateOffset);

        for (const auto& ID : RE::VTABLE::PlayerCharacter)
        {
            REL::Relocation<std::uintptr_t> Vtable{ ID };

            if (Vtable.address() == Live)
            {
                GetCurrentMaxSpeeds::Original = Vtable.write_vfunc(MaxSpeedsSlot, GetCurrentMaxSpeeds::Thunk);
                REX::INFO("Sprint speed hook installed");
                return;
            }
        }

        REX::ERROR("Sprint speeds: the player's movement state vtable ({:X}) is not a PlayerCharacter one. The tactical sprint keeps the vanilla speed.", Live);
    }
}
