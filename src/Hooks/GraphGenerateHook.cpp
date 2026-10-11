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

#include "GraphGenerateHook.h"
#include "Arm/ArmSpace.h"
#include "Weapon/WeaponBones.h"

namespace
{
    constexpr REL::ID     GraphGenerateID{ 997127, 2256860 };
    constexpr REL::ID     BehaviorGraphGenerateID{ 346267, 2260439 };
    constexpr std::size_t ScanSize = 0x300;

    struct BehaviorGraphGenerate
    {
        static void Thunk(void* Graph, void* Context, void* Output, bool Arg4, float Arg5, bool Arg6)
        {
            Original(Graph, Context, Output, Arg4, Arg5, Arg6);

            const auto Character = Context ? *static_cast<const void* const*>(Context) : nullptr;

            if (!Character || !Output)
                return;

            WeaponBones::OnGenerate(Character, Output);
            ArmSpace::OnGenerate(Character, Output);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] std::optional<std::uintptr_t> FindDirectCall(std::uintptr_t Begin, std::uintptr_t Target)
    {
        for (std::size_t i = 0; i + 5 <= ScanSize; ++i)
        {
            const auto At = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != 0xE8)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(At + 1);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + 5) + Displacement) == Target)
                return At;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uintptr_t> FindGenerateCall(std::uintptr_t Function, std::uintptr_t Target)
    {
        if (const auto Site = FindDirectCall(Function, Target))
            return Site;

        std::vector<std::uintptr_t> Found;
        std::vector<std::uintptr_t> Visited;

        for (std::size_t i = 0; i + 5 <= ScanSize; ++i)
        {
            const auto At = Function + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != 0xE8)
                continue;

            const auto Callee = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + 5) + *reinterpret_cast<const std::int32_t*>(At + 1));

            if (std::ranges::find(Visited, Callee) != Visited.end())
                continue;

            Visited.push_back(Callee);

            if (const auto Site = FindDirectCall(Callee, Target))
                Found.push_back(*Site);
        }

        return Found.size() == 1 ? std::optional{ Found.front() } : std::nullopt;
    }
}

namespace Hooks
{
    void InstallGraphGenerateHook()
    {
        const auto Function = REL::Relocation<std::uintptr_t>{ GraphGenerateID }.address();
        const auto Target = REL::Relocation<std::uintptr_t>{ BehaviorGraphGenerateID }.address();
        const auto Site = FindGenerateCall(Function, Target);

        if (!Site)
        {
            REX::WARN("The call to hkbBehaviorGraph::generate was not found (another mod may have it). Arm animations play in the "
                      "weapon's torso space, and the slide lock is off.");
            return;
        }

        BehaviorGraphGenerate::Original = REL::GetTrampoline().write_call<5>(*Site, BehaviorGraphGenerate::Thunk);
        REX::INFO("Graph generate hook installed at {:X} (chaining to {:X})", *Site, BehaviorGraphGenerate::Original.address());
    }
}
