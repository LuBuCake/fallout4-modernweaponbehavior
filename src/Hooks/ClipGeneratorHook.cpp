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
#include "Weapon/AimDownSight.h"
#include "Settings/Settings.h"
#include "Sprint/TacticalSprint.h"

namespace
{
    constexpr std::size_t ActivateSlot = 0x7;
    constexpr std::size_t UpdateSlot = 0x8;
    constexpr std::size_t DeactivateSlot = 0xA;

    constexpr std::ptrdiff_t ClipAnimationControlOffset = 0xD0;
    constexpr std::ptrdiff_t ClipLocalTimeOffset = 0x140;
    constexpr std::ptrdiff_t ClipAnimationNameOffset = 0x90;
    constexpr std::ptrdiff_t ClipPlaybackSpeedOffset = 0xB0;
    constexpr std::ptrdiff_t ControlLocalTimeOffset = 0x10;
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

    struct ActorClip
    {
        const void*      Clip;
        const void*      Character;
        const RE::Actor* Owner;
    };

    constexpr std::size_t      MaxActorClips = 64;
    constexpr std::string_view ReloadMarkPrefix = "MWB_Reload."sv;
    std::vector<ActorClip>     ActorClips;
    std::atomic<std::size_t>   ActorClipCount{ 0 };

    [[nodiscard]] bool IsPlayerCharacter(const void* Character)
    {
        if (!Character)
            return false;

        return std::ranges::any_of(PlayerCharacters, [&](const auto& Slot) { return Slot.load(std::memory_order_relaxed) == Character; });
    }

    constexpr float CrossedTolerance = 0.0001f;
    constexpr float JustCrossed = 0.25f;

    enum class ReloadMarkKind
    {
        InsertShell,
        CallLoop,
    };

    struct ReloadMark
    {
        float          Time;
        ReloadMarkKind Kind;
    };

    struct PendingJump
    {
        const void* Clip;
        float       Time;
    };

    struct ScaledClip
    {
        const void* Clip;
        float       Base;
        float       Written;
    };

    std::vector<PendingJump> Jumps;
    std::atomic<std::size_t> JumpCount{ 0 };

    std::mutex               ScaleLock;
    std::vector<ScaledClip>  Scaled;
    std::atomic<std::size_t> ScaledCount{ 0 };

    [[nodiscard]] float& Field(void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    [[nodiscard]] const void* AnimationOf(const void* Clip)
    {
        const auto Control = Read<const void*>(Clip, ClipAnimationControlOffset);
        const auto Binding = Control ? Read<const void*>(Control, ControlBindingOffset) : nullptr;

        return Binding ? Read<const void*>(Binding, BindingAnimationOffset) : nullptr;
    }

    [[nodiscard]] std::vector<ReloadMark> ReloadMarks(const void* Clip, bool& HasAnimation)
    {
        std::vector<ReloadMark> Marks;

        const auto Animation = AnimationOf(Clip);
        HasAnimation = Animation != nullptr;

        if (!Animation)
            return Marks;

        const auto Tracks = Read<HkArray>(Animation, AnimationAnnotationTracksOffset);

        for (std::int32_t t = 0; t < Tracks.Size; ++t)
        {
            const auto& Track = static_cast<const AnnotationTrack*>(Tracks.Data)[t];

            for (std::int32_t a = 0; a < Track.Annotations.Size; ++a)
            {
                const auto& CurrentAnnotation = static_cast<const Annotation*>(Track.Annotations.Data)[a];
                const auto  Text = reinterpret_cast<const char*>(CurrentAnnotation.Text & ~std::uintptr_t{ 1 });

                if (!Text)
                    continue;

                if (_stricmp(Text, "MWB_Reload.InsertShell") == 0)
                    Marks.push_back({ CurrentAnnotation.Time, ReloadMarkKind::InsertShell });
                else if (_stricmp(Text, "MWB_Reload.CallLoop") == 0)
                    Marks.push_back({ CurrentAnnotation.Time, ReloadMarkKind::CallLoop });
            }
        }

        std::ranges::stable_sort(Marks, {}, &ReloadMark::Time);

        return Marks;
    }

    [[nodiscard]] bool HasReloadMarks(const void* Clip)
    {
        const auto Animation = AnimationOf(Clip);

        if (!Animation)
            return false;

        const auto Tracks = Read<HkArray>(Animation, AnimationAnnotationTracksOffset);

        for (std::int32_t t = 0; t < Tracks.Size; ++t)
        {
            const auto& Track = static_cast<const AnnotationTrack*>(Tracks.Data)[t];

            for (std::int32_t a = 0; a < Track.Annotations.Size; ++a)
            {
                const auto Text = reinterpret_cast<const char*>(static_cast<const Annotation*>(Track.Annotations.Data)[a].Text & ~std::uintptr_t{ 1 });

                if (Text && _strnicmp(Text, ReloadMarkPrefix.data(), ReloadMarkPrefix.size()) == 0)
                    return true;
            }
        }

        return false;
    }

    [[nodiscard]] const void* CurrentCharacterOf(RE::Actor* Actor)
    {
        RE::BSTSmartPointer<RE::BSAnimationGraphManager> Manager;

        if (!Actor || !Actor->GetAnimationGraphManagerImpl(Manager) || !Manager || Manager->activeGraph >= Manager->graph.size())
            return nullptr;

        const auto Graph = Manager->graph[Manager->activeGraph].get();

        return Graph ? reinterpret_cast<const std::byte*>(Graph) + GraphCharacterOffset : nullptr;
    }

    [[nodiscard]] std::optional<std::size_t> LastCrossed(const std::vector<ReloadMark>& Marks, float Local, ReloadMarkKind Kind)
    {
        std::optional<std::size_t> Found;

        for (std::size_t i = 0; i < Marks.size() && Marks[i].Time <= Local + CrossedTolerance; ++i)
        {
            if (Marks[i].Kind == Kind)
                Found = i;
        }

        return Found;
    }

    [[nodiscard]] std::string_view AnimationFile(const void* Clip)
    {
        const auto Text = reinterpret_cast<const char*>(Read<std::uintptr_t>(Clip, ClipAnimationNameOffset) & ~std::uintptr_t{ 1 });

        if (!Text)
            return {};

        const std::string_view Path{ Text };
        const auto             Slash = Path.find_last_of("\\/");

        return Slash == std::string_view::npos ? Path : Path.substr(Slash + 1);
    }

    void ApplyJump(void* Clip)
    {
        const std::scoped_lock Guard{ Lock };

        const auto Found = std::ranges::find(Jumps, static_cast<const void*>(Clip), &PendingJump::Clip);

        if (Found == Jumps.end())
            return;

        const float Time = Found->Time;
        Jumps.erase(Found);
        JumpCount = Jumps.size();

        const auto From = Read<float>(Clip, ClipLocalTimeOffset);
        Field(Clip, ClipLocalTimeOffset) = Time;

        if (const auto Control = Read<void*>(Clip, ClipAnimationControlOffset))
            Field(Control, ControlLocalTimeOffset) = Time;

        if (Settings::VerboseLog())
            REX::INFO("MWB_Reload: clip jumped from {:.3f}s to the last InsertShell at {:.3f}s", From, Time);
    }

    void ScalePlayback(void* Clip, float Scale)
    {
        const std::scoped_lock Guard{ ScaleLock };

        auto&      Speed = Field(Clip, ClipPlaybackSpeedOffset);
        const auto Found = std::ranges::find(Scaled, static_cast<const void*>(Clip), &ScaledClip::Clip);
        const bool Ours = Found != Scaled.end() && Speed == Found->Written;
        const auto Base = Ours ? Found->Base : Speed;

        if (Scale == 1.0f)
        {
            if (Found == Scaled.end())
                return;

            if (Ours)
                Speed = Base;

            Scaled.erase(Found);
            ScaledCount = Scaled.size();

            return;
        }

        Speed = Base * Scale;

        if (Found != Scaled.end())
            *Found = { Clip, Base, Speed };
        else
            Scaled.push_back({ Clip, Base, Speed });

        ScaledCount = Scaled.size();
    }

    void RestorePlaybackBase(void* Clip)
    {
        const std::scoped_lock Guard{ ScaleLock };

        auto&      Speed = Field(Clip, ClipPlaybackSpeedOffset);
        const auto Found = std::ranges::find(Scaled, static_cast<const void*>(Clip), &ScaledClip::Clip);

        if (Found != Scaled.end() && Speed == Found->Written)
            Speed = Found->Base;
    }

    void Untrack(const void* Clip)
    {
        std::erase_if(Clips, [&](const TrackedClip& Tracked) { return Tracked.Clip == Clip; });
        TrackedCount = Clips.size();
        std::erase_if(Jumps, [&](const PendingJump& Jump) { return Jump.Clip == Clip; });
        JumpCount = Jumps.size();
        std::erase_if(ActorClips, [&](const ActorClip& Tracked) { return Tracked.Clip == Clip; });
        ActorClipCount = ActorClips.size();
    }

    void TrackActorClip(const void* Clip, const void* Character)
    {
        if (ActorClipCount.load(std::memory_order_relaxed) > 0)
        {
            const std::scoped_lock Guard{ Lock };
            std::erase_if(ActorClips, [&](const ActorClip& Tracked) { return Tracked.Clip == Clip; });
            ActorClipCount = ActorClips.size();
        }

        if (!Character || !HasReloadMarks(Clip))
            return;

        const auto Owner = Hooks::ActorOfCharacter(Character);

        if (!Owner)
            return;

        const std::scoped_lock Guard{ Lock };

        if (ActorClips.size() >= MaxActorClips)
            ActorClips.erase(ActorClips.begin());

        ActorClips.push_back({ Clip, Character, Owner });
        ActorClipCount = ActorClips.size();
    }

    [[nodiscard]] std::vector<const void*> ReloadClipsOn(const void* Character, bool Player)
    {
        std::vector<const void*> Found;

        if (!Character)
            return Found;

        if (Player)
        {
            for (const auto& Tracked : Clips)
            {
                if (Tracked.Character == Character)
                    Found.push_back(Tracked.Clip);
            }
        }
        else
        {
            for (const auto& Tracked : ActorClips)
            {
                if (Tracked.Character == Character)
                    Found.push_back(Tracked.Clip);
            }
        }

        return Found;
    }

    struct Activate
    {
        static void Thunk(void* Clip, const void* Context)
        {
            Original(Clip, Context);

            const auto Character = Context ? Read<const void*>(Context, 0) : nullptr;
            MagicAnnotations::OnClipActivated(Clip, Character);

            if (!IsPlayerCharacter(Character))
            {
                TrackActorClip(Clip, Character);
                return;
            }

            ArmSpace::OnClipActivated(Clip, Character);
            AimDownSight::OnClipActivated(Clip);

            const std::scoped_lock Guard{ Lock };
            Untrack(Clip);
            Clips.push_back({ Clip, Character });
            TrackedCount = Clips.size();
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct Update
    {
        static void Thunk(void* Clip, const void* Context, float Delta)
        {
            const bool Tactical = TacticalSprint::Active();
            const bool Jumping = JumpCount.load(std::memory_order_relaxed) > 0;
            const bool Restoring = ScaledCount.load(std::memory_order_relaxed) > 0;

            bool  Diagnose = false;
            float Playback = 1.0f;
            float BeforeUpdate = 0.0f;

            std::string_view Node;

            if (Jumping)
                ApplyJump(Clip);

            if ((Tactical || Restoring) && IsPlayerCharacter(Context ? Read<const void*>(Context, 0) : nullptr))
            {
                const bool ThirdPerson = Tactical && TacticalSprint::ThirdPerson();

                if (const auto Name = reinterpret_cast<const char*>(Read<std::uintptr_t>(Clip, NodeNameOffset) & ~std::uintptr_t{ 1 }))
                    Node = Name;

                if (Tactical && !ThirdPerson && !Node.empty())
                    Delta *= TacticalSprint::ClipTimeScale(Node);

                if (ThirdPerson)
                    Playback = TacticalSprint::ThirdPersonPlaybackScale(Node, AnimationFile(Clip));

                if (Playback != 1.0f)
                {
                    RestorePlaybackBase(Clip);
                    Delta *= Playback;
                }
                else if (Restoring)
                {
                    ScalePlayback(Clip, 1.0f);
                }

                Diagnose = ThirdPerson && Settings::VerboseLog();
                BeforeUpdate = Read<float>(Clip, ClipPlaybackSpeedOffset);
            }

            Original(Clip, Context, Delta);

            if (Playback == 1.0f)
            {
                if (Diagnose)
                    TacticalSprint::NoteThirdPersonClip(Node, AnimationFile(Clip), false, BeforeUpdate, BeforeUpdate, BeforeUpdate);

                return;
            }

            const float AfterUpdate = Read<float>(Clip, ClipPlaybackSpeedOffset);
            ScalePlayback(Clip, Playback);

            if (Diagnose)
                TacticalSprint::NoteThirdPersonClip(Node, AnimationFile(Clip), true, BeforeUpdate, AfterUpdate, Read<float>(Clip, ClipPlaybackSpeedOffset));
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct Deactivate
    {
        static void Thunk(void* Clip, const void* Context)
        {
            if (TrackedCount.load(std::memory_order_relaxed) > 0 || JumpCount.load(std::memory_order_relaxed) > 0 ||
                ActorClipCount.load(std::memory_order_relaxed) > 0)
            {
                const std::scoped_lock Guard{ Lock };
                Untrack(Clip);
            }

            if (ScaledCount.load(std::memory_order_relaxed) > 0)
                ScalePlayback(Clip, 1.0f);

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
        Update::Original = Vtable.write_vfunc(UpdateSlot, Update::Thunk);
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
            Jumps.clear();
            JumpCount = 0;
        }
    }

    std::optional<std::uint32_t> InsertShellsAfterCallLoop(RE::Actor* Actor)
    {
        const bool Player = !Actor || Actor == RE::PlayerCharacter::GetSingleton();
        const auto Character = Player ? ActiveCharacter.load(std::memory_order_relaxed) : CurrentCharacterOf(Actor);

        const std::scoped_lock Guard{ Lock };

        std::optional<std::uint32_t> Ahead;
        float                        Nearest = (std::numeric_limits<float>::max)();
        std::size_t                  WithAnimation = 0;
        std::size_t                  WithMarks = 0;

        const auto Candidates = ReloadClipsOn(Character, Player);

        for (const auto Clip : Candidates)
        {
            bool       HasAnimation = false;
            const auto Marks = ReloadMarks(Clip, HasAnimation);
            WithAnimation += HasAnimation ? 1 : 0;

            if (Marks.empty())
                continue;

            ++WithMarks;

            const auto Local = Read<float>(Clip, ClipLocalTimeOffset);
            const auto Crossed = LastCrossed(Marks, Local, ReloadMarkKind::CallLoop);

            if (!Crossed)
                continue;

            const float Since = Local - Marks[*Crossed].Time;

            if (Since < Nearest)
            {
                Nearest = Since;
                Ahead = static_cast<std::uint32_t>(std::ranges::count_if(Marks, [&](const ReloadMark& Current)
                    { return Current.Kind == ReloadMarkKind::InsertShell && Current.Time > Local + CrossedTolerance; }));
            }
        }

        if (Settings::VerboseLog())
        {
            const auto FormID = Actor ? Actor->GetFormID() : 0x14u;

            if (Ahead)
            {
                REX::INFO("MWB_Reload.CallLoop: {:08X} has {} InsertShell after it in this clip", FormID, *Ahead);
            }
            else
            {
                REX::INFO("MWB_Reload.CallLoop: no clip found for {:08X}, no InsertShell taken as ahead ({} clip(s) on the active graph, {} with an "
                          "animation, {} with MWB_Reload; active character {:X})",
                    FormID, Candidates.size(), WithAnimation, WithMarks, reinterpret_cast<std::uintptr_t>(Character));
            }
        }

        return Ahead;
    }

    void SkipToLastInsertShell(RE::Actor* Actor)
    {
        const bool Player = !Actor || Actor == RE::PlayerCharacter::GetSingleton();
        const auto Character = Player ? ActiveCharacter.load(std::memory_order_relaxed) : CurrentCharacterOf(Actor);

        const std::scoped_lock Guard{ Lock };

        for (const auto Clip : ReloadClipsOn(Character, Player))
        {
            bool       HasAnimation = false;
            const auto Marks = ReloadMarks(Clip, HasAnimation);
            const auto Local = Read<float>(Clip, ClipLocalTimeOffset);
            const auto Crossed = LastCrossed(Marks, Local, ReloadMarkKind::InsertShell);
            const auto Last = LastCrossed(Marks, (std::numeric_limits<float>::max)(), ReloadMarkKind::InsertShell);

            if (!Crossed || !Last || *Crossed == *Last || Local - Marks[*Crossed].Time > JustCrossed)
                continue;

            const float Target = Marks[*Last].Time;

            if (Target <= Local)
                continue;

            std::erase_if(Jumps, [&](const PendingJump& Jump) { return Jump.Clip == Clip; });
            Jumps.push_back({ Clip, Target });
            JumpCount = Jumps.size();

            if (Settings::VerboseLog())
                REX::INFO("MWB_Reload: {:08X} has no shell missing, the clip skips from {:.3f}s to the last InsertShell at {:.3f}s",
                    Actor ? Actor->GetFormID() : 0x14u, Local, Target);
        }
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
        Jumps.clear();
        JumpCount = 0;
        ActorClips.clear();
        ActorClipCount = 0;
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
