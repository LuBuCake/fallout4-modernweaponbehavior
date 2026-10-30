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

#include "FireLast.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::array LastClips{ "MWB_WPNFireLastReady"sv, "MWB_WPNFireLastSighted"sv, "MWB_WPNFireLastReadySlave"sv };

    bool WarnedGraph{ false };

    [[nodiscard]] bool Write(RE::PlayerCharacter* Player, const RE::BSFixedString& Variable, std::int32_t Wanted)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
            return false;

        if (Current != Wanted)
            Player->SetGraphVariableInt(Variable, Wanted);

        return true;
    }
}

namespace FireLast
{
    void OnEventSending(const RE::BSFixedString& Event)
    {
        static const RE::BSFixedString AttackStart{ "attackStart" };

        if (Event != AttackStart)
            return;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
            return;

        const bool ThirdPerson = Player->Is3rdPersonVisible();
        const auto Weapon = Core::GetEquippedWeapon(Player);
        const auto Rule = Weapon ? WeaponConfig::MatchFireLast(Weapon->Base, Weapon->Instance) : std::nullopt;
        const auto Loaded = Weapon && Weapon->Data ? Weapon->Data->ammoCount : 0;
        const bool Person = Rule && (ThirdPerson ? Rule->ThirdPerson : Rule->FirstPerson);
        const bool Last = Person && Loaded == 1;

        static const RE::BSFixedString ReadyVariable{ "iMWB_FireLastReady" };
        static const RE::BSFixedString SightedVariable{ "iMWB_FireLastSighted" };

        const bool Known = ThirdPerson ? Write(Player, ReadyVariable, Last ? 1 : 0) :
                                         Write(Player, ReadyVariable, Last && Rule->Ready ? 1 : 0) && Write(Player, SightedVariable, Last && Rule->Sighted ? 1 : 0);

        if (!Known)
        {
            if (Person && !WarnedGraph)
            {
                REX::WARN("The player's animation graph does not know iMWB_FireLastReady (or iMWB_FireLastSighted). The last shot clips need "
                          "Modern Weapon Behavior's GunBehavior (first person) or WeaponBehavior (third person), its root and weapon "
                          "wrapping behaviors, winning over other mods'");
                WarnedGraph = true;
            }

            return;
        }

        if (Rule && Settings::VerboseLog())
            REX::INFO("Last shot: attackStart with {} loaded, {} person -> {}", Loaded, ThirdPerson ? "third" : "first",
                      !Last       ? "vanilla clips" :
                      ThirdPerson ? "slave clip" :
                      Rule->Ready && Rule->Sighted ? "both clips" : Rule->Ready ? "ready clip" : "sighted clip");
    }

    bool Playing()
    {
        return Hooks::FindActiveClip(LastClips) != nullptr;
    }
}
