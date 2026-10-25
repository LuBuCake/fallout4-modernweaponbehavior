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

#include "Slide.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

#include <numbers>

namespace
{
    constexpr float CrouchTimeout = 0.2f;

    constexpr std::uint16_t SprintingBit = 0x100;
    constexpr std::uint32_t SneakingStance = 1;

    constexpr float Pi = std::numbers::pi_v<float>;

    constexpr std::size_t ButtonEventSlot = 0x08;

    constexpr std::size_t    AgentVtableIndex = 1;
    constexpr std::size_t    SmallDeltaSlot = 0x02;
    constexpr std::ptrdiff_t SmallDeltaOutputOffset = 0x08;
    constexpr std::size_t    OutputHeading = 2;
    constexpr std::size_t    OutputSpeed = 8;

    constexpr std::string_view PluginName = "Modern Weapon Behavior.esp"sv;
    constexpr std::uint16_t    LoopFadeOut = 120;
    constexpr REL::ID          GetLandTextureID{ 549652, 2192076 };
    constexpr std::uint32_t    SoundUsageFlags = 0x10;

    enum class SoundPart : std::uint8_t
    {
        kStart,
        kLoop,

        kCount
    };

    constexpr std::array<std::uint32_t, static_cast<std::size_t>(SoundPart::kCount)>    ImpactSetIDs{ 0x804, 0x805 };
    constexpr std::array<std::string_view, static_cast<std::size_t>(SoundPart::kCount)> SoundPartNames{ "start"sv, "loop"sv };

    constexpr std::array RefusedPrefixes{ "attack"sv, "bash"sv, "block"sv, "reload"sv, "throw"sv, "rifleSightedStart"sv, "boltCharge"sv };

    enum class Phase : std::uint8_t
    {
        kOff,
        kCommitted,
        kSliding
    };

    std::atomic<Phase> Current{ Phase::kOff };
    std::atomic<bool>  SwallowRelease{ false };
    float              Elapsed{ 0.0f };
    float              Duration{ 0.0f };
    float              Commitment{ 0.0f };
    std::atomic<float> Progress{ 0.0f };
    bool               PlayPending{ false };
    std::atomic<bool>  OwnClip{ false };
    bool               Played{ false };
    bool               Crouched{ false };
    bool               SoundPending{ false };
    bool               SoundsStarted{ false };
    RE::BSSoundHandle  LoopHandle;

    std::array<RE::BGSImpactDataSet*, static_cast<std::size_t>(SoundPart::kCount)> ImpactSets{};
    bool                                                                          ImpactSetsSearched{ false };
    std::unordered_map<std::uint32_t, RE::BGSMaterialType*> Materials;

    std::atomic<float> SprintHeading{ 0.0f };
    std::atomic<float> SprintSpeed{ 0.0f };
    std::atomic<bool>  HaveSprintMovement{ false };
    std::atomic<float> StartHeading{ 0.0f };
    std::atomic<float> StartSpeed{ 0.0f };

    [[nodiscard]] bool TagStarts(const RE::BSFixedString& Tag, std::string_view Prefix)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() >= Prefix.size() && _strnicmp(Text.data(), Prefix.data(), Prefix.size()) == 0;
    }

    [[nodiscard]] bool InFirstPerson(RE::PlayerCharacter* Player)
    {
        return !Player->Is3rdPersonVisible();
    }

    [[nodiscard]] bool Sprinting(const RE::PlayerCharacter* Player)
    {
        return (static_cast<std::uint16_t>(Player->moveMode) & SprintingBit) != 0;
    }

    [[nodiscard]] bool Sneaking(const RE::PlayerCharacter* Player)
    {
        return Player->stance == SneakingStance;
    }

    bool Send(RE::PlayerCharacter* Player, const char* Event)
    {
        const bool Accepted = Player->NotifyAnimationGraphImpl(RE::BSFixedString{ Event });

        if (Settings::VerboseLog())
            REX::INFO("Slide: {} {}", Event, Accepted ? "accepted" : "refused");

        return Accepted;
    }

    [[nodiscard]] RE::BGSImpactDataSet* ImpactSet(SoundPart Part)
    {
        if (!ImpactSetsSearched)
        {
            ImpactSetsSearched = true;

            for (std::size_t i = 0; i < ImpactSets.size(); ++i)
            {
                const auto Form = WeaponConfig::LookupPluginForm(PluginName, ImpactSetIDs[i]);
                ImpactSets[i] = Form ? Form->As<RE::BGSImpactDataSet>() : nullptr;

                if (!ImpactSets[i])
                    REX::WARN("Slide: the {} sound set ({:03X} in {}) not found. The slide plays no {} sound.", SoundPartNames[i], ImpactSetIDs[i], PluginName, SoundPartNames[i]);
            }
        }

        return ImpactSets[static_cast<std::size_t>(Part)];
    }

    [[nodiscard]] RE::BGSMaterialType* MaterialFromID(std::uint32_t ID)
    {
        if (Materials.empty())
        {
            if (const auto DataHandler = RE::TESDataHandler::GetSingleton())
            {
                for (const auto Material : DataHandler->GetFormArray<RE::BGSMaterialType>())
                {
                    if (Material)
                        Materials.emplace(Material->materialID, Material);
                }
            }
        }

        const auto Found = Materials.find(ID);

        return Found != Materials.end() ? Found->second : nullptr;
    }

    [[nodiscard]] RE::TESLandTexture* LandTexture(RE::NiPoint3& Position)
    {
        using GetLandTexture_t = RE::TESLandTexture* (*)(RE::TES*, RE::NiPoint3&);
        static REL::Relocation<GetLandTexture_t> GetLandTexture{ GetLandTextureID };
        const auto Tes = RE::TES::GetSingleton();

        return Tes ? GetLandTexture(Tes, Position) : nullptr;
    }

    [[nodiscard]] RE::BGSMaterialType* GroundMaterial(RE::PlayerCharacter* Player)
    {
        const auto Process = Player->currentProcess;
        const auto Controller = Process && Process->middleHigh ? Process->middleHigh->charController.get() : nullptr;

        if (Controller && Controller->supportMaterial != 0)
        {
            if (const auto Material = MaterialFromID(Controller->supportMaterial))
                return Material;
        }

        const auto Cell = Player->GetParentCell();

        if (!Cell || !Cell->IsExterior())
            return nullptr;

        auto       Position = Player->GetPosition();
        const auto Texture = LandTexture(Position);

        return Texture ? Texture->materialType : nullptr;
    }

    [[nodiscard]] RE::BGSSoundDescriptorForm* SoundFor(SoundPart Part, RE::BGSMaterialType* Ground)
    {
        const auto Set = ImpactSet(Part);
        RE::BGSImpactData* Impact = nullptr;

        for (auto Material = Ground; Set && Material && !Impact; Material = Material->parentType)
        {
            if (const auto Found = Set->impactMap.find(Material); Found != Set->impactMap.end())
                Impact = Found->second;
        }

        return Impact ? (Impact->sound2 ? Impact->sound2 : Impact->sound1) : nullptr;
    }

    bool PlaySound(RE::PlayerCharacter* Player, SoundPart Part, RE::BGSMaterialType* Ground, RE::BSSoundHandle& Handle)
    {
        const auto Sound = SoundFor(Part, Ground);
        const auto Audio = RE::BSAudioManager::GetSingleton();
        bool SoundPlayed = false;

        if (Sound && Audio && Audio->GetSoundHandle(Handle, Sound, 0.0f, SoundUsageFlags) && Handle.IsValid())
        {
            Handle.SetPosition(Player->GetPosition());
            SoundPlayed = Handle.Play();
        }

        if (Settings::VerboseLog())
        {
            REX::INFO("Slide: {} sound on {} ({:08X}): {:08X} {}", SoundPartNames[static_cast<std::size_t>(Part)],
                Ground ? Ground->materialName.c_str() : "unknown ground", Ground ? Ground->GetFormID() : 0u, Sound ? Sound->GetFormID() : 0u,
                SoundPlayed ? "played" : "not played");
        }

        return SoundPlayed;
    }

    void StartSounds(RE::PlayerCharacter* Player)
    {
        const auto Ground = GroundMaterial(Player);
        RE::BSSoundHandle Start;

        PlaySound(Player, SoundPart::kStart, Ground, Start);
        PlaySound(Player, SoundPart::kLoop, Ground, LoopHandle);
        SoundsStarted = true;
    }

    void StopSounds()
    {
        if (!SoundsStarted)
            return;

        SoundsStarted = false;

        if (LoopHandle.IsValid())
            LoopHandle.FadeOutAndRelease(LoopFadeOut);

        LoopHandle = {};
    }

    void End(RE::PlayerCharacter* Player, std::string_view Why)
    {
        if (Current.exchange(Phase::kOff) == Phase::kOff)
            return;

        if (Played && Player)
            Send(Player, "MWB_SlideEnd");

        StopSounds();

        Elapsed = 0.0f;
        Progress = 0.0f;
        PlayPending = false;
        OwnClip = false;
        Played = false;
        Crouched = false;

        if (Settings::VerboseLog())
            REX::INFO("Slide: off, {}", Why);
    }

    void ToggleSneak(RE::PlayerInputHandler* Handler, const RE::ButtonEvent& Pressed, auto&& Original)
    {
        RE::ButtonEvent Release{ Pressed };
        Release.value = 0.0f;
        Release.heldDownSecs = 0.0f;
        Original(Handler, &Release);
    }

    [[nodiscard]] bool HasClip(RE::PlayerCharacter* Player)
    {
        const auto Weapon = Core::GetEquippedWeapon(Player);

        return Weapon && Weapon->Base && Weapon->Data && WeaponConfig::MatchSlide(Weapon->Base, Weapon->Instance);
    }

    [[nodiscard]] bool CanStart(RE::PlayerCharacter* Player)
    {
        if (!Settings::Slide() || !Player || !InFirstPerson(Player) || !Player->sprintToggled || !Sprinting(Player) || Sneaking(Player) ||
            !HaveSprintMovement)
            return false;

        StartHeading = SprintHeading.load();
        StartSpeed = SprintSpeed.load() * Settings::SlideSpeed();

        return true;
    }

    [[nodiscard]] float Wrap(float Angle)
    {
        return std::remainder(Angle, 2.0f * Pi);
    }

    [[nodiscard]] std::string_view VtableClassName(std::uintptr_t Vtable)
    {
        struct CompleteObjectLocator
        {
            std::uint32_t Signature;
            std::uint32_t Offset;
            std::uint32_t CdOffset;
            std::uint32_t TypeDescriptor;
        };

        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator)
            return {};

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*));
    }

    struct UpdateSmallDelta
    {
        static void Thunk(void* This, void* Data)
        {
            Original(This, Data);

            const auto Output = Data ? *reinterpret_cast<float**>(reinterpret_cast<std::uintptr_t>(Data) + SmallDeltaOutputOffset) : nullptr;

            if (!Output)
                return;

            const auto Player = RE::PlayerCharacter::GetSingleton();

            if (!Player)
                return;

            auto& Heading = Output[OutputHeading];
            auto& Speed = Output[OutputSpeed];
            const float Facing = Player->data.angle.z;

            if (Current.load() == Phase::kOff)
            {
                if (Sprinting(Player) && Speed > 0.0f)
                {
                    SprintHeading = Wrap(Heading + Facing);
                    SprintSpeed = Speed;
                    HaveSprintMovement = true;
                }

                return;
            }

            const float Time = Progress.load();
            const float Share = 1.0f - Time * Time;

            if (Share <= 0.0f)
                return;

            Speed += (std::max)(0.0f, StartSpeed.load() - Speed) * Share;
            Heading = Wrap(StartHeading.load() - Facing);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct SneakButton
    {
        static void Thunk(RE::PlayerInputHandler* This, const RE::ButtonEvent* Event)
        {
            if (!Event)
            {
                Original(This, Event);
                return;
            }

            auto& Handled = const_cast<RE::ButtonEvent*>(Event)->handled;

            if (Event->QJustPressed())
            {
                const auto Now = Current.load();

                if (Now == Phase::kCommitted)
                {
                    SwallowRelease = true;
                    Handled = RE::InputEvent::HANDLED_RESULT::kStop;
                    return;
                }

                if (Now == Phase::kSliding)
                {
                    ToggleSneak(This, *Event, Original);
                    SwallowRelease = true;
                    Handled = RE::InputEvent::HANDLED_RESULT::kStop;
                    End(RE::PlayerCharacter::GetSingleton(), "cancelled");
                    return;
                }

                if (CanStart(RE::PlayerCharacter::GetSingleton()))
                {
                    ToggleSneak(This, *Event, Original);

                    Elapsed = 0.0f;
                    Duration = Settings::SlideDuration();
                    Commitment = Settings::SlideCommittedTime();
                    Progress = 0.0f;
                    PlayPending = HasClip(RE::PlayerCharacter::GetSingleton());
                    OwnClip = PlayPending;
                    Played = false;
                    Crouched = false;
                    SoundPending = true;
                    Current = Phase::kCommitted;
                    SwallowRelease = true;
                    Handled = RE::InputEvent::HANDLED_RESULT::kStop;

                    if (Settings::VerboseLog())
                        REX::INFO("Slide: on{}", PlayPending ? "" : ", no clip of its own");

                    return;
                }
            }
            else if (Event->value == 0.0f && SwallowRelease.exchange(false))
            {
                Handled = RE::InputEvent::HANDLED_RESULT::kStop;
                return;
            }

            Original(This, Event);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };
}

namespace Slide
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::SneakHandler[0] };
        const auto Controls = RE::PlayerControls::GetSingleton();
        const auto Live = Controls && Controls->sneakHandler ? *reinterpret_cast<std::uintptr_t*>(Controls->sneakHandler) : 0;

        if (Live != Vtable.address())
        {
            REX::ERROR("Slide: sneak handler vtable mismatch (id {:X}, live {:X}). Slide disabled.", Vtable.address(), Live);
            return;
        }

        REL::Relocation<std::uintptr_t> AgentVtable{ RE::VTABLE::MovementHandlerAgentPlayerControls[AgentVtableIndex] };
        const auto AgentName = VtableClassName(AgentVtable.address());

        if (AgentName != ".?AVMovementHandlerAgentPlayerControls@@"sv)
        {
            REX::ERROR("Slide: the player's movement agent vtable id points at '{}'. Slide disabled.", AgentName);
            return;
        }

        SneakButton::Original = Vtable.write_vfunc(ButtonEventSlot, SneakButton::Thunk);
        UpdateSmallDelta::Original = AgentVtable.write_vfunc(SmallDeltaSlot, UpdateSmallDelta::Thunk);
        REX::INFO("Slide hooks installed");
    }

    bool Active()
    {
        return Current.load() != Phase::kOff;
    }

    bool PlaysClip()
    {
        return OwnClip;
    }

    bool Refuse(const RE::BSFixedString& Event)
    {
        if (Current.load() != Phase::kCommitted)
            return false;

        for (const auto Prefix : RefusedPrefixes)
        {
            if (TagStarts(Event, Prefix))
            {
                if (Settings::VerboseLog())
                    REX::INFO("Slide: {} refused while committed", Event.c_str());

                return true;
            }
        }

        return false;
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player || Current.load() == Phase::kOff)
            return;

        if (!Settings::Slide())
        {
            End(Player, "option turned off");
            return;
        }

        if (!InFirstPerson(Player))
        {
            End(Player, "third person");
            return;
        }

        Elapsed += Delta;
        Progress = (std::min)(Elapsed / Duration, 1.0f);

        if (Sneaking(Player))
        {
            Crouched = true;
        }
        else if (Crouched || Elapsed >= CrouchTimeout)
        {
            End(Player, Crouched ? "not crouched any more" : "the crouch was refused");
            return;
        }

        if (SoundPending)
        {
            SoundPending = false;
            StartSounds(Player);
        }
        else if (LoopHandle.IsValid())
        {
            LoopHandle.SetPosition(Player->GetPosition());
        }

        if (PlayPending)
        {
            PlayPending = false;
            Played = Send(Player, "MWB_SlidePlay");
        }

        if (Elapsed >= Duration)
        {
            End(Player, "done");
            return;
        }

        if (Elapsed >= Commitment && Current.load() == Phase::kCommitted)
        {
            Current = Phase::kSliding;

            if (Settings::VerboseLog())
                REX::INFO("Slide: committed time over");
        }
    }

    void Reset()
    {
        Current = Phase::kOff;
        SwallowRelease = false;
        Elapsed = 0.0f;
        Progress = 0.0f;
        PlayPending = false;
        OwnClip = false;
        Played = false;
        Crouched = false;
        SoundPending = false;
        HaveSprintMovement = false;
        ImpactSetsSearched = false;
        ImpactSets = {};

        if (SoundsStarted && LoopHandle.IsValid())
            LoopHandle.Stop();

        SoundsStarted = false;
        LoopHandle = {};
    }
}
