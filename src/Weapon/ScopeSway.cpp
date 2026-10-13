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

#include "ScopeSway.h"
#include "Core/EquippedWeapon.h"
#include "Core/GunState.h"
#include "Settings/Settings.h"

namespace
{
    constexpr REL::ID UpdateAnimationID{ 740864, 2233005 };
    constexpr REL::ID CalculateAimModelWobbleID{ 211411, 2209037 };
    constexpr std::size_t ScanSize = 0x100;
    constexpr std::size_t SetterScan = 0x30;

    constexpr REL::ID CalcSteadyDrainID{ 380926, 2209045 };
    constexpr REL::ID SayID{ 1528256, 2229978 };
    constexpr std::uint32_t BreatheInSubtype = 0x6A;
    constexpr std::uint32_t BreatheOutSubtype = 0x6B;

    constexpr std::array<RE::TESFormID, 2> ScopeKeywords{ 0x0009F425, 0x001AE34C };

    constexpr float ScopeStabilityPenalty = 0.2f;

    constexpr RE::TESFormID STSQuest = 0x09029D;
    constexpr RE::TESFormID STSHoldBreathSetting = 0x0BF223;
    constexpr std::array STSPlugins{ "3dscopes.esp"sv, "3dscopes-goty.esp"sv, "3dscopes-replacer.esp"sv };

    constexpr float SteadyEventWindow = 0.25f;
    constexpr float SteadyOutOfSightsWindow = 1.5f;

    enum class Sight : std::uint8_t
    {
        None,
        VanillaScope,
        Scope3D,
        NoScope
    };

    RE::TESObjectWEAP* LastBase{ nullptr };
    RE::TESObjectWEAP::InstanceData* LastInstance{ nullptr };
    std::atomic<Sight> CurrentSight{ Sight::None };
    std::atomic<float> CurrentStabilityPenalty{ 0.0f };

    std::atomic<bool> SteadyHeld{ false };
    std::atomic<bool> SteadyPressed{ false };
    std::atomic<bool> SteadyReleased{ false };

    bool Holding{ false };
    float OutOfSightsFor{ 0.0f };
    float SteadyEventFor{ 0.0f };
    bool STSChecked{ false };

    std::atomic<bool> SightedEntered{ false };

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view TagView{ Tag.c_str() };

        return TagView.size() == Name.size() && _strnicmp(TagView.data(), Name.data(), TagView.size()) == 0;
    }

    [[nodiscard]] bool HasScopeKeyword(RE::TESObjectWEAP* Base, RE::TESObjectWEAP::InstanceData* Instance)
    {
        const auto Keywords = Instance && Instance->keywords ? Instance->keywords : static_cast<RE::BGSKeywordForm*>(Base);

        for (const auto ID : ScopeKeywords)
        {
            if (const auto Keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(ID); Keyword && Keywords->HasKeyword(Keyword))
                return true;
        }

        return false;
    }

    [[nodiscard]] float StabilityPenalty(RE::TESObjectWEAP* Base, RE::TESObjectWEAP::InstanceData* Instance)
    {
        const auto Own = Base->weaponData.aimModel;
        const auto Modded = Instance ? Instance->aimModel : Own;

        if (!Own || !Modded)
            return 0.0f;

        const auto BaseStability = Own->aimModelData.aimModelBaseStability;

        if (Modded->aimModelData.aimModelBaseStability < BaseStability - 0.0001f)
            return 0.0f;

        return ScopeStabilityPenalty * BaseStability;
    }

    void UpdateWeapon(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        const auto Base = Weapon && Weapon->Data ? Weapon->Base : nullptr;
        const auto Instance = Base ? Weapon->Instance : nullptr;

        if (Base == LastBase && Instance == LastInstance)
            return;

        LastBase = Base;
        LastInstance = Instance;

        auto Kind = Sight::None;
        float Penalty = 0.0f;

        if (Base)
        {
            const auto& Data = Instance ? *Instance : Base->weaponData;

            if (Data.flags.all(RE::WEAPON_FLAGS::kHasScope))
                Kind = Sight::VanillaScope;
            else
                Kind = HasScopeKeyword(Base, Instance) ? Sight::Scope3D : Sight::NoScope;

            Penalty = Kind == Sight::Scope3D || Kind == Sight::NoScope ? StabilityPenalty(Base, Instance) : 0.0f;
        }

        CurrentStabilityPenalty = Penalty;
        CurrentSight = Kind;

        if (Base)
        {
            constexpr std::array Names{ "no weapon"sv, "a vanilla scope"sv, "a 3D scope"sv, "no scope"sv };

            REX::INFO("Aim sway: {:08X} has {}{}", Base->GetFormID(), Names[std::to_underlying(Kind)],
                Penalty > 0.0f ? std::format(" (base stability -{:.3f})", Penalty) : ""s);
        }
    }

    [[nodiscard]] bool SwayEnabled(Sight Kind)
    {
        switch (Kind)
        {
        case Sight::VanillaScope:
            return Settings::AimWobbleVanillaScopes();
        case Sight::Scope3D:
            return Settings::AimWobble3DScopes();
        case Sight::NoScope:
            return Settings::AimWobbleNoScope();
        default:
            return false;
        }
    }

    [[nodiscard]] bool Aiming(RE::PlayerCharacter* Player)
    {
        const auto State = Core::GetGunState(Player);

        return State == RE::GUN_STATE::kSighted || State == RE::GUN_STATE::kFireSighted;
    }

    [[nodiscard]] bool OwnSight(RE::PlayerCharacter* Player)
    {
        const auto Kind = CurrentSight.load();

        return (Kind == Sight::Scope3D || Kind == Sight::NoScope) && !Player->usingScopeWithOverlay && !Player->Is3rdPersonVisible() && Aiming(Player);
    }

    [[nodiscard]] bool ThroughScope(RE::PlayerCharacter* Player)
    {
        return CurrentSight == Sight::Scope3D && OwnSight(Player);
    }

    [[nodiscard]] bool OwnSway(RE::PlayerCharacter* Player)
    {
        return OwnSight(Player) && SwayEnabled(CurrentSight);
    }

    bool Sync(RE::PlayerCharacter* Player, const RE::BSFixedString& Variable, std::int32_t Wanted)
    {
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
            return false;

        if (Current != Wanted)
            Player->SetGraphVariableInt(Variable, Wanted);

        return true;
    }

    void Say(RE::PlayerCharacter* Player, std::uint32_t Subtype)
    {
        using SaySignature = void(RE::Actor*, std::uint32_t, bool);
        static REL::Relocation<SaySignature> GameSay{ SayID };

        GameSay(Player, Subtype, false);
    }

    [[nodiscard]] float ActionPoints(RE::PlayerCharacter* Player)
    {
        const auto Values = RE::ActorValue::GetSingleton();

        return Values && Values->actionPoints ? Player->GetActorValue(*Values->actionPoints) : 0.0f;
    }

    void StartHolding(RE::PlayerCharacter* Player)
    {
        if (ActionPoints(Player) <= 0.0f)
            return;

        Player->StopCurrentDialogue(false);
        Say(Player, BreatheInSubtype);
        Player->steadyingWeapon = true;
        Holding = true;

        if (Settings::VerboseLog())
            REX::INFO("Aim sway: holding breath");
    }

    void StopHolding(RE::PlayerCharacter* Player, bool BreatheOut, std::string_view Why)
    {
        if (!Holding)
            return;

        Holding = false;

        if (!Player->usingScopeWithOverlay)
            Player->steadyingWeapon = false;

        if (BreatheOut)
            Say(Player, BreatheOutSubtype);

        if (Settings::VerboseLog())
            REX::INFO("Aim sway: breath let go ({})", Why);
    }

    void TickBreath(RE::PlayerCharacter* Player, float Delta, bool CanSteady)
    {
        const bool Pressed = SteadyPressed.exchange(false);
        const bool Released = SteadyReleased.exchange(false);

        if (Holding && Released)
            StopHolding(Player, true, "key up");

        if (Holding && !CanSteady)
        {
            if (Settings::VerboseLog() && OutOfSightsFor == 0.0f)
                REX::INFO("Aim sway: breath kept out of the sights (gun state {})", std::to_underlying(Core::GetGunState(Player)));

            OutOfSightsFor += Delta;

            if (OutOfSightsFor >= SteadyOutOfSightsWindow)
                StopHolding(Player, false, "out of the sights");

            return;
        }

        OutOfSightsFor = 0.0f;

        if (!Holding && Pressed && CanSteady && SteadyHeld)
            StartHolding(Player);

        if (!Holding)
            return;

        const auto Values = RE::ActorValue::GetSingleton();

        if (Values && Values->actionPoints && Delta > 0.0f)
        {
            using DrainSignature = float(RE::Actor*, float);
            static REL::Relocation<DrainSignature> Drain{ CalcSteadyDrainID };

            Player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *Values->actionPoints, Drain(Player, Delta));
        }

        if (ActionPoints(Player) <= 0.0f)
            StopHolding(Player, true, "out of action points");
    }

    void TickSteadyIdle(RE::PlayerCharacter* Player, float Delta)
    {
        static const RE::BSFixedString Variable{ "iMWB_ScopeSteady" };
        static const RE::BSFixedString Event{ "MWB_ScopeSteady" };

        const bool IsFirstPerson = !Player->Is3rdPersonVisible();
        const bool Scope3D = CurrentSight == Sight::Scope3D;
        Sync(Player, Variable, Scope3D && IsFirstPerson ? 1 : 0);

        if (SightedEntered.exchange(false))
            SteadyEventFor = SteadyEventWindow;

        if (SteadyEventFor <= 0.0f)
            return;

        SteadyEventFor -= Delta;

        if (!Scope3D || !IsFirstPerson)
        {
            SteadyEventFor = 0.0f;
            return;
        }

        if (Player->NotifyAnimationGraphImpl(Event))
        {
            SteadyEventFor = 0.0f;

            if (Settings::VerboseLog())
                REX::INFO("Aim sway: sighted idle held still");
        }
    }

    [[nodiscard]] float SightWobble(RE::PlayerCharacter* Player)
    {
        using CalculateSignature = float(RE::Actor*);
        static REL::Relocation<CalculateSignature> Calculate{ CalculateAimModelWobbleID };

        const auto Weapon = Core::GetEquippedWeapon(Player);
        const auto Model = Weapon && Weapon->Data ? Weapon->Data->aimModel : nullptr;
        const auto Penalty = Model ? CurrentStabilityPenalty.load() : 0.0f;
        const auto Stability = Model ? Model->aimModelData.aimModelBaseStability : 0.0f;

        if (Penalty > 0.0f)
            Model->aimModelData.aimModelBaseStability = Stability - Penalty;

        auto Wobble = Calculate(Player);

        if (Penalty > 0.0f)
            Model->aimModelData.aimModelBaseStability = Stability;

        const auto Stance = Player->stance;

        if (Stance >= 1 && Stance <= 4)
        {
            static const auto Setting = RE::GameSettingCollection::GetSingleton()->GetSetting("fAimStabilityCrouchedScopeMult");

            if (Setting)
                Wobble = std::clamp(1.0f - (1.0f - Wobble) * Setting->GetFloat(), 0.0f, 1.0f);
        }

        return Wobble;
    }

    struct SetAimWobble
    {
        static bool Thunk(RE::IAnimationGraphManagerHolder* Holder, const RE::BSFixedString& Variable, float Value)
        {
            auto Written = Value;

            if (const auto Player = RE::PlayerCharacter::GetSingleton(); Player && OwnSight(Player))
                Written = SwayEnabled(CurrentSight) ? SightWobble(Player) : 0.0f;
            else if (CurrentSight == Sight::VanillaScope && !Settings::AimWobbleVanillaScopes())
                Written = 0.0f;

            return Original(Holder, Variable, Written);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindCall(std::uintptr_t Begin, std::size_t Size, std::uintptr_t Target)
    {
        std::optional<std::uintptr_t> Found;

        for (std::size_t i = 0; i + 5 <= Size; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != 0xE8)
                continue;

            const auto Callee = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + 5) + *reinterpret_cast<const std::int32_t*>(At + 1));

            if (Callee != Target)
                continue;

            if (Found)
                return std::nullopt;

            Found = At;
        }

        return Found;
    }

    [[nodiscard]] std::uintptr_t CallTarget(std::uintptr_t Site)
    {
        return static_cast<std::uintptr_t>(static_cast<std::intptr_t>(Site + 5) + *reinterpret_cast<const std::int32_t*>(Site + 1));
    }

    [[nodiscard]] bool IsExecutable(std::uintptr_t Address)
    {
        constexpr std::uint32_t PageExecuteWriteCopy = 0x80;
        constexpr std::uint32_t ExecutableProtection = REX::W32::PAGE_EXECUTE | REX::W32::PAGE_EXECUTE_READ | REX::W32::PAGE_EXECUTE_READWRITE | PageExecuteWriteCopy;

        REX::W32::MEMORY_BASIC_INFORMATION Info{};

        if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(Address), &Info, sizeof(Info)) != sizeof(Info))
            return false;

        return Info.state == REX::W32::MEM_COMMIT && (Info.protect & ExecutableProtection) != 0;
    }

    [[nodiscard]] bool IsForeignHook(std::uintptr_t Callee)
    {
        const auto Text = REX::FModule::GetExecutingModule().GetSection(".text");
        const auto InGameCode = Callee >= Text.GetAddress() && Callee < Text.GetAddress() + Text.GetSize();

        return !InGameCode && IsExecutable(Callee);
    }

    using ArgumentsSignature = bool(RE::BSScrapArray<RE::BSScript::Variable>&);

    class NoArguments
    {
    public:
        virtual NoArguments* Copy(void*) const { return nullptr; }
        virtual NoArguments* Move(void*) noexcept { return nullptr; }
        virtual bool Call(RE::BSScrapArray<RE::BSScript::Variable>&) { return true; }
        virtual const std::type_info& TargetType() const noexcept { return typeid(void); }
        virtual void DeleteThis(bool) noexcept {}
        virtual const void* Get() const noexcept { return nullptr; }
    };

    struct GameArguments
    {
        static constexpr std::size_t OGFunctorSlot = 3;
        static constexpr std::size_t FunctorSlot = 7;

        explicit GameArguments(NoArguments* Functor) noexcept
        {
            Slots[OGFunctorSlot] = Functor;
            Slots[FunctorSlot] = Functor;
        }

        [[nodiscard]] const RE::BSTThreadScrapFunction<ArgumentsSignature>& AsFunction() const noexcept
        {
            return *reinterpret_cast<const RE::BSTThreadScrapFunction<ArgumentsSignature>*>(this);
        }

        alignas(RE::BSTThreadScrapFunction<ArgumentsSignature>) std::array<void*, 8> Slots{};
    };

    static_assert(sizeof(GameArguments) == sizeof(RE::BSTThreadScrapFunction<ArgumentsSignature>));

    [[nodiscard]] bool CallWithoutArguments(RE::BSScript::IVirtualMachine& VM, std::uint64_t Handle, const RE::BSFixedString& Script, const RE::BSFixedString& Function)
    {
        NoArguments Functor;
        const GameArguments Arguments{ &Functor };

        return VM.DispatchMethodCall(Handle, Script, Function, Arguments.AsFunction(), RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>{});
    }

    [[nodiscard]] std::optional<std::uintptr_t> FindSetter(std::uintptr_t Begin, std::uintptr_t Vanilla)
    {
        if (const auto Direct = FindCall(Begin, SetterScan, Vanilla))
            return Direct;

        for (std::size_t i = 0; i + 5 <= SetterScan; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) == 0xE8 && IsForeignHook(CallTarget(At)))
                return At;
        }

        return std::nullopt;
    }
}

namespace ScopeSway
{
    void Install()
    {
        const auto Function = REL::Relocation<std::uintptr_t>{ UpdateAnimationID }.address();
        const auto Target = REL::Relocation<std::uintptr_t>{ CalculateAimModelWobbleID }.address();
        const auto Wobble = FindCall(Function, ScanSize, Target);

        if (!Wobble)
        {
            REX::WARN("The aim wobble call was not found in {:X} (another mod may have it). The aim sway options do nothing.", Function);
            return;
        }

        const auto Vanilla = REL::Relocation<std::uintptr_t>{ RE::ID::IAnimationGraphManagerHolder::SetGraphVariableFloat }.address();
        const auto Setter = FindSetter(*Wobble + 5, Vanilla);

        if (!Setter)
        {
            REX::WARN("The AimWobble setter was not found after {:X}. The aim sway options do nothing.", *Wobble);
            return;
        }

        if (CallTarget(*Setter) != Vanilla)
            REX::INFO("The AimWobble setter at {:X} already goes to {:X}, not the game's {:X} (another mod); chaining to it", *Setter, CallTarget(*Setter), Vanilla);

        SetAimWobble::Original = REL::GetTrampoline().write_call<5>(*Setter, SetAimWobble::Thunk);
        REX::INFO("Aim sway hook installed at {:X} (chaining to {:X})", *Setter, SetAimWobble::Original.address());
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        std::optional<Core::EquippedWeapon> Weapon;

        if (Core::TryGetEquippedWeapon(Player, Weapon))
            UpdateWeapon(Weapon);

        const bool IsThroughScope = ThroughScope(Player);

        if (IsThroughScope && !STSChecked)
        {
            STSChecked = true;
            DisableSeeThroughScopesHoldBreath();
        }

        TickBreath(Player, Delta, OwnSway(Player));
        TickSteadyIdle(Player, Delta);
    }

    void OnAnimationEvent(const RE::BSFixedString& Tag)
    {
        if (!Tag.empty() && TagIs(Tag, "sightedStateEnter"sv))
            SightedEntered = true;
    }

    void OnButton(const RE::ButtonEvent& Event)
    {
        const auto Map = RE::ControlMap::GetSingleton();

        if (!Map)
            return;

        const auto Device = Event.device.get();

        if (Map->GetMappedKey("Steady"sv, Device, RE::UserEvents::INPUT_CONTEXT_ID::kScope) != static_cast<std::uint32_t>(Event.idCode))
            return;

        if (Event.QJustPressed())
        {
            SteadyHeld = true;
            SteadyPressed = true;
        }
        else if (Event.value == 0.0f)
        {
            SteadyHeld = false;
            SteadyReleased = true;
        }
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (!SteadyHeld)
            return false;

        static const RE::BSFixedString Gun{ "meleeAttackGun" };
        static const RE::BSFixedString Bayonet{ "meleeAttackBayonet" };

        if (Event != Gun && Event != Bayonet)
            return false;

        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player || !OwnSway(Player))
            return false;

        if (Settings::VerboseLog())
            REX::INFO("Aim sway: {} refused, holding the Steady key", Event.c_str());

        return true;
    }

    void DisableSeeThroughScopesHoldBreath()
    {
        const auto Handler = RE::TESDataHandler::GetSingleton();

        if (!Handler)
            return;

        RE::TESGlobal* Setting = nullptr;
        RE::TESQuest* Quest = nullptr;

        for (const auto Plugin : STSPlugins)
        {
            Setting = Handler->LookupForm<RE::TESGlobal>(STSHoldBreathSetting, Plugin);
            Quest = Handler->LookupForm<RE::TESQuest>(STSQuest, Plugin);

            if (Setting && Quest)
                break;
        }

        if (!Setting || !Quest || Setting->GetValue() == 0.0f)
            return;

        Setting->value = 0.0f;

        const auto Game = RE::GameVM::GetSingleton();
        const auto VM = Game ? Game->GetVM() : nullptr;
        bool Called = false;

        if (VM)
        {
            const auto& Handles = VM->GetObjectHandlePolicy();
            const auto Handle = Handles.GetHandleForObject(static_cast<std::uint32_t>(RE::ENUM_FORM_ID::kQUST), Quest);

            if (Handle != Handles.EmptyHandle())
            {
                Called = CallWithoutArguments(*VM, Handle, RE::BSFixedString{ "SeeThroughScopesController" }, RE::BSFixedString{ "updateHoldBreathSetting" });
            }
        }

        REX::INFO("See Through Scopes' hold breath turned off (the plugin does it now){}", Called ? "" : "; its script was not reached, it takes over from the next holotape change");
    }

    void Reset()
    {
        if (const auto Player = RE::PlayerCharacter::GetSingleton(); Player && Holding)
            StopHolding(Player, false, "loading a save");

        Holding = false;
        OutOfSightsFor = 0.0f;
        SteadyEventFor = 0.0f;
        STSChecked = false;
        SteadyHeld = false;
        SteadyPressed = false;
        SteadyReleased = false;
        SightedEntered = false;
        LastBase = nullptr;
        LastInstance = nullptr;
        CurrentSight = Sight::None;
        CurrentStabilityPenalty = 0.0f;
    }
}
