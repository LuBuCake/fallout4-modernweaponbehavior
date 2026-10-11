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

#include "SprintFire.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    using Holder = RE::IAnimationGraphManagerHolder;

    constexpr std::size_t HolderVtableIndex = 5;
    constexpr std::size_t HolderOffset = 0x48;
    constexpr std::size_t GetGraphVariableBoolSlot = 0x11;
    constexpr std::uint32_t IsSprintingId = 0x0B;

    constexpr std::uint16_t SprintingBit = 0x100;

    constexpr REL::ID AttackButtonID{ 300076, 2234896 };
    constexpr REL::ID AttackPerFrameID{ 788179, 2234900 };
    constexpr REL::ID AttackIsSprintingID{ 1516567, 2230409 };
    constexpr REL::ID PlayerUpdateID{ 263152, 2232920 };
    constexpr REL::ID SprintUpdateID{ 153632, 2233272 };

    constexpr std::size_t  ScanSize = 0x600;
    constexpr std::size_t  PlayerUpdateScanSize = 0x2000;
    constexpr std::uint8_t CallOpcode = 0xE8;
    constexpr std::size_t  CallInstructionSize = 5;

    constexpr float MinimumForwardInput = 0.1f;

    constexpr std::ptrdiff_t FirstPersonCameraControl = 0x58;
    constexpr std::size_t    CameraStateUpdateSlot = 0x0B;
    constexpr float CameraBlendOut = 0.3f;

    constexpr float FireWaitTimeout = 0.75f;
    constexpr float RetryWindow = 0.5f;
    constexpr float ResumeDelay = 0.1f;
    constexpr float ResumeTimeout = 1.0f;
    constexpr float BusyTimeout = 10.0f;

    enum class Phase : std::uint8_t
    {
        kOff,
        kFiring,
        kResuming
    };

    std::atomic<Phase> Current{ Phase::kOff };
    std::atomic<bool>  GraphSprinting{ false };
    std::atomic<bool>  IdChecked{ false };
    std::atomic<bool>  UpdateSkipped{ false };
    float              Waited{ 0.0f };
    float              CameraShare{ 0.0f };
    float              SinceAttack{ 0.0f };
    std::atomic<bool>  FireStateEntered{ false };
    std::atomic<bool>  Retrying{ false };
    bool               RetryPending{ false };
    float              SinceResume{ RetryWindow };
    RE::BSFixedString  RetryEvent;
    std::atomic<float> SprintFov{ 0.0f };
    std::atomic<float> FovShare{ 0.0f };
    std::atomic<bool>  HaveSprintFov{ false };
    bool               SawFire{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] bool TagStarts(const RE::BSFixedString& Tag, std::string_view Prefix)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() >= Prefix.size() && _strnicmp(Text.data(), Prefix.data(), Prefix.size()) == 0;
    }

    [[nodiscard]] std::uint16_t MoveMode(const RE::PlayerCharacter* Player)
    {
        return static_cast<std::uint16_t>(Player->moveMode);
    }

    [[nodiscard]] bool WantsSprint(const RE::PlayerCharacter* Player)
    {
        return Player->sprintToggled;
    }

    [[nodiscard]] bool InFirstPerson(RE::PlayerCharacter* Player)
    {
        return !Player->Is3rdPersonVisible();
    }

    void SetCameraShare(float Share)
    {
        const float Previous = FovShare.exchange(Share);

        if ((Previous == 0.0f) != (Share == 0.0f) && Settings::VerboseLog())
            REX::INFO("Sprint fire: sprint field of view {} (Camera Control Z {:.2f})", Share > 0.0f ? "held" : "released", SprintFov.load());
    }

    [[nodiscard]] RE::NiAVObject* CameraControl(RE::TESCameraState* State)
    {
        return State ? *reinterpret_cast<RE::NiAVObject**>(reinterpret_cast<std::uintptr_t>(State) + FirstPersonCameraControl) : nullptr;
    }

    struct FirstPersonUpdate
    {
        static void Thunk(RE::TESCameraState* This, RE::BSTSmartPointer<RE::TESCameraState>& Next)
        {
            if (const auto Node = CameraControl(This))
            {
                auto& Z = Node->local.translate.z;
                const float Share = FovShare.load();

                if (Share > 0.0f && HaveSprintFov)
                    Z += (SprintFov.load() - Z) * Share;
                else if (Current.load() == Phase::kOff && GraphSprinting)
                {
                    SprintFov = Z;
                    HaveSprintFov = true;
                }
            }

            Original(This, Next);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] bool HasGunOut(RE::PlayerCharacter* Player)
    {
        if (!Player->GetWeaponMagicDrawn())
            return false;

        const auto Weapon = Core::GetEquippedWeapon(Player);

        return Weapon && Weapon->Base && Weapon->Base->weaponData.type.get() == RE::WEAPON_TYPE::kGun;
    }

    [[nodiscard]] bool Eligible(RE::PlayerCharacter* Player)
    {
        return (MoveMode(Player) & SprintingBit) != 0 && WantsSprint(Player) && HasGunOut(Player) && InFirstPerson(Player);
    }

    void End(std::string_view Why)
    {
        if (Current.exchange(Phase::kOff) == Phase::kOff)
            return;

        Waited = 0.0f;

        if (Settings::VerboseLog())
            REX::INFO("Sprint fire: off, {}", Why);
    }

    void CheckId(Holder* This, bool Cached)
    {
        if (IdChecked.exchange(true))
            return;

        static const RE::BSFixedString Variable{ "IsSprinting" };
        bool Named = false;

        if (!This->GetGraphVariableImplBool(Variable, Named))
        {
            IdChecked = false;
            return;
        }

        if (Named != Cached)
            REX::WARN("Sprint fire: graph variable cache id {} reads {}, IsSprinting reads {}. The cache order is not the expected one.", IsSprintingId, Cached, Named);
        else if (Settings::VerboseLog())
            REX::INFO("Sprint fire: graph variable cache id {} matches IsSprinting ({})", IsSprintingId, Named);
    }

    struct GetGraphVariableBool
    {
        static bool Thunk(Holder* This, std::uint32_t Id, bool& Out)
        {
            const bool Result = Original(This, Id, Out);

            if (Id != IsSprintingId || !Result)
                return Result;

            GraphSprinting = Out;

            if (Current.load() == Phase::kOff)
                return Result;

            CheckId(This, Out);

            const auto Player = RE::PlayerCharacter::GetSingleton();

            if (!Out && Player && WantsSprint(Player))
                Out = true;

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    template <std::size_t N>
    struct AttackIsSprinting
    {
        static bool Thunk(RE::Actor* Actor)
        {
            const bool Sprinting = Original(Actor);

            if (!Sprinting || !Settings::FireWhileSprinting())
                return Sprinting;

            const auto Player = RE::PlayerCharacter::GetSingleton();

            if (Actor != Player || !InFirstPerson(Player) || !HasGunOut(Player))
                return Sprinting;

            if (Current.load() == Phase::kOff && Settings::VerboseLog())
                REX::INFO("Sprint fire: the {} sees the player not sprinting", N == 0 ? "attack button"sv : "queued attack"sv);

            return false;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCall(std::uintptr_t Begin, std::uintptr_t Target, std::size_t Size)
    {
        std::optional<std::uintptr_t> Found;

        for (std::size_t i = 0; i + CallInstructionSize <= Size; ++i)
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

    struct SprintUpdate
    {
        static void Thunk(RE::PlayerCharacter* This)
        {
            if (Current.load() == Phase::kFiring && Settings::FireWhileSprinting() && This && HasGunOut(This))
            {
                if (!UpdateSkipped.exchange(true) && Settings::VerboseLog())
                    REX::INFO("Sprint fire: the sprint update waits for the shot");

                return;
            }

            Original(This);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCheck(REL::ID Caller, REL::ID Callee, std::string_view Name, std::size_t Size = ScanSize)
    {
        const auto Target = REL::Relocation<std::uintptr_t>{ Callee }.address();
        const auto Site = FindCall(REL::Relocation<std::uintptr_t>{ Caller }.address(), Target, Size);

        if (!Site)
            REX::WARN("Sprint fire: the {} was not found (another mod may have it).", Name);

        return Site;
    }

    template <std::size_t N>
    void InstallAttackCheck(std::uintptr_t Site, std::string_view Name)
    {
        AttackIsSprinting<N>::Original = REL::GetTrampoline().write_call<5>(Site, AttackIsSprinting<N>::Thunk);
        REX::INFO("Sprint fire: sprint check of the {} at {:X}, chaining to {:X}", Name, Site, AttackIsSprinting<N>::Original.address());
    }
    void RetryShot(RE::PlayerCharacter* Player)
    {
        if (!RetryPending)
            return;

        const bool Firing = Player->gunState == RE::GUN_STATE::kFire || Player->gunState == RE::GUN_STATE::kFireSighted;

        if (FireStateEntered || Firing || SinceResume >= RetryWindow || !Settings::FireWhileSprinting() || !InFirstPerson(Player) || !HasGunOut(Player))
        {
            RetryPending = false;

            if (!FireStateEntered && !Firing && Settings::VerboseLog())
                REX::INFO("Sprint fire: {} never reached a fire state, given up", RetryEvent.c_str());

            return;
        }

        FireStateEntered = false;
        Retrying = true;
        Player->NotifyAnimationGraphImpl(RetryEvent);
        Retrying = false;

        if (FireStateEntered)
        {
            RetryPending = false;

            if (Settings::VerboseLog())
                REX::INFO("Sprint fire: {} sent again, the shot reached the fire state", RetryEvent.c_str());
        }
    }
}

namespace SprintFire
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::PlayerCharacter[HolderVtableIndex] };

        if (const auto Player = RE::PlayerCharacter::GetSingleton())
        {
            const auto Live = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(Player) + HolderOffset);

            if (Live != Vtable.address())
            {
                REX::ERROR("Sprint fire: animation graph holder vtable mismatch (id {:X}, live {:X}). Firing while sprinting disabled.", Vtable.address(), Live);
                return;
            }
        }

        const auto ButtonSite = FindCheck(AttackButtonID, AttackIsSprintingID, "sprint check of the attack button"sv);
        const auto QueuedSite = FindCheck(AttackPerFrameID, AttackIsSprintingID, "sprint check of the queued attack"sv);
        const auto UpdateSite = FindCheck(PlayerUpdateID, SprintUpdateID, "player's sprint update"sv, PlayerUpdateScanSize);

        if (!ButtonSite || !QueuedSite || !UpdateSite)
        {
            REX::ERROR("Sprint fire: firing while sprinting disabled.");
            return;
        }

        InstallAttackCheck<0>(*ButtonSite, "attack button"sv);
        InstallAttackCheck<1>(*QueuedSite, "queued attack"sv);
        SprintUpdate::Original = REL::GetTrampoline().write_call<5>(*UpdateSite, SprintUpdate::Thunk);
        REX::INFO("Sprint fire: player's sprint update at {:X}, chaining to {:X}", *UpdateSite, SprintUpdate::Original.address());

        REL::Relocation<std::uintptr_t> CameraVtable{ RE::VTABLE::FirstPersonState[0] };
        FirstPersonUpdate::Original = CameraVtable.write_vfunc(CameraStateUpdateSlot, FirstPersonUpdate::Thunk);

        GetGraphVariableBool::Original = Vtable.write_vfunc(GetGraphVariableBoolSlot, GetGraphVariableBool::Thunk);
        REX::INFO("Sprint fire hook installed");
    }

    void OnEventSending(const RE::BSFixedString& Event)
    {
        if (!GetGraphVariableBool::Original.address() || !TagStarts(Event, "attackStart"sv) || !Settings::FireWhileSprinting())
            return;

        FireStateEntered = false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !Eligible(Player))
            return;

        CameraShare = 1.0f;
        SetCameraShare(CameraShare);
    }

    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        if (!GetGraphVariableBool::Original.address() || !TagStarts(Event, "attackStart"sv))
            return;

        if (!Settings::FireWhileSprinting())
            return;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
            return;

        const auto Mode = MoveMode(Player);
        const bool GunOut = HasGunOut(Player);

        if (Settings::VerboseLog())
        {
            REX::INFO("Sprint fire: {} {}, sprinting {}, sprint toggled {}, gun out {}, graph sprinting {}", Event.c_str(), Accepted ? "accepted" : "refused",
                (Mode & SprintingBit) != 0, WantsSprint(Player), GunOut, GraphSprinting.load());
        }

        if (!Accepted || !Eligible(Player))
            return;

        if (!FireStateEntered && !Retrying && SinceResume < RetryWindow)
        {
            RetryPending = true;
            RetryEvent = Event;

            if (Settings::VerboseLog())
                REX::INFO("Sprint fire: {} did not reach a fire state (the graph is still blending into the sprint), sending it again", Event.c_str());
        }

        Waited = 0.0f;
        SinceAttack = 0.0f;
        SawFire = false;
        UpdateSkipped = false;

        if (Current.exchange(Phase::kFiring) == Phase::kOff && Settings::VerboseLog())
            REX::INFO("Sprint fire: {} while sprinting, the sprint goes on", Event.c_str());
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        const auto Now = Current.load();

        if (!Player)
            return;

        SinceResume = (std::min)(SinceResume + Delta, RetryWindow);
        RetryShot(Player);

        if (Now == Phase::kOff)
        {
            if (CameraShare > 0.0f)
            {
                CameraShare = Delta > 0.0f ? (std::max)(0.0f, CameraShare - Delta / CameraBlendOut) : CameraShare;
                SetCameraShare(CameraShare);
            }

            return;
        }

        if (!InFirstPerson(Player))
        {
            End("third person");
            CameraShare = 0.0f;
            SetCameraShare(CameraShare);
            return;
        }

        if (!Settings::FireWhileSprinting())
        {
            End("option turned off");
            return;
        }

        if (!WantsSprint(Player))
        {
            End("the sprint was stopped");
            return;
        }

        if (!HasGunOut(Player))
        {
            End("no gun out");
            return;
        }

        if (Now == Phase::kFiring)
        {
            const auto Controls = RE::PlayerControls::GetSingleton();

            if (Controls && Controls->data.moveInputVec.y < MinimumForwardInput)
            {
                End("not moving forward");
                return;
            }

            const auto Values = RE::ActorValue::GetSingleton();

            if (Values && Values->actionPoints && Player->GetActorValue(*Values->actionPoints) <= 0.0f)
            {
                End("out of action points");
                return;
            }
        }

        if (Now == Phase::kResuming)
        {
            if (GraphSprinting)
            {
                End("the graph is sprinting again");
                return;
            }

            Waited += Delta;

            if (Waited >= ResumeTimeout)
                End("the graph did not go back to the sprint");

            return;
        }

        switch (Player->gunState)
        {
            case RE::GUN_STATE::kFire:
            case RE::GUN_STATE::kFireSighted:
                Waited = 0.0f;
                SawFire = true;
                return;
            case RE::GUN_STATE::kSighted:
                End("aiming");
                return;
            case RE::GUN_STATE::kReloading:
            case RE::GUN_STATE::kThrowing:
                Waited += Delta;

                if (Waited >= BusyTimeout)
                    End("busy for too long");

                return;
            default:
                break;
        }

        SinceAttack += Delta;

        if (!SawFire && SinceAttack < FireWaitTimeout)
            return;

        Waited += Delta;

        if (Waited < ResumeDelay)
            return;

        static const RE::BSFixedString SprintEvent{ "sprintStart" };
        const bool Resumed = Player->NotifyAnimationGraphImpl(SprintEvent);

        if (Settings::VerboseLog())
            REX::INFO("Sprint fire: done firing, sprintStart {}", Resumed ? "accepted" : "refused");

        if (Resumed)
        {
            SinceResume = 0.0f;
            Current = Phase::kResuming;
            Waited = 0.0f;
        }
        else if (Waited >= ResumeTimeout)
        {
            End("the graph refused sprintStart");
        }
    }

    void Reset()
    {
        Current = Phase::kOff;
        Waited = 0.0f;
        CameraShare = 0.0f;
        RetryPending = false;
        SinceResume = RetryWindow;
        FovShare = 0.0f;
        HaveSprintFov = false;
    }

    void OnAnimationEvent(const RE::BSAnimationGraphEvent& Event)
    {
        if (!TagIs(Event.tag, "attackState"sv))
            return;

        if (Current.load() != Phase::kOff && Settings::VerboseLog())
            REX::INFO("Sprint fire: attackState {}", Event.payload.c_str());

        if (TagIs(Event.payload, "Enter"sv))
            FireStateEntered = true;
    }
}
