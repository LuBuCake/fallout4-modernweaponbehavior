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

#include "AimAfterLoad.h"
#include "Core/EquippedWeapon.h"
#include "Core/GunState.h"
#include "Settings/Settings.h"

namespace
{
    constexpr float SettleSeconds = 0.25f;
    constexpr float GiveUpSeconds = 3.0f;

    std::atomic<bool> Pending{ false };
    std::atomic<bool> Fresh{ false };
    float             Elapsed{ 0.0f };

    [[nodiscard]] bool GunAtRest(RE::PlayerCharacter* Player)
    {
        if (!Player->GetWeaponMagicDrawn() || Player->meleeAttackState != 0)
            return false;

        const auto Weapon = Core::GetEquippedWeapon(Player);

        if (!Weapon || !Weapon->Base || Weapon->Base->weaponData.type.get() != RE::WEAPON_TYPE::kGun)
            return false;

        switch (Core::GetGunState(Player))
        {
            case RE::GUN_STATE::kDrawn:
            case RE::GUN_STATE::kRelaxed:
            case RE::GUN_STATE::kAlert:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] bool GunActed(RE::PlayerCharacter* Player)
    {
        switch (Core::GetGunState(Player))
        {
            case RE::GUN_STATE::kReloading:
            case RE::GUN_STATE::kThrowing:
            case RE::GUN_STATE::kSighted:
            case RE::GUN_STATE::kFire:
            case RE::GUN_STATE::kFireSighted:
                return true;
            default:
                return false;
        }
    }

    void Finish(std::string_view Why)
    {
        Pending = false;
        Elapsed = 0.0f;

        if (Settings::VerboseLog())
            REX::INFO("Aim after load: {}", Why);
    }
}

namespace AimAfterLoad
{
    void Arm()
    {
        Elapsed = 0.0f;
        Pending = true;
        Fresh = true;
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        if (Fresh.load() && Player->GetWeaponMagicDrawn() && GunActed(Player))
        {
            Fresh = false;

            if (Settings::VerboseLog())
                REX::INFO("Aim after load: the gun acted, the graph opens the sights again on its own");
        }

        if (!Pending.load())
            return;

        Elapsed += Delta;

        if (Elapsed < SettleSeconds)
            return;

        if (!Player->GetWeaponMagicDrawn())
        {
            Finish("no weapon drawn, nothing to do");
            return;
        }

        if (!GunAtRest(Player))
        {
            if (Elapsed >= GiveUpSeconds)
                Finish("the gun never came to rest, given up");

            return;
        }

        if (Player->NotifyAnimationGraphImpl(OpenEvent()))
        {
            Finish("initiateStart sent, the sights can be raised");
            return;
        }

        if (Elapsed >= GiveUpSeconds)
            Finish("initiateStart refused, given up");
    }

    bool Refused(const RE::BSFixedString& Event)
    {
        static const RE::BSFixedString AimEvent{ "rifleSightedStart" };

        if (!Fresh.load() || Event != AimEvent)
            return false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        return Player && GunAtRest(Player);
    }

    const RE::BSFixedString& OpenEvent()
    {
        static const RE::BSFixedString Event{ "initiateStart" };

        return Event;
    }

    void Retried(bool Accepted)
    {
        if (Settings::VerboseLog())
            REX::INFO("Aim after load: rifleSightedStart refused at rest, initiateStart sent, sights {}", Accepted ? "raised" : "still refused");
    }

    void Reset()
    {
        Pending = false;
        Fresh = false;
        Elapsed = 0.0f;
    }
}
