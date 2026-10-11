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

namespace Hooks
{
    void InstallClipGeneratorHook();

    void RefreshPlayerCharacters(RE::PlayerCharacter* Player);

    [[nodiscard]] std::optional<std::uint32_t> RemainingReloadCompletes();

    void ForgetClips();

    [[nodiscard]] bool IsPlayerGraph(const void* Graph);

    using AnnotationVisitor = void (*)(void* Context, float Time, const char* Text);
    void VisitClipAnnotations(const void* Clip, AnnotationVisitor Visit, void* Context);

    [[nodiscard]] float ClipLocalTime(const void* Clip);

    [[nodiscard]] float ClipDuration(const void* Clip);

    [[nodiscard]] const void* FindActiveClip(std::span<const std::string_view> Names);

    [[nodiscard]] RE::Actor* ActorOfCharacter(const void* Character);
}
