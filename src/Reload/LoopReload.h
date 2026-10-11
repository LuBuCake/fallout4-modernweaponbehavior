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
    enum class Action
    {
        None,
        RequestNextPass,
    };

    void Initialize();

    [[nodiscard]] Action BeforeGameHandles(const RE::BSFixedString& Tag, const void* Source);
    void                 AfterGameHandles(Action Requested);

    void Tick(float Delta);

    void SetTickAvailable(bool Available);

    void RequestStop();

    void Abort(std::string_view Reason);
}
