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

#include "AnimationEventHook.h"
#include "Magic/MagicAnnotations.h"
#include "Arm/ArmAnimations.h"
#include "Fire/AmmoFire.h"
#include "Reload/BoltChargeResync.h"
#include "Reload/BulletInChamber.h"
#include "Reload/DropUnspentAmmo.h"
#include "Reload/LoopReload.h"
#include "Reload/SightedBoltCharge.h"
#include "Reload/SightedReload.h"
#include "Settings/Settings.h"
#include "Sprint/SprintFire.h"
#include "Sprint/SprintTransitions.h"
#include "Weapon/FireMode.h"
#include "Weapon/ScopeSway.h"
#include "Weapon/SlideLock.h"
#include "Weapon/WeaponGraphSights.h"

namespace
{
    using Sink = RE::BSTEventSink<RE::BSAnimationGraphEvent>;
    using Source = RE::BSTEventSource<RE::BSAnimationGraphEvent>;

    constexpr std::size_t SinkVtableIndex = 3;
    constexpr std::size_t SinkOffset = 0x38;
    constexpr std::size_t ProcessEventSlot = 1;

    [[nodiscard]] RE::Actor* SinkOwner(Sink* EventSink)
    {
        return reinterpret_cast<RE::Actor*>(reinterpret_cast<std::uintptr_t>(EventSink) - SinkOffset);
    }

    std::array<std::atomic<const Source*>, 2> PlayerSources{};

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() ? Tag.c_str() : "" };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    void NotePlayerSource(const Source* EventSource)
    {
        if (!EventSource || PlayerSources[0].load() == EventSource || PlayerSources[1].load() == EventSource)
            return;

        PlayerSources[1] = PlayerSources[0].load();
        PlayerSources[0] = EventSource;
    }

    [[nodiscard]] bool FromPlayer(const Source* EventSource)
    {
        return EventSource && (PlayerSources[0].load() == EventSource || PlayerSources[1].load() == EventSource);
    }

    void LogCustomEvent(RE::Actor* Actor, const RE::BSAnimationGraphEvent& Event)
    {
        constexpr std::string_view CustomTag = "MWB_Event";
        const std::string_view     Tag = Event.tag;

        if (!Settings::VerboseLog() || Tag.size() < CustomTag.size() || _strnicmp(Tag.data(), CustomTag.data(), CustomTag.size()) != 0 ||
            (Tag.size() > CustomTag.size() && Tag[CustomTag.size()] != '.'))
        {
            return;
        }

        REX::INFO("MWB_Event from {:08X} (tag '{}', payload '{}')", Actor ? Actor->GetFormID() : 0, Tag, std::string_view{ Event.payload });
    }

    struct ProcessEvent
    {
        static RE::BSEventNotifyControl Thunk(Sink* This, const RE::BSAnimationGraphEvent& Event, Source* EventSource)
        {
            Reload::TraceEvent(Event);
            NotePlayerSource(EventSource);
            const auto ReloadAction = Reload::BeforeGameHandles(Event, EventSource);

            if (SightedReload::OnAnimationEvent(Event))
                return RE::BSEventNotifyControl::kContinue;

            if (SightedBoltCharge::OnAnimationEvent(Event))
                return RE::BSEventNotifyControl::kContinue;

            const auto Result = Original(This, Event, EventSource);
            Reload::AfterGameHandles(ReloadAction);
            Sprint::OnAnimationEvent(Event.tag);
            SprintFire::OnAnimationEvent(Event);
            SlideLock::OnAnimationEvent(Event.tag);
            AmmoFire::OnAnimationEvent(SinkOwner(This), Event);
            MagicAnnotations::OnAnimationEvent(SinkOwner(This), Event);

            if (ArmAnimations::OnAnimationEvent(Event.tag))
            {
                const RE::BSAnimationGraphEvent Unload{ Event.holderID, ArmAnimations::Unload(), RE::BSFixedString{} };
                Original(This, Unload, EventSource);
            }

            FireMode::OnAnimationEvent(Event.tag);
            BulletInChamber::OnAnimationEvent(Event.tag);
            DropUnspentAmmo::OnAnimationEvent(Event.tag);
            BoltChargeResync::OnAnimationEvent(Event.tag);
            ScopeSway::OnAnimationEvent(Event.tag);
            LogCustomEvent(SinkOwner(This), Event);

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct ActorProcessEvent
    {
        static RE::BSEventNotifyControl Thunk(Sink* This, const RE::BSAnimationGraphEvent& Event, Source* EventSource)
        {
            const auto Result = Original(This, Event, EventSource);
            Reload::OnActorEvent(SinkOwner(This), Event);
            AmmoFire::OnAnimationEvent(SinkOwner(This), Event);
            MagicAnnotations::OnAnimationEvent(SinkOwner(This), Event);
            LogCustomEvent(SinkOwner(This), Event);

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct ManagerProcessEvent
    {
        static RE::BSEventNotifyControl Thunk(Sink* This, const RE::BSAnimationGraphEvent& Event, Source* EventSource)
        {
            if (WeaponGraphSights::KeepFromDependents(This, Event))
                return RE::BSEventNotifyControl::kContinue;

            const auto Result = Original(This, Event, EventSource);
            WeaponGraphSights::AfterForwarding(Event, FromPlayer(EventSource));

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
        inline static bool                             Installed{ false };
    };

    struct CompleteObjectLocator
    {
        std::uint32_t Signature;
        std::uint32_t Offset;
        std::uint32_t CdOffset;
        std::uint32_t TypeDescriptor;
    };

    [[nodiscard]] bool VtableIs(std::uintptr_t Vtable, std::size_t Offset, std::string_view TypeName)
    {
        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator || Locator->Offset != Offset)
            return false;

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*)) == TypeName;
    }

    [[nodiscard]] bool IsActorSinkVtable(std::uintptr_t Vtable)
    {
        return VtableIs(Vtable, SinkOffset, ".?AVActor@@"sv);
    }
}

namespace Hooks
{
    void InstallAnimationEventHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[SinkVtableIndex] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(Player) + SinkOffset);

            if (Live != Vtable.address())
            {
                REX::ERROR("Animation event sink vtable mismatch (id {:X}, live {:X}). Loop reload disabled.", Vtable.address(), Live);
                return;
            }
        }

        ProcessEvent::Original = Vtable.write_vfunc(ProcessEventSlot, ProcessEvent::Thunk);
        REX::INFO("Animation event hook installed");
    }

    void InstallActorAnimationEventHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::Actor[SinkVtableIndex] };

        if (!IsActorSinkVtable(Vtable.address()))
        {
            REX::ERROR("Actor animation event sink vtable id {:X} is not Actor's at +{:X}. MWB_WeaponFireAmmo, MWB_UseAlchemyItem and MWB_CastSpell work for the player only.",
                Vtable.address(), SinkOffset);
            return;
        }

        ActorProcessEvent::Original = Vtable.write_vfunc(ProcessEventSlot, ActorProcessEvent::Thunk);
        REX::INFO("Actor animation event hook installed");
    }

    void InstallGraphManagerEventHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::BSAnimationGraphManager[0] };

        if (!VtableIs(Vtable.address(), 0, ".?AVBSAnimationGraphManager@@"sv))
        {
            REX::WARN("Animation graph manager vtable id {:X} is not BSAnimationGraphManager's. Weapon graphs (See Through Scopes) get the "
                      "player's sightedStateEnter/Exit as the behaviors send them.",
                Vtable.address());
            return;
        }

        ManagerProcessEvent::Original = Vtable.write_vfunc(ProcessEventSlot, ManagerProcessEvent::Thunk);
        ManagerProcessEvent::Installed = true;
        REX::INFO("Animation graph manager event hook installed");
    }

    bool GraphManagerHookInstalled()
    {
        return ManagerProcessEvent::Installed;
    }

    void ForwardToDependentGraphs(RE::BSAnimationGraphManager* Manager, const RE::BSFixedString& Tag)
    {
        if (!ManagerProcessEvent::Installed || !Manager)
            return;

        const RE::BSAnimationGraphEvent Event{ 0, Tag, RE::BSFixedString{} };
        ManagerProcessEvent::Original(Manager, Event, nullptr);
    }
}
