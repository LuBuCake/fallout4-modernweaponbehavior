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

#include "WeaponBones.h"
#include "Settings/Settings.h"

#include <numbers>

namespace
{
    constexpr std::ptrdiff_t GraphCharacter = 0x1C8;
    constexpr std::ptrdiff_t GraphSetup = 0x240;
    constexpr std::ptrdiff_t SetupSkeleton = 0x20;

    constexpr std::ptrdiff_t SkeletonBones = 0x28;
    constexpr std::ptrdiff_t SkeletonBoneCount = 0x30;

    struct HavokBone
    {
        std::uintptr_t Name;
        std::uint64_t LockTranslation;
    };
    static_assert(sizeof(HavokBone) == 0x10);

    [[nodiscard]] const char* NameOf(const HavokBone& Bone)
    {
        return reinterpret_cast<const char*>(Bone.Name & ~std::uintptr_t{ 1 });
    }

    constexpr std::ptrdiff_t TracksCount = 0x04;
    constexpr std::ptrdiff_t PoseHeader = 0x30;
    constexpr std::ptrdiff_t HeaderNumData = 0x02;
    constexpr std::ptrdiff_t HeaderDataOffset = 0x04;
    constexpr std::ptrdiff_t HeaderOnFraction = 0x08;
    constexpr std::ptrdiff_t HeaderFlags = 0x0C;
    constexpr std::uint8_t HeaderFlagNotPose = 0x10;
    constexpr std::size_t TransformFloats = 12;

    constexpr std::size_t MaxGraphs = 6;

    constexpr std::size_t LayerCount = static_cast<std::size_t>(WeaponBones::Layer::Count);

    constexpr std::array<std::string_view, LayerCount> LayerNames{ "Slide lock"sv, "Fire mode (auto)"sv, "Fire mode (single)"sv };

    template <class T>
    [[nodiscard]] T Read(const void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    struct Target
    {
        const void* Character{ nullptr };
        std::int16_t Bone{ 0 };
        std::array<float, 3> Offset{};
        std::array<float, 3> Axis{};
        float Angle{ 0.0f };
        float Weight{ 0.0f };
    };

    using Quaternion = std::array<float, 4>;

    [[nodiscard]] Quaternion Multiply(const Quaternion& A, const Quaternion& B)
    {
        return { A[3] * B[0] + A[0] * B[3] + A[1] * B[2] - A[2] * B[1],
            A[3] * B[1] - A[0] * B[2] + A[1] * B[3] + A[2] * B[0],
            A[3] * B[2] + A[0] * B[1] - A[1] * B[0] + A[2] * B[3],
            A[3] * B[3] - A[0] * B[0] - A[1] * B[1] - A[2] * B[2] };
    }

    [[nodiscard]] Quaternion AboutAxis(const std::array<float, 3>& Axis, float Angle)
    {
        const float HalfSine = std::sin(Angle * 0.5f);

        return { Axis[0] * HalfSine, Axis[1] * HalfSine, Axis[2] * HalfSine, std::cos(Angle * 0.5f) };
    }

    void SetRotation(Target& BoneTarget, const std::array<float, 3>& Degrees)
    {
        constexpr float Radians = std::numbers::pi_v<float> / 180.0f;
        auto Rotation = Multiply(Multiply(AboutAxis({ 1.0f, 0.0f, 0.0f }, Degrees[0] * Radians), AboutAxis({ 0.0f, 1.0f, 0.0f }, Degrees[1] * Radians)),
            AboutAxis({ 0.0f, 0.0f, 1.0f }, Degrees[2] * Radians));

        if (Rotation[3] < 0.0f)
            Rotation = { -Rotation[0], -Rotation[1], -Rotation[2], -Rotation[3] };

        const float SinHalf = std::sqrt(Rotation[0] * Rotation[0] + Rotation[1] * Rotation[1] + Rotation[2] * Rotation[2]);

        if (SinHalf < 1e-6f)
            return;

        BoneTarget.Axis = { Rotation[0] / SinHalf, Rotation[1] / SinHalf, Rotation[2] / SinHalf };
        BoneTarget.Angle = 2.0f * std::atan2(SinHalf, Rotation[3]);
    }

    struct GraphKey
    {
        const void* Graph{ nullptr };
        const void* Skeleton{ nullptr };
        const char* Person{ "" };

        bool operator==(const GraphKey& Other) const
        {
            return Graph == Other.Graph && Skeleton == Other.Skeleton;
        }
    };

    using GraphKeys = std::array<GraphKey, MaxGraphs>;

    struct LayerState
    {
        std::vector<WeaponConfig::BoneOffset> Bones;
        std::uint32_t WeaponID{ 0 };
        std::vector<Target> Built;
        float Weight{ 0.0f };
    };

    std::array<std::atomic<const void*>, MaxGraphs> Characters{};
    std::mutex Lock;
    std::vector<Target> Targets;

    GraphKeys Graphs{};
    std::array<LayerState, LayerCount> Layers{};

    [[nodiscard]] std::span<const HavokBone> BonesOf(const GraphKey& Key)
    {
        if (!Key.Skeleton)
            return {};

        const auto Bones = Read<const HavokBone*>(Key.Skeleton, SkeletonBones);
        const auto Count = Read<std::int32_t>(Key.Skeleton, SkeletonBoneCount);

        if (!Bones || Count <= 0)
            return {};

        return { Bones, static_cast<std::size_t>(Count) };
    }

    [[nodiscard]] GraphKeys CollectGraphs(RE::PlayerCharacter* Player)
    {
        GraphKeys Keys{};
        std::size_t Used = 0;

        const auto Add = [&](const void* Graph, const char* Person)
        {
            if (!Graph || Used >= MaxGraphs || std::ranges::any_of(Keys, [&](const GraphKey& Key) { return Key.Graph == Graph; }))
                return;

            const auto Setup = Read<const void*>(Graph, GraphSetup);
            Keys[Used++] = { Graph, Setup ? Read<const void*>(Setup, SetupSkeleton) : nullptr, Person };
        };

        const std::array<std::pair<const RE::BipedAnim*, const char*>, 2> Bipeds{ { { Player->firstPersonBipedAnim.get(), "first person" },
            { Player->biped.get(), "third person" } } };

        for (const auto& [Biped, Person] : Bipeds)
        {
            if (!Biped)
                continue;

            for (auto Slot = std::to_underlying(RE::BIPED_OBJECT::kWeaponHand); Slot < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++Slot)
            {
                const auto Holder = Biped->object[Slot].objectGraphManager.get();
                const auto Manager = Holder ? Holder->animationGraphManager.get() : nullptr;

                if (!Manager)
                    continue;

                for (std::uint32_t i = 0; i < Manager->graph.size(); ++i)
                    Add(Manager->graph[i].get(), Person);
            }
        }

        RE::BSTSmartPointer<RE::BSAnimationGraphManager> Manager;

        if (Player->GetAnimationGraphManagerImpl(Manager) && Manager)
        {
            for (std::uint32_t i = 0; i < Manager->graph.size(); ++i)
                Add(Manager->graph[i].get(), "player's");
        }

        return Keys;
    }

    void LogGraphs(const GraphKeys& Keys)
    {
        for (const auto& Key : Keys)
        {
            if (!Key.Graph)
                continue;

            std::string Names;

            for (const auto& Bone : BonesOf(Key))
            {
                const auto Name = NameOf(Bone);
                Names += std::format("{}{}", Names.empty() ? "" : ", ", Name ? Name : "?");
            }

            REX::INFO("Weapon bones: {} graph {:X}, {} bones{}{}", Key.Person, reinterpret_cast<std::uintptr_t>(Key.Graph), BonesOf(Key).size(),
                Names.empty() ? "" : ": ", Names);
        }
    }

    [[nodiscard]] bool IsBone(const HavokBone& Bone, const RE::BSFixedString& Name)
    {
        const auto BoneName = NameOf(Bone);

        return BoneName && _stricmp(BoneName, Name.c_str()) == 0;
    }

    void Build(std::size_t Index)
    {
        auto& Layer = Layers[Index];
        Layer.Built.clear();

        bool AnyGraph = false;

        for (const auto& Bone : Layer.Bones)
        {
            bool Found = false;

            for (const auto& Key : Graphs)
            {
                const auto Bones = BonesOf(Key);
                AnyGraph |= !Bones.empty();

                for (std::size_t i = 0; i < Bones.size(); ++i)
                {
                    if (IsBone(Bones[i], Bone.Name))
                    {
                        Target NewTarget{ reinterpret_cast<const std::byte*>(Key.Graph) + GraphCharacter, static_cast<std::int16_t>(i), Bone.Offset };
                        SetRotation(NewTarget, Bone.Rotation);
                        Layer.Built.push_back(NewTarget);
                        Found = true;

                        if (Settings::VerboseLog())
                        {
                            REX::INFO("{}: '{}' is bone {} of the {} graph {:X}", LayerNames[Index], Bone.Name.c_str(), i, Key.Person,
                                reinterpret_cast<std::uintptr_t>(Key.Graph));
                        }

                        break;
                    }
                }
            }

            if (!Found && AnyGraph)
            {
                REX::WARN("{}: no bone '{}' in the animation graphs of weapon {:08X} (bVerboseLog lists them)", LayerNames[Index], Bone.Name.c_str(),
                    Layer.WeaponID);
            }
        }
    }

    void Clear()
    {
        for (auto& Character : Characters)
            Character.store(nullptr, std::memory_order_relaxed);

        const std::scoped_lock Guard{ Lock };
        Targets.clear();
    }
}

namespace WeaponBones
{
    float Smooth(float X)
    {
        const float Clamped = std::clamp(X, 0.0f, 1.0f);

        return Clamped * Clamped * (3.0f - 2.0f * Clamped);
    }

    void BeginFrame(RE::PlayerCharacter* Player)
    {
        if (!Player)
            return;

        const auto Collected = CollectGraphs(Player);

        if (Collected == Graphs)
            return;

        Graphs = Collected;
        const bool AnyBones = std::ranges::any_of(Layers, [](const LayerState& State) { return !State.Bones.empty(); });

        if (Settings::VerboseLog() && AnyBones)
            LogGraphs(Graphs);

        for (std::size_t i = 0; i < LayerCount; ++i)
            Build(i);
    }

    void SetBones(Layer TargetLayer, const std::vector<WeaponConfig::BoneOffset>& Bones, std::uint32_t WeaponID)
    {
        auto& State = Layers[static_cast<std::size_t>(TargetLayer)];
        const bool HadBones = std::ranges::any_of(Layers, [](const LayerState& Candidate) { return !Candidate.Bones.empty(); });
        State.Bones = Bones;
        State.WeaponID = WeaponID;

        if (!HadBones && !Bones.empty() && Settings::VerboseLog())
            LogGraphs(Graphs);

        Build(static_cast<std::size_t>(TargetLayer));
    }

    void SetWeight(Layer TargetLayer, float Weight)
    {
        Layers[static_cast<std::size_t>(TargetLayer)].Weight = std::clamp(Weight, 0.0f, 1.0f);
    }

    void EndFrame()
    {
        std::vector<Target> Weighted;

        for (const auto& State : Layers)
        {
            if (State.Weight <= 0.0f)
                continue;

            for (auto Entry : State.Built)
            {
                Entry.Weight = State.Weight;
                Weighted.push_back(Entry);
            }
        }

        {
            const std::scoped_lock Guard{ Lock };
            Targets = Weighted;
        }

        for (std::size_t i = 0; i < MaxGraphs; ++i)
        {
            const void* Character = nullptr;

            if (Graphs[i].Graph)
            {
                const auto Candidate = reinterpret_cast<const std::byte*>(Graphs[i].Graph) + GraphCharacter;

                if (std::ranges::any_of(Weighted, [&](const Target& Entry) { return Entry.Character == Candidate; }))
                    Character = Candidate;
            }

            Characters[i].store(Character, std::memory_order_relaxed);
        }
    }

    void OnGenerate(const void* Character, void* Output)
    {
        if (std::ranges::none_of(Characters, [&](const auto& Slot) { return Slot.load(std::memory_order_relaxed) == Character; }))
            return;

        const auto Tracks = Read<std::byte*>(Output, 0);

        if (!Tracks || Read<std::int32_t>(Tracks, TracksCount) < 3)
            return;

        const auto Header = Tracks + PoseHeader;

        if (Read<float>(Header, HeaderOnFraction) <= 0.0f || (Read<std::uint8_t>(Header, HeaderFlags) & HeaderFlagNotPose) != 0)
            return;

        const auto NumData = Read<std::int16_t>(Header, HeaderNumData);
        const auto Pose = reinterpret_cast<float*>(Tracks + Read<std::int16_t>(Header, HeaderDataOffset));

        const std::scoped_lock Guard{ Lock };

        for (const auto& Entry : Targets)
        {
            if (Entry.Character != Character || Entry.Bone >= NumData)
                continue;

            const auto Transform = Pose + static_cast<std::size_t>(Entry.Bone) * TransformFloats;

            for (std::size_t i = 0; i < 3; ++i)
                Transform[i] += Entry.Weight * Entry.Offset[i];

            if (Entry.Angle != 0.0f)
            {
                const auto Rotation = Transform + 4;
                const auto Turned = Multiply({ Rotation[0], Rotation[1], Rotation[2], Rotation[3] }, AboutAxis(Entry.Axis, Entry.Weight * Entry.Angle));
                std::ranges::copy(Turned, Rotation);
            }
        }
    }

    void Reset()
    {
        Clear();
        Graphs = {};
        Layers = {};
    }
}
