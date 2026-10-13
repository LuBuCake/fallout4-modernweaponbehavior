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

#include "HoldBlock.h"
#include "Settings/Settings.h"

namespace
{
    constexpr float MinimumHold = 0.3f;
    constexpr float HoldingSpeedScale = 0.5f;

    constexpr std::size_t MaxSpeedsSlot = 0x0A;
    constexpr std::ptrdiff_t MovementStateOffset = 0x128;

    constexpr std::size_t TranslationRows = 4;
    constexpr std::size_t Columns = 4;

    std::atomic<bool>    ButtonDown{ false };
    std::atomic<bool>    Holding{ false };
    std::atomic<bool>    GraphHolds{ false };
    float                Elapsed{ 0.0f };
    std::optional<float> ActionPointsCap;

    [[nodiscard]] bool TagIs(const RE::BSFixedString& Tag, std::string_view Name)
    {
        const std::string_view Text{ Tag.c_str() };

        return Text.size() == Name.size() && _strnicmp(Text.data(), Name.data(), Text.size()) == 0;
    }

    [[nodiscard]] bool IsBlockStart(const RE::BSFixedString& Event)
    {
        return TagIs(Event, "blockStart"sv) || TagIs(Event, "blockSneakStart"sv) || TagIs(Event, "blockStartSneak"sv);
    }

    void SyncVariable(RE::PlayerCharacter* Player)
    {
        static const RE::BSFixedString Variable{ "iMWB_HoldBlock" };
        const std::int32_t Wanted = Settings::HoldBlock() ? 1 : 0;

        std::int32_t Current = 0;

        if (!Player->GetGraphVariableImplInt(Variable, Current))
            return;

        if (Current != Wanted)
        {
            Player->SetGraphVariableInt(Variable, Wanted);

            if (Settings::VerboseLog())
                REX::INFO("Hold block: iMWB_HoldBlock {} -> {}", Current, Wanted);
        }

        GraphHolds = Wanted == 1;
    }

    [[nodiscard]] RE::ActorValueInfo* ActionPointsValue()
    {
        const auto Values = RE::ActorValue::GetSingleton();

        return Values ? Values->actionPoints : nullptr;
    }

    void HoldActionPoints(RE::PlayerCharacter* Player)
    {
        const auto Value = ActionPointsValue();

        if (!Value)
            return;

        const float Current = Player->GetActorValue(*Value);

        if (!ActionPointsCap || Current < *ActionPointsCap)
        {
            ActionPointsCap = Current;
            return;
        }

        if (Current > *ActionPointsCap)
            Player->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, *Value, *ActionPointsCap - Current);
    }

    struct GetCurrentMaxSpeeds
    {
        static bool Thunk(const RE::IMovementState* This, RE::Movement::MaxSpeeds& Speeds)
        {
            const bool Result = Original(This, Speeds);

            if (Result && Holding)
            {
                for (std::size_t Row = 0; Row < TranslationRows; ++Row)
                {
                    for (std::size_t Column = 0; Column < Columns; ++Column)
                        Speeds.speeds[Row][Column] *= HoldingSpeedScale;
                }
            }

            return Result;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };
}

namespace HoldBlock
{
    void Install()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
        {
            REX::ERROR("Hold block: no player. Blocking will not slow the player down.");
            return;
        }

        const auto Live = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(Player) + MovementStateOffset);

        for (const auto& ID : RE::VTABLE::PlayerCharacter)
        {
            REL::Relocation<std::uintptr_t> Vtable{ ID };

            if (Vtable.address() == Live)
            {
                GetCurrentMaxSpeeds::Original = Vtable.write_vfunc(MaxSpeedsSlot, GetCurrentMaxSpeeds::Thunk);
                REX::INFO("Hold block speed hook installed");
                return;
            }
        }

        REX::ERROR("Hold block: the player's movement state vtable ({:X}) is not a PlayerCharacter one. Blocking will not slow the player down.", Live);
    }

    void SetBlockButton(bool Down)
    {
        ButtonDown = Down;
    }

    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        if (!Accepted)
            return;

        if (IsBlockStart(Event))
        {
            if (!GraphHolds)
                return;

            Holding = true;

            if (Settings::VerboseLog())
                REX::INFO("Hold block: {} accepted, holding while the block button is down", Event.c_str());
        }
        else if (TagIs(Event, "blockStop"sv))
        {
            Holding = false;
        }
    }

    void Tick(RE::PlayerCharacter* Player, float Delta)
    {
        if (!Player)
            return;

        SyncVariable(Player);

        if (!Holding)
        {
            Elapsed = 0.0f;
            ActionPointsCap.reset();
            return;
        }

        Elapsed += Delta;
        HoldActionPoints(Player);

        if (ButtonDown || Elapsed < MinimumHold)
            return;

        Holding = false;
        Elapsed = 0.0f;
        ActionPointsCap.reset();

        static const RE::BSFixedString StopEvent{ "blockStop" };
        const bool Accepted = Player->NotifyAnimationGraphImpl(StopEvent);

        if (Settings::VerboseLog())
            REX::INFO("Hold block: block button released, blockStop {}", Accepted ? "accepted" : "refused (the block was already over)");
    }

    void Reset()
    {
        Holding = false;
        Elapsed = 0.0f;
        ActionPointsCap.reset();
    }
}
