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

#include "QueuedEventHook.h"
#include "Arm/ArmAnimations.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Reload/LoopReload.h"

namespace
{
    constexpr REL::ID                SendEventID{ 876457, 2256862 };
    constexpr std::array<REL::ID, 2> QueueFunctions{ REL::ID{ 1225215, 2257431 }, REL::ID{ 79938, 2257431 } };
    constexpr std::size_t            ScanSize = 0x200;
    constexpr std::size_t            MaxSites = 2;

    using SendSignature = bool(void*, const RE::BSFixedString*, void*);

    template <std::size_t N>
    struct QueuedSend
    {
        static bool Thunk(void* Graph, const RE::BSFixedString* Event, void* Arg3)
        {
            if (!Event || !Hooks::IsPlayerGraph(Graph))
                return Original(Graph, Event, Arg3);

            if (const auto FallbackEvent = ArmAnimations::Fallback(*Event))
                return Original(Graph, FallbackEvent, Arg3);

            if (Original(Graph, Event, Arg3))
            {
                Reload::TraceSent(*Event, true, "IDLE queue"sv);
                return true;
            }

            Reload::TraceSent(*Event, false, "IDLE queue"sv);

            if (const auto FallbackEvent = ArmAnimations::RefusedFallback(*Event))
                return Original(Graph, FallbackEvent, Arg3);

            return false;
        }

        inline static REL::Relocation<SendSignature> Original;
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
        QueuedSend<N>::Original = REL::GetTrampoline().write_call<5>(Site, QueuedSend<N>::Thunk);
        REX::INFO("Queued event hook installed at {:X} (chaining to {:X})", Site, QueuedSend<N>::Original.address());
    }
}

namespace Hooks
{
    void InstallQueuedEventHook()
    {
        const auto Target = REL::Relocation<std::uintptr_t>{ SendEventID }.address();

        std::vector<std::uintptr_t> Sites;

        for (const auto& QueueFunctionID : QueueFunctions)
        {
            const auto Function = REL::Relocation<std::uintptr_t>{ QueueFunctionID }.address();
            const auto Calls = FindCalls(Function, Target);

            if (Calls.size() != 1)
            {
                REX::WARN("Queued event hook: {} call(s) to the event send in {:X}, expected one. Arm animations of IDLEs "
                          "whose animation had to load may not fall back outside GunBehavior.",
                    Calls.size(), Function);
                continue;
            }

            if (std::ranges::find(Sites, Calls.front()) == Sites.end())
                Sites.push_back(Calls.front());
        }

        static_assert(MaxSites == 2);

        if (Sites.size() > 0)
            Write<0>(Sites[0]);

        if (Sites.size() > 1)
            Write<1>(Sites[1]);
    }
}
