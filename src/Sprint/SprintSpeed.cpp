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

#include "SprintSpeed.h"
#include "Melee/HoldBlock.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t MaxSpeedsSlot = 0x0A;
    constexpr std::ptrdiff_t MovementStateOffset = 0x128;

    constexpr std::size_t TranslationRows = 4;
    constexpr std::size_t Columns = 4;

    std::atomic<float> Scale{ 1.0f };

    struct GetCurrentMaxSpeeds
    {
        static bool Thunk(const RE::IMovementState* This, RE::Movement::MaxSpeeds& Speeds)
        {
            const bool Result = Original(This, Speeds);
            const float CurrentScale = Scale.load(std::memory_order_relaxed) * HoldBlock::SpeedScale();

            if (Result && CurrentScale != 1.0f)
            {
                for (std::size_t Row = 0; Row < TranslationRows; ++Row)
                {
                    for (std::size_t Column = 0; Column < Columns; ++Column)
                        Speeds.speeds[Row][Column] *= CurrentScale;
                }
            }

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };
}

namespace SprintSpeed
{
    void Install()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
        {
            REX::ERROR("Sprint speed: no player. fLocomotionSprintPlaybackSpeed works in third person only.");
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

        REX::ERROR("Sprint speed: the player's movement state vtable ({:X}) is not a PlayerCharacter one. fLocomotionSprintPlaybackSpeed works in third person only.", Live);
    }

    void Tick(RE::PlayerCharacter* Player)
    {
        if (!GetCurrentMaxSpeeds::Original.address())
            return;

        static const RE::BSFixedString SpeedVariable{ "fLocomotionSprintPlaybackSpeed" };
        static const RE::BSFixedString SyncSprintVariable{ "iSyncSprintState" };

        float NewScale = 1.0f;

        if (Player && !Player->Is3rdPersonVisible())
        {
            std::int32_t Sprinting = 0;
            float Speed = 1.0f;

            if (Player->GetGraphVariableImplInt(SyncSprintVariable, Sprinting) && Sprinting == 1 &&
                Player->GetGraphVariableImplFloat(SpeedVariable, Speed) && Speed >= 0.0f)
            {
                NewScale = Speed;
            }
        }

        const float Previous = Scale.exchange(NewScale, std::memory_order_relaxed);

        if (Previous != NewScale && Settings::VerboseLog())
            REX::INFO("Sprint speed: first person sprint speed x{:.3f} (was x{:.3f})", NewScale, Previous);
    }
}
