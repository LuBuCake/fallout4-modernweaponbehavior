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
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t UseAmmoIndex = 0xF0;

    constexpr REL::ID     UseAmmoID{ 902833, 2233939 };
    constexpr REL::ID     QueueActionID{ 390914, 2234273 };
    constexpr std::size_t ScanSize = 0x400;

    constexpr REL::ID     PlayOneShotID{ 1239759, 2214755 };
    constexpr std::size_t DoActionScanSize = 0x800;
    constexpr std::size_t DoActionSelfCalls = 2;
    constexpr std::size_t ReloadArgumentWindow = 12;
    constexpr std::array<std::uint8_t, 3> ReloadArgument{ 0x8D, 0x50, 0x6C };

    constexpr std::uint8_t CallOpcode = 0xE8;
    constexpr std::size_t  CallInstructionSize = 5;

    constexpr float DisarmedTimer = -1.0f;

    thread_local bool SkipAutoReload{ false };

    [[nodiscard]] bool ManualFor(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return Settings::ManualReload() && (!Base || !WeaponConfig::Excluded(WeaponConfig::Feature::ManualReload, Base, Instance));
    }

    [[nodiscard]] bool ManualFor(const RE::BGSObjectInstanceT<RE::TESObjectWEAP>& Weapon)
    {
        const auto Base = Weapon.object ? Weapon.object->As<RE::TESObjectWEAP>() : nullptr;

        return ManualFor(Base, static_cast<const RE::TESObjectWEAP::InstanceData*>(Weapon.instanceData.get()));
    }

    void PlayDryFire(const Core::EquippedWeapon& Weapon)
    {
        using PlayOneShot_t = void(RE::BGSSoundDescriptorForm*, std::uint32_t, const RE::NiPoint3*, RE::NiAVObject*);
        static REL::Relocation<PlayOneShot_t*> PlayOneShot{ PlayOneShotID };

        const auto Sound = Weapon.Instance ? Weapon.Instance->attackFailSound : Weapon.Base->weaponData.attackFailSound;

        if (Sound)
            PlayOneShot(Sound, 0, nullptr, nullptr);
    }

    struct FailedAttackReload
    {
        static bool Thunk(RE::PlayerControls* Controls, RE::DEFAULT_OBJECT Action, RE::ActionInput::ACTIONPRIORITY Priority)
        {
            const auto Weapon = Core::GetEquippedWeapon(RE::PlayerCharacter::GetSingleton());

            if (Action != RE::DEFAULT_OBJECT::kActionReload || !Weapon || !ManualFor(Weapon->Base, Weapon->Instance))
                return Original(Controls, Action, Priority);

            if (Settings::VerboseLog())
                REX::INFO("Manual reload: the trigger on an empty magazine dry fires instead of reloading");

            const bool Fired = Original(Controls, RE::DEFAULT_OBJECT::kActionFireEmpty, Priority);
            PlayDryFire(*Weapon);
            return Fired;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

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

    [[nodiscard]] std::optional<std::uintptr_t> FindFailedAttackReload(std::uintptr_t DoAction)
    {
        std::vector<std::uintptr_t> SelfCalls;

        for (std::size_t i = ReloadArgumentWindow; i + CallInstructionSize <= DoActionScanSize; ++i)
        {
            const auto At = DoAction + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != CallOpcode)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(At + 1);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + CallInstructionSize) + Displacement) == DoAction)
                SelfCalls.push_back(At);
        }

        if (SelfCalls.size() != DoActionSelfCalls)
            return std::nullopt;

        std::optional<std::uintptr_t> Found;

        for (const auto At : SelfCalls)
        {
            const std::span<const std::uint8_t> Before{ reinterpret_cast<const std::uint8_t*>(At - ReloadArgumentWindow), ReloadArgumentWindow };

            if (std::ranges::search(Before, ReloadArgument).empty())
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

        const auto DoAction = REL::Relocation<std::uintptr_t>{ RE::ID::PlayerControls::DoAction }.address();
        const auto FailedSite = FindFailedAttackReload(DoAction);

        if (!FailedSite)
        {
            REX::WARN("The reload on a failed attack was not found (another mod may have it). The trigger on an empty magazine still reloads.");
            return;
        }

        FailedAttackReload::Original = REL::GetTrampoline().write_call<5>(*FailedSite, FailedAttackReload::Thunk);
        REX::INFO("Manual reload: trigger on an empty magazine at {:X}, chaining to {:X}", *FailedSite, FailedAttackReload::Original.address());
    }
}
