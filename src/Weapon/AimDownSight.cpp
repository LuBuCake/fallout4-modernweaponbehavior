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

#include "AimDownSight.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::ptrdiff_t NodeNameOffset = 0x38;
    constexpr std::ptrdiff_t ClipCropStartOffset = 0xA4;
    constexpr std::ptrdiff_t ClipCropEndOffset = 0xA8;
    constexpr std::ptrdiff_t ClipPlaybackSpeedOffset = 0xB0;

    constexpr float MinimumTransition = 0.01f;

    constexpr std::array ClipNodes{ "MWB_WPNAimDownSightIn"sv, "MWB_WPNAimDownSightOut"sv };

    template <class T>
    [[nodiscard]] T Read(const void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    template <class T>
    T& At(void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    [[nodiscard]] std::string_view NodeName(const void* Node)
    {
        const auto Name = reinterpret_cast<const char*>(Read<std::uintptr_t>(Node, NodeNameOffset) & ~std::uintptr_t{ 1 });

        return Name ? std::string_view{ Name } : std::string_view{};
    }

    std::atomic<float> TransitionSeconds{ 0.0f };

    RE::TESObjectWEAP*               Base{ nullptr };
    RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
    std::uint32_t                    SeenGeneration{ 0 };
    bool                             Matched{ false };
    bool                             WarnedGraph{ false };

    [[nodiscard]] bool MatchFor(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        const auto CurrentBase = Weapon ? Weapon->Base : nullptr;
        const auto CurrentInstance = Weapon ? Weapon->Instance : nullptr;

        if (CurrentBase != Base || CurrentInstance != Instance || SeenGeneration != WeaponConfig::Generation())
        {
            Base = CurrentBase;
            Instance = CurrentInstance;
            SeenGeneration = WeaponConfig::Generation();
            const bool CurrentMatch = CurrentBase && WeaponConfig::MatchAimDownSight(CurrentBase, CurrentInstance);

            if (CurrentMatch != Matched)
            {
                if (CurrentMatch)
                    REX::INFO("Aim down sight clips on for {:08X}", CurrentBase->GetFormID());
                else
                    REX::INFO("Aim down sight clips off");
            }

            Matched = CurrentMatch;
        }

        return Matched;
    }

    [[nodiscard]] float SightedTransitionSeconds(const std::optional<Core::EquippedWeapon>& Weapon)
    {
        if (!Weapon || !Weapon->Instance || !Weapon->Instance->rangedData)
            return 0.0f;

        return Weapon->Instance->rangedData->sightedTransitionSeconds;
    }
}

namespace AimDownSight
{
    void Tick(RE::PlayerCharacter* Player)
    {
        if (!Player)
            return;

        std::optional<Core::EquippedWeapon> Weapon;

        if (!Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        TransitionSeconds = SightedTransitionSeconds(Weapon);

        const std::int32_t Wanted = MatchFor(Weapon) ? 1 : 0;

        static const RE::BSFixedString Variable{ "iMWB_AimDownSight" };
        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
        {
            if (Wanted && !WarnedGraph)
            {
                REX::WARN("The player's animation graph does not know iMWB_AimDownSight. In first person, the aim down "
                          "sight clips need Modern Weapon Behavior's GunBehavior, RootBehavior and weapon wrapping behaviors, "
                          "winning over other mods'");
                WarnedGraph = true;
            }

            return;
        }

        if (Current == Wanted)
            return;

        Player->SetGraphVariableInt(Variable, Wanted);

        if (Settings::VerboseLog())
            REX::INFO("Aim down sight: iMWB_AimDownSight {} -> {}", Current, Wanted);
    }

    void OnClipActivated(void* Clip)
    {
        if (!Clip)
            return;

        const auto Name = NodeName(Clip);

        if (std::ranges::find(ClipNodes, Name) == ClipNodes.end())
            return;

        const float Duration = Hooks::ClipDuration(Clip) - Read<float>(Clip, ClipCropStartOffset) - Read<float>(Clip, ClipCropEndOffset);

        if (Duration <= 0.0f)
        {
            if (Settings::VerboseLog())
                REX::INFO("Aim down sight: {} has no animation", Name);

            return;
        }

        const float Seconds = (std::max)(TransitionSeconds.load(), MinimumTransition);
        const float Speed = Duration / Seconds;
        At<float>(Clip, ClipPlaybackSpeedOffset) = Speed;

        if (Settings::VerboseLog())
            REX::INFO("Aim down sight: {} lasts {:.3f}s, played at {:.3f}x to last the weapon's {:.3f}s", Name, Duration, Speed, Seconds);
    }

    void Reset()
    {
        Base = nullptr;
        Instance = nullptr;
        Matched = false;
    }
}
