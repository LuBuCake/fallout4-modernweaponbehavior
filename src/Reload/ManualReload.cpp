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

#include "ManualReload.h"
#include "Config/WeaponConfig.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t UseAmmoIndex = 0xF0;

    constexpr REL::ID     UseAmmoID{ 902833, 2233939 };
    constexpr REL::ID     QueueActionID{ 390914, 2234273 };
    constexpr std::size_t ScanSize = 0x400;

    constexpr std::uint8_t CallOpcode = 0xE8;
    constexpr std::size_t  CallInstructionSize = 5;

    constexpr float DisarmedTimer = -1.0f;

    thread_local bool SkipAutoReload{ false };

    [[nodiscard]] bool ManualFor(const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& Weapon)
    {
        if (!Settings::ManualReload())
            return false;

        const auto Base = Weapon.object ? Weapon.object->As<RE::TESObjectWEAP>() : nullptr;
        const auto Instance = static_cast<const RE::TESObjectWEAP::InstanceData*>(Weapon.instanceData.get());

        return !Base || !WeaponConfig::Excluded(WeaponConfig::Feature::ManualReload, Base, Instance);
    }

    struct QueueAutoReload
    {
        static void Thunk(void* Action)
        {
            if (SkipAutoReload)
            {
                if (Settings::VerboseLog())
                    REX::INFO("Manual reload: automatic reload skipped");

                return;
            }

            Original(Action);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct UseAmmo
    {
        static std::uint32_t Thunk(RE::PlayerCharacter* Player, const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& Weapon, std::uint32_t EquipIndex, std::uint32_t Flags)
        {
            const bool Manual = ManualFor(Weapon);
            SkipAutoReload = Manual;
            const auto Remaining = Original(Player, Weapon, EquipIndex, Flags);
            SkipAutoReload = false;

            if (Remaining == 0 && !Manual && Settings::ManualReload() && Settings::VerboseLog())
                REX::INFO("Manual reload: the weapon is excluded, the game reloads on its own");

            if (Remaining == 0 && Manual && Player->autoReloadTimer >= 0.0f)
            {
                Player->autoReloadTimer = DisarmedTimer;

                if (Settings::VerboseLog())
                    REX::INFO("Manual reload: delayed automatic reload skipped (scope)");
            }

            return Remaining;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCall(std::uintptr_t Begin, std::uintptr_t Target)
    {
        std::optional<std::uintptr_t> Found;

        for (std::size_t i = 0; i + CallInstructionSize <= ScanSize; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != CallOpcode)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(At + 1);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + CallInstructionSize) + Displacement) != Target)
                continue;

            if (Found)
                return std::nullopt;

            Found = At;
        }

        return Found;
    }
}

namespace ManualReload
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[0] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(Player);

            if (Live != Vtable.address())
            {
                REX::ERROR("PlayerCharacter vtable mismatch (id {:X}, live {:X}). Manual reload disabled.", Vtable.address(), Live);
                return;
            }
        }

        const auto Caller = REL::Relocation<std::uintptr_t>{ UseAmmoID }.address();
        const auto Target = REL::Relocation<std::uintptr_t>{ QueueActionID }.address();
        const auto Site = FindCall(Caller, Target);

        if (!Site)
        {
            REX::WARN("The automatic reload call was not found (another mod may have it). Manual reload disabled.");
            return;
        }

        UseAmmo::Original = Vtable.write_vfunc(UseAmmoIndex, UseAmmo::Thunk);
        QueueAutoReload::Original = REL::GetTrampoline().write_call<5>(*Site, QueueAutoReload::Thunk);
        REX::INFO("Manual reload installed (automatic reload at {:X}, chaining to {:X})", *Site, QueueAutoReload::Original.address());
    }
}
