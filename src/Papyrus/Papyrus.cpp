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

#include "Papyrus.h"
#include "Settings/Settings.h"

#include <variant>

namespace
{
    constexpr std::string_view ScriptName = "MWB";

    using GlobalFunctionSelf = std::monostate;

    bool Send(std::string_view Event)
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const bool Accepted = Player && Player->NotifyAnimationGraphImpl(RE::BSFixedString{ Event });

        if (Settings::VerboseLog())
            REX::INFO("Papyrus: {} {}", Event, Accepted ? "accepted" : "refused");

        return Accepted;
    }

    bool Inspect(GlobalFunctionSelf)
    {
        return Send("MWB_Inspect");
    }

    bool AmmoCheck(GlobalFunctionSelf)
    {
        return Send("MWB_AmmoCheck");
    }

    bool SpecialAttack(GlobalFunctionSelf)
    {
        return Send("MWB_SpecialAttack");
    }

    bool StopArmAnimation(GlobalFunctionSelf)
    {
        return Send("MWB_ArmStop");
    }

    bool SwitchFireMode(GlobalFunctionSelf)
    {
        return Send("MWB_SwitchFireModeStart");
    }
}

namespace Papyrus
{
    void Register()
    {
        const auto PapyrusInterface = F4SE::GetPapyrusInterface();

        if (!PapyrusInterface)
        {
            REX::WARN("No F4SE Papyrus interface: MWB.Inspect, MWB.AmmoCheck, MWB.SpecialAttack, MWB.StopArmAnimation and MWB.SwitchFireMode will not exist");
            return;
        }

        PapyrusInterface->Register([](RE::BSScript::IVirtualMachine* VM)
        {
            if (!VM)
                return false;

            VM->BindNativeMethod(ScriptName, "Inspect", Inspect);
            VM->BindNativeMethod(ScriptName, "AmmoCheck", AmmoCheck);
            VM->BindNativeMethod(ScriptName, "SpecialAttack", SpecialAttack);
            VM->BindNativeMethod(ScriptName, "StopArmAnimation", StopArmAnimation);
            VM->BindNativeMethod(ScriptName, "SwitchFireMode", SwitchFireMode);
            REX::INFO("Papyrus functions registered on \"{}\"", ScriptName);

            return true;
        });
    }
}
