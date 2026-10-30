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

#include "ArmSpace.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::ptrdiff_t NodeName = 0x38;
    constexpr std::ptrdiff_t NodeUserData = 0x30;
    constexpr std::uintptr_t DynamicClipUserData = 8;
    constexpr std::ptrdiff_t ClipAnimationControl = 0xD0;
    constexpr std::ptrdiff_t ClipLocalTime = 0x140;
    constexpr std::ptrdiff_t ControlBinding = 0x38;
    constexpr std::ptrdiff_t BindingAnimation = 0x18;
    constexpr std::ptrdiff_t BindingTrackToBone = 0x20;
    constexpr std::ptrdiff_t AnimationDuration = 0x14;
    constexpr std::ptrdiff_t AnimationTransformTracks = 0x18;
    constexpr std::size_t SamplePartialTracks = 5;

    constexpr std::ptrdiff_t TracksCount = 0x04;
    constexpr std::ptrdiff_t PoseHeader = 0x30;
    constexpr std::ptrdiff_t HeaderNumData = 0x02;
    constexpr std::ptrdiff_t HeaderDataOffset = 0x04;
    constexpr std::ptrdiff_t HeaderOnFraction = 0x08;

    constexpr std::int16_t COM = 1;
    constexpr std::int16_t Spine1 = 9;
    constexpr std::int16_t Spine2 = 10;
    constexpr std::int16_t Chest = 11;
    constexpr std::int16_t LeftCollarbone = 14;
    constexpr std::int16_t RightCollarbone = 20;
    constexpr std::int16_t Camera = 78;
    constexpr std::array<std::int16_t, 5> SampledBones{ COM, Spine1, Spine2, Chest, Camera };
    constexpr std::array<std::int16_t, 6> LeftChain{ 14, 15, 16, 17, 18, 19 };
    constexpr std::array<std::int16_t, 6> RightChain{ 20, 21, 22, 23, 24, 26 };

    constexpr float BlendIn = 0.1f;
    constexpr float BlendOut = 0.1f;

    constexpr float RadiansToDegrees = 57.2957795f;

    template <class T>
    [[nodiscard]] T Read(const void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    struct alignas(16) QsTransform
    {
        float Translation[4];
        float Rotation[4];
        float Scale[4];
    };
    static_assert(sizeof(QsTransform) == 0x30);

    struct Transform
    {
        std::array<float, 3> T{};
        std::array<float, 4> Q{ 0.0f, 0.0f, 0.0f, 1.0f };
    };

    [[nodiscard]] std::array<float, 4> Multiply(const std::array<float, 4>& A, const std::array<float, 4>& B)
    {
        return { A[3] * B[0] + A[0] * B[3] + A[1] * B[2] - A[2] * B[1],
            A[3] * B[1] - A[0] * B[2] + A[1] * B[3] + A[2] * B[0],
            A[3] * B[2] + A[0] * B[1] - A[1] * B[0] + A[2] * B[3],
            A[3] * B[3] - A[0] * B[0] - A[1] * B[1] - A[2] * B[2] };
    }

    [[nodiscard]] std::array<float, 3> Rotate(const std::array<float, 4>& Q, const std::array<float, 3>& V)
    {
        const auto P = Multiply(Multiply(Q, { V[0], V[1], V[2], 0.0f }), { -Q[0], -Q[1], -Q[2], Q[3] });

        return { P[0], P[1], P[2] };
    }

    [[nodiscard]] Transform Compose(const Transform& A, const Transform& B)
    {
        const auto R = Rotate(A.Q, B.T);

        return { { A.T[0] + R[0], A.T[1] + R[1], A.T[2] + R[2] }, Multiply(A.Q, B.Q) };
    }

    [[nodiscard]] Transform Inverse(const Transform& A)
    {
        const std::array<float, 4> Q{ -A.Q[0], -A.Q[1], -A.Q[2], A.Q[3] };
        const auto R = Rotate(Q, A.T);

        return { { -R[0], -R[1], -R[2] }, Q };
    }

    [[nodiscard]] Transform Partial(const Transform& A, float Weight)
    {
        auto Q = A.Q;

        if (Q[3] < 0.0f)
            Q = { -Q[0], -Q[1], -Q[2], -Q[3] };

        const float Half = std::acos((std::min)(1.0f, Q[3]));
        const float SinHalf = std::sin(Half);
        Transform Result;

        if (SinHalf > 1e-5f)
        {
            const float S = std::sin(Half * Weight) / SinHalf;
            Result.Q = { Q[0] * S, Q[1] * S, Q[2] * S, std::cos(Half * Weight) };
        }

        for (std::size_t i = 0; i < 3; ++i)
            Result.T[i] = A.T[i] * Weight;

        return Result;
    }

    [[nodiscard]] Transform FromQs(const QsTransform& A)
    {
        return { { A.Translation[0], A.Translation[1], A.Translation[2] }, { A.Rotation[0], A.Rotation[1], A.Rotation[2], A.Rotation[3] } };
    }

    void ToQs(const Transform& A, QsTransform& Out)
    {
        for (std::size_t i = 0; i < 3; ++i)
            Out.Translation[i] = A.T[i];

        for (std::size_t i = 0; i < 4; ++i)
            Out.Rotation[i] = A.Q[i];
    }

    [[nodiscard]] float Smooth(float Value)
    {
        const float X = std::clamp(Value, 0.0f, 1.0f);

        return X * X * (3.0f - 2.0f * X);
    }

    struct ArmClip
    {
        const void* Clip{ nullptr };
        const void* Character{ nullptr };
        std::int16_t Collarbone{ 0 };
        float StopTime{ -1.0f };
        bool Logged{ false };

        bool Pending{ false };
        float Time{ 0.0f };
        float Weight{ 0.0f };
        std::array<float, 3> WantHand{};
        std::array<float, 3> WeaponChestPos{};
        std::array<float, 3> ArmChestPos{};
        float ChestAngle{ 0.0f };
        float CorrectionAngle{ 0.0f };
        float CorrectionOffset{ 0.0f };
    };

    std::mutex Lock;
    std::array<ArmClip, 2> Arms;
    std::array<std::atomic<const void*>, 2> WatchedClips{};
    std::atomic<const void*> WatchedCharacter{ nullptr };

    std::optional<RE::BSFixedString> StopEvent;

    const void* VanillaClip{ nullptr };
    const void* VanillaCharacter{ nullptr };
    std::atomic<const void*> VanillaAtomic{ nullptr };
    const void* LastLoggedClip{ nullptr };
    int LastLoggedStep{ -1 };
    constexpr float LogStep = 0.25f;

    std::optional<RE::BSFixedString> CameraName;
    std::optional<RE::BSFixedString> ChestName;
    std::optional<RE::BSFixedString> LeftHandName;
    std::optional<RE::BSFixedString> RightHandName;
    std::optional<RE::BSFixedString> LeftCollarboneName;
    std::optional<RE::BSFixedString> RightCollarboneName;

    constexpr std::ptrdiff_t FirstPersonStateCamera = 0x50;

    [[nodiscard]] std::size_t SlotOf(std::int16_t Collarbone)
    {
        return Collarbone == LeftCollarbone ? 0 : 1;
    }

    void Clear(std::size_t Slot)
    {
        Arms[Slot] = {};
        WatchedClips[Slot] = nullptr;

        if (!Arms[1 - Slot].Clip)
            WatchedCharacter = nullptr;
    }

    void ClearAll()
    {
        Clear(0);
        Clear(1);
    }

    [[nodiscard]] std::int16_t CollarboneOf(const void* Clip)
    {
        const auto Name = reinterpret_cast<const char*>(Read<std::uintptr_t>(Clip, NodeName) & ~std::uintptr_t{ 1 });

        if (!Name)
            return 0;

        const std::string_view Text{ Name };

        if (!Text.ends_with("Clip"sv))
            return 0;

        if (Text.starts_with("MWB_LeftArm"sv))
            return LeftCollarbone;

        if (Text.starts_with("MWB_RightArm"sv))
            return RightCollarbone;

        return 0;
    }

    [[nodiscard]] bool SampleArm(const void* Animation, const void* Binding, float Time, const std::array<std::int16_t, 6>& Chain,
        Transform& ArmChest, Transform& ArmCamera, Transform& ArmHand)
    {
        struct HkArrayI16
        {
            const std::int16_t* Data;
            std::int32_t Size;
            std::int32_t Capacity;
        };

        const auto TrackCount = Read<std::int32_t>(Animation, AnimationTransformTracks);
        const auto Map = Read<HkArrayI16>(Binding, BindingTrackToBone);

        constexpr std::size_t Count = SampledBones.size() + 6;
        std::array<std::int16_t, Count> Bones{};
        std::ranges::copy(SampledBones, Bones.begin());
        std::ranges::copy(Chain, Bones.begin() + SampledBones.size());

        std::array<std::int16_t, Count> Tracks{};

        for (std::size_t i = 0; i < Count; ++i)
        {
            std::int32_t Track = -1;

            if (Map.Size == 0)
            {
                Track = Bones[i];
            }
            else
            {
                for (std::int32_t j = 0; j < Map.Size; ++j)
                {
                    if (Map.Data[j] == Bones[i])
                    {
                        Track = j;
                        break;
                    }
                }
            }

            if (Track < 0 || Track >= TrackCount)
                return false;

            Tracks[i] = static_cast<std::int16_t>(Track);
        }

        const auto TracksToSample = static_cast<std::uint32_t>(*std::ranges::max_element(Tracks)) + 1;
        thread_local std::vector<QsTransform> SampledTracks;
        SampledTracks.resize(TracksToSample);
        float NoFloat = 0.0f;

        using SampleSignature = void (*)(const void*, float, std::uint32_t, QsTransform*, std::uint32_t, float*);
        const auto Vtable = *reinterpret_cast<const SampleSignature* const*>(Animation);
        Vtable[SamplePartialTracks](Animation, Time, TracksToSample, SampledTracks.data(), 0, &NoFloat);

        const auto TrackTransform = [&](std::size_t Index)
        {
            return FromQs(SampledTracks[static_cast<std::size_t>(Tracks[Index])]);
        };

        ArmChest = Compose(Compose(Compose(TrackTransform(0), TrackTransform(1)), TrackTransform(2)), TrackTransform(3));
        ArmCamera = TrackTransform(4);
        ArmHand = ArmChest;

        for (std::size_t i = SampledBones.size(); i < Count; ++i)
            ArmHand = Compose(ArmHand, TrackTransform(i));

        return true;
    }

    [[nodiscard]] float Angle(const Transform& A)
    {
        return 2.0f * std::acos((std::min)(1.0f, std::abs(A.Q[3]))) * RadiansToDegrees;
    }

    [[nodiscard]] float Length(const std::array<float, 3>& A)
    {
        return std::sqrt(A[0] * A[0] + A[1] * A[1] + A[2] * A[2]);
    }

    void Correct(ArmClip& Arm, void* Output)
    {
        const auto Tracks = Read<std::byte*>(Output, 0);

        if (!Tracks || Read<std::int32_t>(Tracks, TracksCount) < 3)
            return;

        const auto Header = Tracks + PoseHeader;
        const auto NumData = Read<std::int16_t>(Header, HeaderNumData);
        const bool PartialPose = NumData <= Camera || Read<float>(Header, HeaderOnFraction) <= 0.0f;

        if (PartialPose)
            return;

        const auto Pose = reinterpret_cast<QsTransform*>(Tracks + Read<std::int16_t>(Header, HeaderDataOffset));

        const auto ClipNode = Arm.Clip;
        const auto Control = Read<const void*>(ClipNode, ClipAnimationControl);
        const auto Binding = Control ? Read<const void*>(Control, ControlBinding) : nullptr;
        const auto Animation = Binding ? Read<const void*>(Binding, BindingAnimation) : nullptr;

        if (!Animation)
            return;

        const float Time = Read<float>(ClipNode, ClipLocalTime);
        const float Duration = Read<float>(Animation, AnimationDuration);
        float Weight = (std::min)(Smooth(Time / BlendIn), Smooth((Duration - Time) / BlendOut));

        if (Arm.StopTime >= 0.0f)
            Weight = (std::min)(Weight, Smooth(1.0f - (Time - Arm.StopTime) / BlendOut));

        if (Weight <= 0.0f)
            return;

        const auto& Chain = Arm.Collarbone == LeftCollarbone ? LeftChain : RightChain;
        Transform ArmChest, ArmCamera, ArmHand;

        if (!SampleArm(Animation, Binding, Time, Chain, ArmChest, ArmCamera, ArmHand))
        {
            if (!Arm.Logged && Settings::VerboseLog())
                REX::INFO("Arm space: the arm animation has no track for one of the sampled bones; not corrected");

            Arm.Logged = true;
            return;
        }

        const auto WeaponChest = Compose(Compose(Compose(FromQs(Pose[COM]), FromQs(Pose[Spine1])), FromQs(Pose[Spine2])), FromQs(Pose[Chest]));
        const auto WeaponCamera = FromQs(Pose[Camera]);
        const auto Correction = Compose(Compose(Compose(Inverse(WeaponChest), WeaponCamera), Inverse(ArmCamera)), ArmChest);

        auto& CollarbonePose = Pose[Arm.Collarbone];
        ToQs(Compose(Partial(Correction, Weight), FromQs(CollarbonePose)), CollarbonePose);

        if (Settings::VerboseLog())
        {
            const auto WeaponChestInCamera = Compose(Inverse(WeaponCamera), WeaponChest);
            const auto ArmChestInCamera = Compose(Inverse(ArmCamera), ArmChest);
            Arm.Pending = true;
            Arm.Time = Time;
            Arm.Weight = Weight;
            Arm.WantHand = Compose(Inverse(ArmCamera), ArmHand).T;
            Arm.WeaponChestPos = WeaponChestInCamera.T;
            Arm.ArmChestPos = ArmChestInCamera.T;
            Arm.ChestAngle = Angle(Compose(Inverse(ArmChestInCamera), WeaponChestInCamera));
            Arm.CorrectionAngle = Angle(Correction);
            Arm.CorrectionOffset = Length(Correction.T);
        }
    }

    [[nodiscard]] std::array<float, 3> Relative(const RE::NiAVObject* From, const RE::NiAVObject* To, bool Transpose)
    {
        const auto& FromWorld = From->world;
        const RE::NiPoint3 Delta{ To->world.translate.x - FromWorld.translate.x, To->world.translate.y - FromWorld.translate.y,
            To->world.translate.z - FromWorld.translate.z };
        const auto Rotation = Transpose ? FromWorld.rotate.Transpose() : FromWorld.rotate;
        const auto Local = Rotation * Delta;
        const float Scale = FromWorld.scale != 0.0f ? FromWorld.scale : 1.0f;

        return { Local.x / Scale, Local.y / Scale, Local.z / Scale };
    }

    [[nodiscard]] std::string Format(const std::array<float, 3>& A)
    {
        return std::format("({:.1f}, {:.1f}, {:.1f})", A[0], A[1], A[2]);
    }

    [[nodiscard]] std::array<float, 3> Minus(const std::array<float, 3>& A, const std::array<float, 3>& B)
    {
        return { A[0] - B[0], A[1] - B[1], A[2] - B[2] };
    }
}

namespace ArmSpace
{
    void Install()
    {
        StopEvent.emplace("MWB_ArmStop");
        CameraName.emplace("Camera");
        ChestName.emplace("Chest");
        LeftHandName.emplace("LArm_Hand");
        RightHandName.emplace("RArm_Hand");
        LeftCollarboneName.emplace("LArm_Collarbone");
        RightCollarboneName.emplace("RArm_Collarbone");
    }

    void OnGenerate(const void* Character, void* Output)
    {
        if (Character != WatchedCharacter.load(std::memory_order_relaxed))
            return;

        const std::scoped_lock Guard{ Lock };

        for (auto& Arm : Arms)
        {
            if (Arm.Clip && Arm.Character == Character)
                Correct(Arm, Output);
        }
    }

    void Diagnose(RE::PlayerCharacter* Player)
    {
        if (!Player || !Settings::VerboseLog() || !CameraName)
            return;

        ArmClip ArmSnapshot;
        const void* ShownClip = nullptr;
        float Time = 0.0f;

        {
            const std::scoped_lock Guard{ Lock };
            const auto& Arm = Arms[0].Clip ? Arms[0] : Arms[1];
            ShownClip = Arm.Clip ? Arm.Clip : VanillaClip;

            if (!ShownClip)
                return;

            Time = Read<float>(ShownClip, ClipLocalTime);

            if (ShownClip != LastLoggedClip)
            {
                LastLoggedClip = ShownClip;
                LastLoggedStep = -1;
            }

            const int Step = static_cast<int>(Time / LogStep);

            if (Step <= LastLoggedStep)
                return;

            LastLoggedStep = Step;
            ArmSnapshot = Arm;
        }

        const auto Root = Player->firstPerson3D.get();

        if (!Root)
            return;

        const auto CameraNode = Root->GetObjectByName(*CameraName);

        if (!CameraNode)
        {
            REX::INFO("Arm space diag: no Camera node in the first person skeleton");
            return;
        }

        const auto At = [&](const RE::BSFixedString& Name, bool Transpose)
        {
            const auto Node = Root->GetObjectByName(Name);

            return Node ? Format(Relative(CameraNode, Node, Transpose)) : std::string{ "?" };
        };

        const RE::NiAVObject* ViewCamera = nullptr;

        if (const auto CameraSingleton = RE::PlayerCamera::GetSingleton())
        {
            if (const auto FirstPersonCameraState = CameraSingleton->cameraStates[RE::CameraState::kFirstPerson].get())
                ViewCamera = *reinterpret_cast<RE::NiAVObject* const*>(reinterpret_cast<std::uintptr_t>(FirstPersonCameraState) + FirstPersonStateCamera);
        }

        const auto Eye = Player->firstPersonEye;
        const auto Source = !ArmSnapshot.Clip ? "vanilla"sv : ArmSnapshot.Collarbone == LeftCollarbone ? "left arm"sv : "right arm"sv;

        REX::INFO("Arm space diag [{}] t={:.2f}, Camera bone space: L hand {} R hand {} L collarbone {} R collarbone {} Chest {}; "
                  "NiCamera {}; eye node '{}'{}",
            Source, Time, At(*LeftHandName, false), At(*RightHandName, false), At(*LeftCollarboneName, false),
            At(*RightCollarboneName, false), At(*ChestName, false), ViewCamera ? Format(Relative(CameraNode, ViewCamera, false)) : "?",
            Eye ? Eye->name.c_str() : "?", Eye == CameraNode ? " (the Camera bone)" : "");

        if (ArmSnapshot.Clip && ArmSnapshot.Pending)
        {
            REX::INFO("Arm space diag [{}] t={:.2f} w={:.2f}: correction {:.1f} deg {:.1f} u; chest in camera space: weapon {}, arm animation {} "
                      "({:.1f} deg apart); hand wanted {}",
                Source, ArmSnapshot.Time, ArmSnapshot.Weight, ArmSnapshot.CorrectionAngle, ArmSnapshot.CorrectionOffset, Format(ArmSnapshot.WeaponChestPos),
                Format(ArmSnapshot.ArmChestPos), ArmSnapshot.ChestAngle, Format(ArmSnapshot.WantHand));
        }
    }

    void OnClipActivated(const void* Clip, const void* Character)
    {
        const auto Name = reinterpret_cast<const char*>(Read<std::uintptr_t>(Clip, NodeName) & ~std::uintptr_t{ 1 });

        if (const auto Collarbone = CollarboneOf(Clip))
        {
            {
                const std::scoped_lock Guard{ Lock };
                const auto Slot = SlotOf(Collarbone);
                Arms[Slot] = { Clip, Character, Collarbone, -1.0f, false };
                WatchedClips[Slot] = Clip;
                WatchedCharacter = Character;
            }

            if (Settings::VerboseLog())
                REX::INFO("Arm space diag: arm clip '{}' started", Name ? Name : "?");

            return;
        }

        if (Settings::VerboseLog() && Read<std::uintptr_t>(Clip, NodeUserData) == DynamicClipUserData)
        {
            {
                const std::scoped_lock Guard{ Lock };
                VanillaClip = Clip;
                VanillaAtomic = Clip;
                VanillaCharacter = Character;
            }

            REX::INFO("Arm space diag: dynamic clip '{}' started", Name ? Name : "?");
        }
    }

    void OnClipDeactivated(const void* Clip)
    {
        if (Clip == VanillaAtomic.load(std::memory_order_relaxed))
        {
            const std::scoped_lock Guard{ Lock };
            VanillaClip = nullptr;
            VanillaAtomic = nullptr;
            VanillaCharacter = nullptr;
            return;
        }

        for (std::size_t Slot = 0; Slot < Arms.size(); ++Slot)
        {
            if (Clip != WatchedClips[Slot].load(std::memory_order_relaxed))
                continue;

            const std::scoped_lock Guard{ Lock };

            if (Arms[Slot].Clip == Clip)
                Clear(Slot);
        }
    }

    void OnEventSent(const RE::BSFixedString& Event, bool Accepted)
    {
        if (!Accepted || !StopEvent || Event != *StopEvent || !WatchedCharacter.load(std::memory_order_relaxed))
            return;

        const std::scoped_lock Guard{ Lock };

        for (auto& Arm : Arms)
        {
            if (Arm.Clip && Arm.StopTime < 0.0f)
                Arm.StopTime = Read<float>(Arm.Clip, ClipLocalTime);
        }
    }

    void OnCharactersChanged(std::span<const void* const> Characters)
    {
        const std::scoped_lock Guard{ Lock };

        for (std::size_t Slot = 0; Slot < Arms.size(); ++Slot)
        {
            if (Arms[Slot].Character && std::ranges::find(Characters, Arms[Slot].Character) == Characters.end())
                Clear(Slot);
        }

        if (VanillaCharacter && std::ranges::find(Characters, VanillaCharacter) == Characters.end())
        {
            VanillaClip = nullptr;
            VanillaAtomic = nullptr;
            VanillaCharacter = nullptr;
        }
    }

    void Reset()
    {
        const std::scoped_lock Guard{ Lock };
        ClearAll();
        VanillaClip = nullptr;
        VanillaAtomic = nullptr;
        VanillaCharacter = nullptr;
    }
}
