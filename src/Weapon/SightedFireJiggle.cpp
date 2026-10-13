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

#include "SightedFireJiggle.h"
#include "Settings/Settings.h"

namespace SightedFireJiggle
{
    void Tick(RE::PlayerCharacter* Player)
    {
        if (!Player)
            return;

        static const RE::BSFixedString Variable{ "iMWB_SightedFireJiggle" };
        const std::int32_t Wanted = Settings::SightedFireJiggle() ? 1 : 0;

        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current) || Current == Wanted)
            return;

        Player->SetGraphVariableInt(Variable, Wanted);

        if (Settings::VerboseLog())
            REX::INFO("Sighted fire jiggle: iMWB_SightedFireJiggle {} -> {}", Current, Wanted);
    }
}
