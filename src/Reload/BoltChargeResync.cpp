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

#include "BoltChargeResync.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr REL::ID ReloadStateHandlerID{ 256338, 2235361 };
    using ReloadStateHandlerSignature = bool(void*, RE::Actor*, const RE::BSFixedString*);

    constexpr auto AfterSwitch = std::chrono::milliseconds(1000);
    constexpr auto BoltChargeTimeout = std::chrono::seconds(5);

    std::optional<RE::BSFixedString> ReloadStart;
    std::optional<RE::BSFixedString> BoltChargeStart;
    std::optional<RE::BSFixedString> ExitPayload;

    std::mutex        Lock;
    bool              BoltCharging{ false };
    Clock::time_point BoltChargeAt{};
    Clock::time_point SwitchedAt{};
    std::atomic<bool> ThirdPersonSeen{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }
}

namespace BoltChargeResync
{
    void Initialize()
    {
        ReloadStart.emplace("reloadStart");
        BoltChargeStart.emplace("boltChargeStart");
        ExitPayload.emplace("Exit");
    }

    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        if (!Accepted || !ReloadStart || (Event != *BoltChargeStart && Event != *ReloadStart))
            return;

        const auto Player = RE::PlayerCharacter::GetSingleton();
        const bool FirstPerson = Player && !Player->Is3rdPersonVisible();

        const std::scoped_lock Guard{ Lock };
        BoltCharging = Event == *BoltChargeStart && FirstPerson;
        BoltChargeAt = Clock::now();
    }

    const RE::BSFixedString* Replace(const RE::BSFixedString& Event)
    {
        if (!ReloadStart || Event != *ReloadStart)
            return nullptr;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !Player->Is3rdPersonVisible())
            return nullptr;

        {
            const std::scoped_lock Guard{ Lock };

            const auto Now = Clock::now();
            const bool JustSwitched = !ThirdPersonSeen || (SwitchedAt >= BoltChargeAt && Now - SwitchedAt <= AfterSwitch);

            if (!BoltCharging || Now - BoltChargeAt > BoltChargeTimeout || !JustSwitched)
                return nullptr;
        }

        const auto Weapon = Core::GetEquippedWeapon(Player);

        if (!Weapon || !Weapon->Data || Weapon->Data->ammoCount == 0)
            return nullptr;

        if (Settings::VerboseLog())
        {
            REX::INFO("Bolt charge resync: reloadStart after the switch to third person mid bolt charge ({} loaded) -> boltChargeStart",
                Weapon->Data->ammoCount);
        }

        return &*BoltChargeStart;
    }

    void Taken()
    {
        {
            const std::scoped_lock Guard{ Lock };
            BoltCharging = false;
        }

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !ExitPayload)
            return;

        static REL::Relocation<ReloadStateHandlerSignature> Handler{ ReloadStateHandlerID };
        Handler(nullptr, Player, &*ExitPayload);

        if (Settings::VerboseLog())
            REX::INFO("Bolt charge resync: boltChargeStart taken; reload state cleared");
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (Tag.empty() || !TagIs(Tag, "reloadEnd"sv))
            return;

        const std::scoped_lock Guard{ Lock };
        BoltCharging = false;
    }

    void Tick(RE::PlayerCharacter* Player)
    {
        const bool ThirdPerson = Player && Player->Is3rdPersonVisible();

        if (ThirdPerson && !ThirdPersonSeen.exchange(true))
        {
            const std::scoped_lock Guard{ Lock };
            SwitchedAt = Clock::now();
        }
        else if (!ThirdPerson)
        {
            ThirdPersonSeen = false;
        }
    }
}
