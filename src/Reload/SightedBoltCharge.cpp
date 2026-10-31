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

#include "SightedBoltCharge.h"
#include "Core/EquippedWeapon.h"
#include "Core/GunState.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Reload/SightedReload.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::int32_t SightedStateID = 1;
    constexpr float        AskInterval = 0.1f;
    constexpr float        GiveUpSeconds = 3.0f;
    constexpr float        FollowAfter = 0.1f;
    constexpr std::chrono::milliseconds TraceAfter{ 1500 };
    constexpr std::chrono::milliseconds ReentryWindow{ 200 };
    constexpr float                     CutAfter = 0.1f;
    constexpr std::array                BoltClips{ "WPNBoltCharge"sv, "WPNBoltChargeSighted"sv };

    std::mutex Lock;
    bool       Starting{ false };
    bool       StartedSighted{ false };
    bool       EndedSighted{ false };
    bool       LeftBolt{ false };

    std::atomic<bool> InSightedBolt{ false };
    std::atomic<bool> Active{ false };
    std::atomic<bool> Outside{ false };
    float             Mismatch{ 0.0f };
    float             ClipMissing{ 0.0f };
    std::atomic<bool> LeaveSights{ false };

    std::atomic<bool> ShotSeen{ false };
    std::atomic<bool> Due{ false };
    std::atomic<bool> Ready{ false };
    std::atomic<bool> Shot{ false };
    bool              BoltClipGone{ false };
    bool              ReadyWaited{ false };
    std::atomic<bool> Asking{ false };
    std::atomic<bool> GameTook{ false };
    float             DueAge{ 0.0f };
    float             AskIn{ 0.0f };

    std::atomic<std::int64_t> TraceUntil{ 0 };
    std::atomic<std::int64_t> ClosedAt{ 0 };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() ? Tag.c_str() : "" };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] bool InSights(RE::PlayerCharacter* Player)
    {
        const auto State = Core::GetGunState(Player);

        return State == RE::GUN_STATE::kSighted || State == RE::GUN_STATE::kFireSighted;
    }

    [[nodiscard]] std::int64_t Now()
    {
        return std::chrono::steady_clock::now().time_since_epoch().count();
    }

    void Trace(const RE::BSAnimationGraphEvent& Event)
    {
        if (!Settings::VerboseLog() || (!Active && Now() > TraceUntil.load()))
            return;

        const auto Player = RE::PlayerCharacter::GetSingleton();
        const auto State = Player ? static_cast<int>(Core::GetGunState(Player)) : -1;
        const std::string_view Payload{ Event.payload.c_str() ? Event.payload.c_str() : "" };

        REX::INFO("Sighted bolt charge trace: {}{}{}{} (gunState {})", Event.tag.c_str(), Payload.empty() ? "" : "(", Payload,
            Payload.empty() ? "" : ")", State);
    }

    const RE::BSFixedString& ReadyVariable()
    {
        static const RE::BSFixedString Variable{ "iMWB_BoltReady" };

        return Variable;
    }

    const RE::BSFixedString& StartVariable()
    {
        static const RE::BSFixedString Variable{ "iMWB_BoltSightedStart" };

        return Variable;
    }

    void Sync(RE::PlayerCharacter* Player, const RE::BSFixedString& Variable, std::int32_t WantedValue)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current) || Current == WantedValue)
            return;

        Player->SetGraphVariableInt(Variable, WantedValue);

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: {} {} -> {}", Variable.c_str(), Current, WantedValue);
    }

    void SetInSights(bool Sighted)
    {
        InSightedBolt = Sighted;

        static const RE::BSFixedString InSightsVariable{ "iMWB_BoltInSights" };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
            Sync(Player, InSightsVariable, Sighted ? 1 : 0);
    }

    [[nodiscard]] bool StartsInTheSights()
    {
        const auto   Player = RE::PlayerCharacter::GetSingleton();
        std::int32_t Start = 0;

        return Player && Player->GetGraphVariableImplInt(StartVariable(), Start) && Start == SightedStateID;
    }

    void SetSwap(bool Open)
    {
        Active = Open;
        TraceUntil = Now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(TraceAfter).count();

        if (!Open)
        {
            ClosedAt = Now();
            Outside = false;
        }

        static const RE::BSFixedString SwapVariable{ "iMWB_BoltSwap" };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
            Sync(Player, SwapVariable, Open ? 1 : 0);
    }

    void Cut(std::string_view Why)
    {
        LeaveSights = InSightedBolt.load();
        EndedSighted = false;
        LeftBolt = false;
        SetSwap(false);
        SetInSights(false);

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: cut before its end ({}){}", Why, LeaveSights ? ", the sights are left" : "");
    }

    void Watch(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Active || Player->Is3rdPersonVisible() || Hooks::FindActiveClip(BoltClips))
        {
            ClipMissing = 0.0f;
            return;
        }

        ClipMissing += Delta;

        if (ClipMissing < CutAfter)
            return;

        ClipMissing = 0.0f;

        const std::scoped_lock Guard{ Lock };

        if (Active)
            Cut("no bolt clip plays");
    }

    void LeaveIfStuck(RE::PlayerCharacter* Player)
    {
        if (!LeaveSights || Active)
            return;

        if (!InSights(Player))
        {
            LeaveSights = false;
            return;
        }

        if (SightedReload::AimButtonHeld())
            return;

        LeaveSights = false;
        Player->SetInIronSightsImpl(false);

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: still in the sights after the cut with the aim button up, sights left");
    }

    void Follow(RE::PlayerCharacter* Player, float Delta)
    {
        const bool Held = SightedReload::AimButtonHeld() && !Outside;

        if (!Active || Held == InSightedBolt.load())
        {
            Mismatch = 0.0f;
            return;
        }

        Mismatch += Delta;

        if (Mismatch < FollowAfter)
            return;

        Mismatch = 0.0f;

        static const RE::BSFixedString AimEvent{ "rifleSightedStart" };
        static const RE::BSFixedString ReleaseEvent{ "rifleSightedEnd" };
        const bool Accepted = Player->NotifyAnimationGraphImpl(Held ? AimEvent : ReleaseEvent);

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: aim button {} but the {} clip plays, {} {}", Held ? "held" : "up", Held ? "unsighted" : "sighted",
                Held ? "rifleSightedStart" : "rifleSightedEnd", Accepted ? "accepted" : "refused");
    }

    [[nodiscard]] bool BoltAction(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        if (!Weapon || !Weapon->Base || Weapon->Base->weaponData.type.get() != RE::WEAPON_TYPE::kGun)
            return false;

        const auto& Flags = Weapon->Instance ? Weapon->Instance->flags : Weapon->Base->weaponData.flags;

        return Flags.any(RE::WEAPON_FLAGS::kBoltAction);
    }

    void Settle(std::string_view Why)
    {
        Due = false;
        Ready = false;
        DueAge = 0.0f;
        AskIn = 0.0f;

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: bolt no longer due ({})", Why);
    }
}

namespace SightedBoltCharge
{
    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        Sync(Player, StartVariable(), SightedReload::AimButtonHeld() || InSights(Player) ? SightedStateID : 0);
        Watch(Player, Delta);
        LeaveIfStuck(Player);
        Follow(Player, Delta);

        if (ShotSeen.exchange(false))
        {
            std::optional<Core::EquippedWeapon> Weapon;
            const bool Known = Core::TryGetEquippedWeapon(Player, Weapon);
            const auto Loaded = Known && Weapon && Weapon->Data ? Weapon->Data->ammoCount : 0;

            if (!Known || !BoltAction(Weapon) || Loaded <= 0)
            {
                Ready = false;
                Shot = false;
            }
            else if (!Active)
            {
                Due = true;
                BoltClipGone = false;
                DueAge = 0.0f;
                AskIn = 0.0f;

                if (Settings::VerboseLog())
                    REX::INFO("Sighted bolt charge: shot with {} loaded, bolt due", Loaded);
            }
        }

        if (GameTook.exchange(false) && Due)
            Settle("the game's own boltChargeStart was taken");

        static const RE::BSFixedString DueVariable{ "iMWB_BoltDue" };
        static const RE::BSFixedString PluginVariable{ "iMWB_BoltPlugin" };
        Sync(Player, PluginVariable, 1);
        Sync(Player, DueVariable, Due ? 1 : 0);
        Sync(Player, ReadyVariable(), Ready ? 1 : 0);

        if (!Due)
            return;

        DueAge += Delta;

        if (DueAge >= GiveUpSeconds)
        {
            Settle("no bolt charge started, given up");
            return;
        }

        if (Player->Is3rdPersonVisible())
        {
            Settle("third person");
            return;
        }

        if (!Hooks::FindActiveClip(BoltClips))
            BoltClipGone = true;
        else if (BoltClipGone && !Active)
        {
            Settle("a bolt charge plays without the graph's MWB_BoltChargeStart");
            return;
        }

        if (!Ready)
            ReadyWaited = false;

        if (!Ready || Active || !Player->GetWeaponMagicDrawn() || Player->meleeAttackState != 0)
            return;

        if (!std::exchange(ReadyWaited, true))
            return;

        AskIn -= Delta;

        if (AskIn > 0.0f)
            return;

        AskIn = AskInterval;

        static const RE::BSFixedString BoltEvent{ "boltChargeStart" };
        Asking = true;
        const bool Accepted = Player->NotifyAnimationGraphImpl(BoltEvent);
        Asking = false;

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: bolt ready, boltChargeStart {}", Accepted ? "accepted" : "refused");
    }

    bool OnAnimationEvent(const RE::BSAnimationGraphEvent& Event)
    {
        const auto& Tag = Event.tag;

        if (Tag.empty())
            return false;

        Trace(Event);

        if (TagIs(Tag, "weaponFire"sv))
        {
            ShotSeen = true;
            TraceUntil = Now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(TraceAfter).count();
            return false;
        }

        if (TagIs(Tag, "attackState"sv) && TagIs(Event.payload, "Enter"sv))
        {
            Shot = true;
            return false;
        }

        if ((TagIs(Tag, "initiateBoltStart"sv) || TagIs(Tag, "initiateStart"sv)) && Shot.exchange(false))
        {
            Ready = true;

            if (const auto Player = RE::PlayerCharacter::GetSingleton())
                Sync(Player, ReadyVariable(), 1);

            if (Settings::VerboseLog())
                REX::INFO("Sighted bolt charge: the shot reached its bolt point ({}), bolt ready", Tag.c_str());

            return false;
        }

        const std::scoped_lock Guard{ Lock };

        if (TagIs(Tag, "MWB_BoltChargeStart"sv))
        {
            if (Settings::VerboseLog() && Now() - ClosedAt.load() < std::chrono::duration_cast<std::chrono::steady_clock::duration>(ReentryWindow).count())
                REX::INFO("Sighted bolt charge: WPNBoltCharge_MG entered again right after the end");

            Starting = true;
            StartedSighted = StartsInTheSights();
            EndedSighted = false;
            LeftBolt = false;
            Outside = false;
            SetSwap(true);
            Ready = false;
            Shot = false;

            if (const auto Player = RE::PlayerCharacter::GetSingleton())
                Sync(Player, ReadyVariable(), 0);

            if (Due)
                Settle("bolt charge started");

            return false;
        }

        if (TagIs(Tag, "reloadStateEnter"sv))
        {
            Shot = false;

            if (Due)
                Settle("reload or bolt charge entered");
        }

        if (TagIs(Tag, "reloadStateExit"sv) && Active)
        {
            LeftBolt = true;
            return false;
        }

        if (LeftBolt && (TagIs(Tag, "PASprintStart"sv) || TagIs(Tag, "reloadStateEnter"sv)))
        {
            LeftBolt = false;
            Outside = true;

            if (Settings::VerboseLog())
                REX::INFO("Sighted bolt charge: WPNBoltCharge_MG left before the end ({}), the bolt charge goes on unsighted", Tag.c_str());
        }
        else if (LeftBolt && ((TagIs(Tag, "reloadState"sv) && TagIs(Event.payload, "Exit"sv)) ||
                                 (TagIs(Tag, "sightedState"sv) && TagIs(Event.payload, "Enter"sv))))
        {
            LeftBolt = false;
            EndedSighted = InSightedBolt.load();
            SetSwap(false);
            SetInSights(false);

            if (Settings::VerboseLog())
                REX::INFO("Sighted bolt charge: WPNBoltCharge_MG left with no end event, taken as the end ({})", EndedSighted ? "in the sights" : "unsighted");
        }

        if (TagIs(Tag, "reloadEnd"sv) || TagIs(Tag, "MWB_BoltSightedEnd"sv) || TagIs(Tag, "reloadStateExit"sv))
        {
            if (Active)
            {
                EndedSighted = InSightedBolt.load() || TagIs(Tag, "MWB_BoltSightedEnd"sv);
                SetSwap(false);
            }

            SetInSights(false);
        }

        if (TagIs(Tag, "reloadState"sv) && TagIs(Event.payload, "Exit"sv) && std::exchange(EndedSighted, false))
        {
            const auto Player = RE::PlayerCharacter::GetSingleton();

            if (Player && Core::GetGunState(Player) != RE::GUN_STATE::kReloading)
            {
                if (Settings::VerboseLog())
                    REX::INFO("Sighted bolt charge: ended in the sights, reloadState Exit dropped");

                return true;
            }
        }

        if (TagIs(Tag, "reloadState"sv) && std::exchange(Starting, false) && TagIs(Event.payload, "Enter"sv) && StartedSighted)
        {
            if (Settings::VerboseLog())
                REX::INFO("Sighted bolt charge: started in the sights, reloadState Enter dropped");

            return true;
        }

        if (TagIs(Tag, "MWB_BoltSightedEnter"sv))
        {
            SetInSights(true);
            return false;
        }

        if (TagIs(Tag, "MWB_BoltSightedLeave"sv) || TagIs(Tag, "reloadStateExit"sv))
        {
            SetInSights(false);
            return false;
        }

        if (Active && InSightedBolt && TagIs(Tag, "sightedState"sv) && TagIs(Event.payload, "Exit"sv))
        {
            if (Settings::VerboseLog())
                REX::INFO("Sighted bolt charge: late sightedState Exit dropped");

            return true;
        }

        return false;
    }

    void Reset()
    {
        const std::scoped_lock Guard{ Lock };
        Starting = false;
        StartedSighted = false;
        EndedSighted = false;
        LeftBolt = false;
        InSightedBolt = false;
        Active = false;
        Outside = false;
        Mismatch = 0.0f;
        ClipMissing = 0.0f;
        LeaveSights = false;
        ShotSeen = false;
        Due = false;
        Ready = false;
        Shot = false;
        BoltClipGone = false;
        ReadyWaited = false;
        Asking = false;
        GameTook = false;
        DueAge = 0.0f;
        AskIn = 0.0f;
    }

    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        static const RE::BSFixedString BoltEvent{ "boltChargeStart" };

        if (!Accepted || Asking || Event != BoltEvent)
            return;

        GameTook = true;

        if (Settings::VerboseLog())
            REX::INFO("Sighted bolt charge: the game's own boltChargeStart taken by the graph");
    }
}
