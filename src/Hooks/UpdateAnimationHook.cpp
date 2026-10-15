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

#include "UpdateAnimationHook.h"
#include "Magic/MagicAnnotations.h"
#include "Arm/ArmSpace.h"
#include "HUD/AmmoCheckDisplay.h"
#include "HUD/WeaponHUD.h"
#include "Melee/HoldBlock.h"
#include "ClipGeneratorHook.h"
#include "Reload/BoltChargeResync.h"
#include "Reload/DropUnspentAmmo.h"
#include "Reload/LoopReload.h"
#include "Reload/SightedReload.h"
#include "Settings/Settings.h"
#include "Sprint/SprintCamera.h"
#include "Sprint/SprintFire.h"
#include "Sprint/SprintTransitions.h"
#include "Weapon/FireMode.h"
#include "Weapon/AimAfterLoad.h"
#include "Weapon/AimDownSight.h"
#include "Weapon/FireModePose.h"
#include "Weapon/ScopeSway.h"
#include "Weapon/SightedFireJiggle.h"
#include "Weapon/SlideLock.h"
#include "Weapon/WeaponBones.h"

namespace
{
    constexpr std::size_t UpdateAnimationSlot = 0x9F;

    struct UpdateAnimation
    {
        static void Thunk(RE::PlayerCharacter* This, float Delta)
        {
            ArmSpace::Diagnose(This);
            MagicAnnotations::OnUpdateAnimation(This);
            Original(This, Delta);
            Hooks::RefreshPlayerCharacters(This);
            Reload::Tick(Delta);
            Sprint::Tick(This, Delta);
            SprintCamera::Tick(This);
            BoltChargeResync::Tick(This);
            SightedReload::Tick(This, Delta);
            AimDownSight::Tick(This);
            SightedFireJiggle::Tick(This);
            DropUnspentAmmo::Tick(This);
            FireMode::Tick(This);
            WeaponBones::BeginFrame(This);
            SlideLock::Tick(This, Delta);
            FireModePose::Tick(This, Delta);
            WeaponBones::EndFrame();
            AmmoCheckDisplay::Tick(This);
            WeaponHUD::Tick(This, Delta);
            ScopeSway::Tick(This, Delta);
            HoldBlock::Tick(This, Delta);
            SprintFire::Tick(This, Delta);
            AimAfterLoad::Tick(This, Delta);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct ActorUpdateAnimation
    {
        static void Thunk(RE::Actor* This, float Delta)
        {
            MagicAnnotations::OnUpdateAnimation(This);
            Original(This, Delta);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] bool IsActorVtable(std::uintptr_t Vtable)
    {
        struct CompleteObjectLocator
        {
            std::uint32_t Signature;
            std::uint32_t Offset;
            std::uint32_t CdOffset;
            std::uint32_t TypeDescriptor;
        };

        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator || Locator->Offset != 0)
            return false;

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*)) == ".?AVActor@@"sv;
    }
}

namespace Hooks
{
    void InstallUpdateAnimationHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[0] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(Player);

            if (Live != Vtable.address())
            {
                REX::ERROR("PlayerCharacter vtable mismatch (id {:X}, live {:X}). Loop delays disabled: every pass is asked for at once.", Vtable.address(), Live);
                return;
            }
        }

        UpdateAnimation::Original = Vtable.write_vfunc(UpdateAnimationSlot, UpdateAnimation::Thunk);
        Reload::SetTickAvailable(true);
        REX::INFO("UpdateAnimation hook installed");
    }

    void InstallActorUpdateAnimationHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::Actor[0] };

        if (!IsActorVtable(Vtable.address()))
        {
            REX::ERROR("Actor vtable id {:X} is not Actor's. The fallback of MWB_UseAlchemyItem and MWB_CastSpell works for the player only.", Vtable.address());
            return;
        }

        ActorUpdateAnimation::Original = Vtable.write_vfunc(UpdateAnimationSlot, ActorUpdateAnimation::Thunk);
        REX::INFO("Actor UpdateAnimation hook installed");
    }
}
