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

#include "WeaponGraphSights.h"
#include "Core/GunState.h"
#include "Hooks/AnimationEventHook.h"
#include "Settings/Settings.h"

namespace
{
    std::atomic<const void*> PlayerManager{ nullptr };
    std::atomic<bool>        Resend{ false };
    bool                     Sent{ false };
    const void*              SeenDependents{ nullptr };
    std::uint32_t            SeenDependentCount{ 0 };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() ? Tag.c_str() : "" };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] bool IsSightedTag(const RE::BSFixedString& Tag)
    {
        return TagIs(Tag, "sightedStateEnter"sv) || TagIs(Tag, "sightedStateExit"sv);
    }

    [[nodiscard]] bool IsFirstPersonStart(const RE::BSFixedString& Tag)
    {
        return TagIs(Tag, "FirstPersonInitialized"sv) || TagIs(Tag, "PowerFirstPersonInitialized"sv);
    }

    [[nodiscard]] bool InSights(RE::PlayerCharacter* Player)
    {
        const auto State = Core::GetGunState(Player);

        return State == RE::GUN_STATE::kSighted || State == RE::GUN_STATE::kFireSighted;
    }
}

namespace WeaponGraphSights
{
    bool KeepFromDependents(const void* Manager, const RE::BSAnimationGraphEvent& Event)
    {
        if (!IsSightedTag(Event.tag) || !Manager || Manager != PlayerManager.load())
            return false;

        if (Settings::VerboseLog())
            REX::INFO("Weapon graph sights: {} kept from the weapon's graph", Event.tag.c_str());

        return true;
    }

    void AfterForwarding(const RE::BSAnimationGraphEvent& Event, bool FromPlayer)
    {
        if (FromPlayer && IsFirstPersonStart(Event.tag))
            Resend = true;
    }

    void Tick(RE::PlayerCharacter* Player)
    {
        if (!Player || !Hooks::GraphManagerHookInstalled())
            return;

        RE::BSTSmartPointer<RE::BSAnimationGraphManager> Manager;

        if (!static_cast<RE::IAnimationGraphManagerHolder*>(Player)->GetAnimationGraphManagerImpl(Manager) || !Manager)
        {
            PlayerManager = nullptr;
            return;
        }

        PlayerManager = Manager.get();

        const auto& Dependents = Manager->subManagers;
        const bool  DependentsChanged = Dependents.data() != SeenDependents || Dependents.size() != SeenDependentCount;
        SeenDependents = Dependents.data();
        SeenDependentCount = Dependents.size();

        const bool Sighted = InSights(Player);
        const bool Again = Resend.exchange(false) || DependentsChanged;

        if (Sighted == Sent && !(Sighted && Again))
            return;

        Sent = Sighted;

        static const RE::BSFixedString EnterEvent{ "sightedStateEnter" };
        static const RE::BSFixedString ExitEvent{ "sightedStateExit" };
        Hooks::ForwardToDependentGraphs(Manager.get(), Sighted ? EnterEvent : ExitEvent);

        if (Settings::VerboseLog())
            REX::INFO("Weapon graph sights: {} sent to the weapon's graph ({} dependent graph manager(s))", Sighted ? "sightedStateEnter" : "sightedStateExit",
                Dependents.size());
    }

    void Reset()
    {
        PlayerManager = nullptr;
        Resend = false;
        Sent = false;
        SeenDependents = nullptr;
        SeenDependentCount = 0;
    }
}
