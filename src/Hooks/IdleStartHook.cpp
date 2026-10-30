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

#include "IdleStartHook.h"
#include "Arm/ArmAnimations.h"

namespace
{
    constexpr REL::ID                QueueID{ 1016554, 2236434 };
    constexpr std::array<REL::ID, 2> StartFunctions{ REL::ID{ 1073018, 2236389 }, REL::ID{ 340765, 2236390 } };
    constexpr std::size_t            ScanSize = 0x300;
    constexpr std::size_t            MaxSites = 2;
    constexpr std::ptrdiff_t         ActionRef = 0x08;
    constexpr std::ptrdiff_t         ActionEvent = 0x28;

    using QueueSignature = bool(void*, void*, void*);

    template <std::size_t N>
    struct QueueIdle
    {
        static bool Thunk(void* Manager, void* Action, void* Path)
        {
            if (Action)
            {
                const auto Ref = *reinterpret_cast<RE::TESObjectREFR* const*>(reinterpret_cast<std::uintptr_t>(Action) + ActionRef);
                const auto& Event = *reinterpret_cast<const RE::BSFixedString*>(reinterpret_cast<std::uintptr_t>(Action) + ActionEvent);

                if (Ref && Ref == RE::PlayerCharacter::GetSingleton() && ArmAnimations::RefuseIdle(Event))
                    return false;
            }

            return Original(Manager, Action, Path);
        }

        inline static REL::Relocation<QueueSignature> Original;
    };

    [[nodiscard]] std::vector<std::uintptr_t> FindCalls(std::uintptr_t Function, std::uintptr_t Target)
    {
        std::vector<std::uintptr_t> Found;

        for (std::size_t i = 0; i + 5 <= ScanSize; ++i)
        {
            const auto At = Function + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != 0xE8)
                continue;

            const auto Callee = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + 5) + *reinterpret_cast<const std::int32_t*>(At + 1));

            if (Callee == Target)
                Found.push_back(At);
        }

        return Found;
    }

    template <std::size_t N>
    void Write(std::uintptr_t Site)
    {
        QueueIdle<N>::Original = REL::GetTrampoline().write_call<5>(Site, QueueIdle<N>::Thunk);
        REX::INFO("Idle start hook installed at {:X} (chaining to {:X})", Site, QueueIdle<N>::Original.address());
    }
}

namespace Hooks
{
    void InstallIdleStartHook()
    {
        const auto Target = REL::Relocation<std::uintptr_t>{ QueueID }.address();

        std::vector<std::uintptr_t> Sites;

        for (const auto& StartFunctionID : StartFunctions)
        {
            const auto Function = REL::Relocation<std::uintptr_t>{ StartFunctionID }.address();
            const auto Calls = FindCalls(Function, Target);

            if (Calls.size() != 1)
            {
                REX::WARN("Idle start hook: {} call(s) to the idle queue in {:X}, expected one. A second arm animation on a "
                          "busy arm may leave the game waiting for it.",
                    Calls.size(), Function);
                continue;
            }

            Sites.push_back(Calls.front());
        }

        static_assert(MaxSites == 2);

        if (Sites.size() > 0)
            Write<0>(Sites[0]);

        if (Sites.size() > 1)
            Write<1>(Sites[1]);
    }
}
