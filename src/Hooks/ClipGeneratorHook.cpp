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

#include "ClipGeneratorHook.h"
#include "Magic/MagicAnnotations.h"
#include "Arm/ArmSpace.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t ActivateSlot = 0x7;
    constexpr std::size_t DeactivateSlot = 0xA;

    constexpr std::ptrdiff_t ClipAnimationControlOffset = 0xD0;
    constexpr std::ptrdiff_t ClipLocalTimeOffset = 0x140;
    constexpr std::ptrdiff_t ControlBindingOffset = 0x38;
    constexpr std::ptrdiff_t BindingAnimationOffset = 0x18;
    constexpr std::ptrdiff_t AnimationAnnotationTracksOffset = 0x28;
    constexpr std::ptrdiff_t AnimationDurationOffset = 0x14;
    constexpr std::ptrdiff_t NodeNameOffset = 0x38;
    constexpr std::ptrdiff_t GraphCharacterOffset = 0x1C8;
    constexpr std::ptrdiff_t GraphOwnerOffset = 0x380;

    struct HkArray
    {
        const void*  Data;
        std::int32_t Size;
        std::int32_t CapacityAndFlags;
    };
    static_assert(sizeof(HkArray) == 0x10);

    struct Annotation
    {
        float          Time;
        std::uintptr_t Text;
    };
    static_assert(sizeof(Annotation) == 0x10);

    struct AnnotationTrack
    {
        std::uintptr_t Name;
        HkArray        Annotations;
    };
    static_assert(sizeof(AnnotationTrack) == 0x18);

    template <class T>
    [[nodiscard]] T Read(const void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    struct TrackedClip
    {
        const void* Clip;
        const void* Character;
    };

    constexpr std::size_t                           MaxGraphs = 4;
    std::array<std::atomic<const void*>, MaxGraphs> PlayerCharacters{};
    std::array<std::atomic<const void*>, MaxGraphs> PlayerGraphs{};
    std::atomic<const void*>                        ActiveCharacter{ nullptr };
    std::mutex                                      Lock;
    std::vector<TrackedClip>                        Clips;
    std::atomic<std::size_t>                        TrackedCount{ 0 };

    [[nodiscard]] bool IsPlayerCharacter(const void* Character)
    {
        if (!Character)
            return false;

        return std::ranges::any_of(PlayerCharacters, [&](const auto& Slot) { return Slot.load(std::memory_order_relaxed) == Character; });
    }

    [[nodiscard]] std::vector<float> ReloadCompleteTimes(const void* Clip, bool& HasAnimation)
    {
        std::vector<float> Times;

        const auto Control = Read<const void*>(Clip, ClipAnimationControlOffset);
        const auto Binding = Control ? Read<const void*>(Control, ControlBindingOffset) : nullptr;
        const auto Animation = Binding ? Read<const void*>(Binding, BindingAnimationOffset) : nullptr;
        HasAnimation = Animation != nullptr;

        if (!Animation)
            return Times;

        const auto Tracks = Read<HkArray>(Animation, AnimationAnnotationTracksOffset);

        for (std::int32_t t = 0; t < Tracks.Size; ++t)
        {
            const auto& Track = static_cast<const AnnotationTrack*>(Tracks.Data)[t];

            for (std::int32_t a = 0; a < Track.Annotations.Size; ++a)
            {
                const auto& CurrentAnnotation = static_cast<const Annotation*>(Track.Annotations.Data)[a];
                const auto  Text = reinterpret_cast<const char*>(CurrentAnnotation.Text & ~std::uintptr_t{ 1 });

                if (Text && _stricmp(Text, "reloadComplete") == 0)
                    Times.push_back(CurrentAnnotation.Time);
            }
        }

        std::ranges::sort(Times);

        return Times;
    }

    void Untrack(const void* Clip)
    {
        std::erase_if(Clips, [&](const TrackedClip& Tracked) { return Tracked.Clip == Clip; });
        TrackedCount = Clips.size();
    }

    struct Activate
    {
        static void Thunk(void* Clip, const void* Context)
        {
            Original(Clip, Context);

            const auto Character = Context ? Read<const void*>(Context, 0) : nullptr;
            MagicAnnotations::OnClipActivated(Clip, Character);

            if (!IsPlayerCharacter(Character))
                return;

            ArmSpace::OnClipActivated(Clip, Character);

            const std::scoped_lock Guard{ Lock };
            Untrack(Clip);
            Clips.push_back({ Clip, Character });
            TrackedCount = Clips.size();
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct Deactivate
    {
        static void Thunk(void* Clip, const void* Context)
        {
            if (TrackedCount.load(std::memory_order_relaxed) > 0)
            {
                const std::scoped_lock Guard{ Lock };
                Untrack(Clip);
            }

            ArmSpace::OnClipDeactivated(Clip);
            MagicAnnotations::OnClipDeactivated(Clip);
            Original(Clip, Context);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

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
}

namespace Hooks
{
    void InstallClipGeneratorHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::hkbClipGenerator[0] };

        const auto Name = VtableClassName(Vtable.address());

        if (Name != ".?AVhkbClipGenerator@@"sv)
        {
            REX::ERROR("hkbClipGenerator vtable id points at '{}'. Every reloadComplete counts as the last of its pass.", Name);
            return;
        }

        Activate::Original = Vtable.write_vfunc(ActivateSlot, Activate::Thunk);
        Deactivate::Original = Vtable.write_vfunc(DeactivateSlot, Deactivate::Thunk);
        REX::INFO("hkbClipGenerator hook installed");
    }

    void RefreshPlayerCharacters(RE::PlayerCharacter* Player)
    {
        std::array<const void*, MaxGraphs> Characters{};
        std::array<const void*, MaxGraphs> Graphs{};
        const void*                        Active = nullptr;

        RE::BSTSmartPointer<RE::BSAnimationGraphManager> Manager;

        if (Player && Player->GetAnimationGraphManagerImpl(Manager) && Manager)
        {
            const auto Count = (std::min)(static_cast<std::size_t>(Manager->graph.size()), MaxGraphs);

            for (std::size_t i = 0; i < Count; ++i)
            {
                if (const auto Graph = Manager->graph[static_cast<std::uint32_t>(i)].get())
                {
                    Graphs[i] = Graph;
                    Characters[i] = reinterpret_cast<const std::byte*>(Graph) + GraphCharacterOffset;
                }
            }

            if (Manager->activeGraph < Count)
                Active = Characters[Manager->activeGraph];
        }

        bool Changed = false;

        for (std::size_t i = 0; i < MaxGraphs; ++i)
        {
            Changed |= PlayerCharacters[i].exchange(Characters[i], std::memory_order_relaxed) != Characters[i];
            PlayerGraphs[i].store(Graphs[i], std::memory_order_relaxed);
        }

        ActiveCharacter.store(Active, std::memory_order_relaxed);

        if (Changed)
            ArmSpace::OnCharactersChanged(Characters);

        if (Changed && TrackedCount.load(std::memory_order_relaxed) > 0)
        {
            const std::scoped_lock Guard{ Lock };
            std::erase_if(Clips, [&](const TrackedClip& Tracked) { return std::ranges::find(Characters, Tracked.Character) == Characters.end(); });
            TrackedCount = Clips.size();
        }
    }

    std::optional<std::uint32_t> RemainingReloadCompletes()
    {
        const auto Active = ActiveCharacter.load(std::memory_order_relaxed);

        const std::scoped_lock Guard{ Lock };

        std::optional<std::uint32_t> Remaining;
        float                        Nearest = (std::numeric_limits<float>::max)();
        std::size_t                  OnActive = 0;
        std::size_t                  WithAnimation = 0;
        std::size_t                  WithReloadComplete = 0;

        for (const auto& Tracked : Clips)
        {
            if (Tracked.Character != Active)
                continue;

            ++OnActive;

            bool       HasAnimation = false;
            const auto Times = ReloadCompleteTimes(Tracked.Clip, HasAnimation);
            WithAnimation += HasAnimation ? 1 : 0;

            if (Times.empty())
                continue;

            ++WithReloadComplete;

            const auto Local = Read<float>(Tracked.Clip, ClipLocalTimeOffset) + 0.0001f;
            const auto Ahead = std::ranges::upper_bound(Times, Local);

            if (Ahead == Times.begin())
                continue;

            const float Since = Local - *std::prev(Ahead);

            if (Since < Nearest)
            {
                Nearest = Since;
                Remaining = static_cast<std::uint32_t>(Times.end() - Ahead);
            }
        }

        if (Settings::VerboseLog())
        {
            if (Remaining)
            {
                REX::INFO("reloadComplete: {} more in this clip", *Remaining);
            }
            else
            {
                REX::INFO("reloadComplete: no clip found, taken as the last of the pass ({} player clip(s), {} on the "
                          "active graph, {} with an animation, {} with reloadComplete; graphs {:X} {:X}, active character {:X})",
                    Clips.size(), OnActive, WithAnimation, WithReloadComplete,
                    reinterpret_cast<std::uintptr_t>(PlayerGraphs[0].load()), reinterpret_cast<std::uintptr_t>(PlayerGraphs[1].load()),
                    reinterpret_cast<std::uintptr_t>(Active));
            }
        }

        return Remaining;
    }

    bool IsPlayerGraph(const void* Graph)
    {
        return Graph && std::ranges::any_of(PlayerGraphs, [&](const auto& Slot) { return Slot.load(std::memory_order_relaxed) == Graph; });
    }

    void ForgetClips()
    {
        const std::scoped_lock Guard{ Lock };
        Clips.clear();
        TrackedCount = 0;
    }

    void VisitClipAnnotations(const void* Clip, AnnotationVisitor Visit, void* Context)
    {
        const auto Control = Read<const void*>(Clip, ClipAnimationControlOffset);
        const auto Binding = Control ? Read<const void*>(Control, ControlBindingOffset) : nullptr;
        const auto Animation = Binding ? Read<const void*>(Binding, BindingAnimationOffset) : nullptr;

        if (!Animation)
            return;

        const auto Tracks = Read<HkArray>(Animation, AnimationAnnotationTracksOffset);

        for (std::int32_t t = 0; t < Tracks.Size; ++t)
        {
            const auto& Track = static_cast<const AnnotationTrack*>(Tracks.Data)[t];

            for (std::int32_t a = 0; a < Track.Annotations.Size; ++a)
            {
                const auto& CurrentAnnotation = static_cast<const Annotation*>(Track.Annotations.Data)[a];

                if (const auto Text = reinterpret_cast<const char*>(CurrentAnnotation.Text & ~std::uintptr_t{ 1 }))
                    Visit(Context, CurrentAnnotation.Time, Text);
            }
        }
    }

    float ClipLocalTime(const void* Clip)
    {
        return Read<float>(Clip, ClipLocalTimeOffset);
    }

    float ClipDuration(const void* Clip)
    {
        const auto Control = Read<const void*>(Clip, ClipAnimationControlOffset);
        const auto Binding = Control ? Read<const void*>(Control, ControlBindingOffset) : nullptr;
        const auto Animation = Binding ? Read<const void*>(Binding, BindingAnimationOffset) : nullptr;

        return Animation ? Read<float>(Animation, AnimationDurationOffset) : 0.0f;
    }

    const void* FindActiveClip(std::span<const std::string_view> Names)
    {
        if (TrackedCount.load(std::memory_order_relaxed) == 0)
            return nullptr;

        const auto Active = ActiveCharacter.load(std::memory_order_relaxed);

        const std::scoped_lock Guard{ Lock };

        for (const auto& Tracked : Clips)
        {
            if (Tracked.Character != Active)
                continue;

            const auto Name = reinterpret_cast<const char*>(Read<std::uintptr_t>(Tracked.Clip, NodeNameOffset) & ~std::uintptr_t{ 1 });

            if (Name && std::ranges::find(Names, std::string_view{ Name }) != Names.end())
                return Tracked.Clip;
        }

        return nullptr;
    }

    RE::Actor* ActorOfCharacter(const void* Character)
    {
        if (!Character)
            return nullptr;

        const auto Graph = reinterpret_cast<const std::byte*>(Character) - GraphCharacterOffset;
        const auto Owner = Read<const void*>(Graph, GraphOwnerOffset);

        if (!Owner)
            return nullptr;

        static const std::uintptr_t ActorVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE::Actor[0] }.address();
        static const std::uintptr_t PlayerVtable = REL::Relocation<std::uintptr_t>{ RE::VTABLE::PlayerCharacter[0] }.address();
        const auto                  Vtable = *static_cast<const std::uintptr_t*>(Owner);

        return Vtable == ActorVtable || Vtable == PlayerVtable ? static_cast<RE::Actor*>(const_cast<void*>(Owner)) : nullptr;
    }
}
