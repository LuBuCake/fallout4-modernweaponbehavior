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

#pragma once

namespace HUDValues
{
    using Value = Scaleform::GFx::Value;

    inline constexpr std::size_t ClipValue = 0x8;
    inline constexpr std::size_t ValueDataOffset = 0x10;
    inline constexpr std::size_t MovieRootOffset = 0x8;
    inline constexpr std::size_t SettingValueOffset = 0x8;

    [[nodiscard]] inline Value& AsValue(void* Component)
    {
        return *reinterpret_cast<Value*>(static_cast<std::byte*>(Component) + ClipValue);
    }

    [[nodiscard]] inline const void* ClipOf(const Value& Object)
    {
        return Object.IsDisplayObject() ? *reinterpret_cast<const void* const*>(reinterpret_cast<const std::byte*>(&Object) + ValueDataOffset) : nullptr;
    }

    [[nodiscard]] inline Scaleform::GFx::Movie* MovieOf(const Value& Object)
    {
        const auto ObjectInterface = *reinterpret_cast<const std::uintptr_t*>(&Object);

        return ObjectInterface ? *reinterpret_cast<Scaleform::GFx::Movie* const*>(ObjectInterface + MovieRootOffset) : nullptr;
    }

    inline void Abandon(Value& Object)
    {
        new (&Object) Value();
    }

    inline void SetFadeTimer(void* Component)
    {
        using SetFadeTimerSignature = void (*)(void*, std::uint32_t);
        static REL::Relocation<SetFadeTimerSignature> GameSetFadeTimer{ REL::ID{ 308508, 2220246 } };
        static REL::Relocation<std::uintptr_t> AmmoCounterFadeTimeSetting{ REL::ID{ 1157750, 2663560 } };

        GameSetFadeTimer(Component, *reinterpret_cast<const std::uint32_t*>(AmmoCounterFadeTimeSetting.address() + SettingValueOffset));
    }
}
