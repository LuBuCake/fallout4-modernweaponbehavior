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

namespace Reload
{
    enum class Step
    {
        None,
        InsertShell,
        FullReload,
        CallLoop,
    };

    struct Action
    {
        Step          What{ Step::None };
        std::uint32_t Count{ 0 };
        bool          Full{ false };
    };

    void Initialize();

    [[nodiscard]] Action BeforeGameHandles(const RE::BSAnimationGraphEvent& Event, const void* Source);
    void                 AfterGameHandles(const Action& Requested);

    void Tick(float Delta);

    void OnActorEvent(RE::Actor* Actor, const RE::BSAnimationGraphEvent& Event);

    void SetTickAvailable(bool Available);

    void RequestStop();

    void Abort(std::string_view Reason);

    [[nodiscard]] bool Refuse(const RE::BSFixedString& Event);

    void TraceEvent(const RE::BSAnimationGraphEvent& Event);
    void TraceSent(const RE::BSFixedString& Event, bool Accepted, std::string_view Path);
}
