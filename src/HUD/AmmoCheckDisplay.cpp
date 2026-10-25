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

#include "AmmoCheckDisplay.h"
#include "Actions/FirstPerson.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Settings/Settings.h"

#include <numbers>

namespace
{
    using Value = Scaleform::GFx::Value;

    constexpr auto Movie = "MWB_AmmoCheck";
    constexpr auto MenuName = "MWBAmmoCheckMenu"sv;

    constexpr std::array Clips{ "MWB_WPNAmmoCheck"sv, "MWB_WPNAmmoCheckEmpty"sv };
    constexpr auto ShowAnnotation = "MWB_AmmoCheckShow";
    constexpr auto HideAnnotation = "MWB_AmmoCheckHide";
    constexpr auto DefaultBoneName = "WeaponMagazine";

    enum class Fill
    {
        Empty = 0,
        LessThanHalf = 1,
        Half = 2,
        MoreThanHalf = 3,
        Full = 4,
        FullChambered = 5,
    };

    enum class Magazine
    {
        Rifle = 0,
        Pistol = 1,
        Energy = 2,
        Heavy = 3,
    };

    struct Frame
    {
        bool Visible{ false };
        Fill Level{ Fill::Empty };
        Magazine Kind{ Magazine::Rifle };
        bool AtCounter{ false };
        float X{ 0.5f };
        float Y{ 0.5f };
    };

    constexpr std::array EnergyKeywords{ "WeaponTypeLaser"sv, "WeaponTypePlasma"sv, "WeaponTypeGammaGun"sv, "WeaponTypeCryolator"sv };
    constexpr std::array HeavyKeywords{ "WeaponTypeHeavyGun"sv };
    constexpr std::array PistolKeywords{ "WeaponTypePistol"sv };
    constexpr std::array RifleKeywords{ "WeaponTypeRifle"sv };

    struct MagazineKeywords
    {
        std::vector<const RE::BGSKeyword*> Energy;
        std::vector<const RE::BGSKeyword*> Heavy;
        std::vector<const RE::BGSKeyword*> Pistol;
        std::vector<const RE::BGSKeyword*> Rifle;
        bool                               Looked{ false };
    };

    MagazineKeywords Keywords;

    template <std::size_t N>
    [[nodiscard]] std::vector<const RE::BGSKeyword*> FindKeywords(const std::array<std::string_view, N>& EditorIDs, std::string& Missing)
    {
        std::vector<const RE::BGSKeyword*> Found;

        for (const auto ID : EditorIDs)
        {
            if (const auto Keyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(ID))
                Found.push_back(Keyword);
            else
                Missing += std::format("{}{}", Missing.empty() ? "" : ", ", ID);
        }

        return Found;
    }

    [[nodiscard]] bool HasAny(const Core::EquippedWeapon& Weapon, const std::vector<const RE::BGSKeyword*>& List)
    {
        return std::ranges::any_of(List, [&](const RE::BGSKeyword* Keyword) { return Weapon.Base->HasKeyword(Keyword, Weapon.Instance); });
    }

    [[nodiscard]] bool UsesCharge(const Core::EquippedWeapon& Weapon)
    {
        const auto Ammo = Weapon.Instance->ammo ? Weapon.Instance->ammo : Weapon.Base->weaponData.ammo;

        return Ammo && Ammo->data.health != 0;
    }

    [[nodiscard]] Magazine MagazineOf(const Core::EquippedWeapon& Weapon)
    {
        if (!Keywords.Looked)
        {
            Keywords.Looked = true;
            std::string Missing;
            Keywords.Energy = FindKeywords(EnergyKeywords, Missing);
            Keywords.Heavy = FindKeywords(HeavyKeywords, Missing);
            Keywords.Pistol = FindKeywords(PistolKeywords, Missing);
            Keywords.Rifle = FindKeywords(RifleKeywords, Missing);

            if (!Missing.empty())
                REX::WARN("Ammo check display: no keyword {}; weapons with it get another magazine icon", Missing);
        }

        if (UsesCharge(Weapon) || HasAny(Weapon, Keywords.Energy))
            return Magazine::Energy;

        if (HasAny(Weapon, Keywords.Heavy))
            return Magazine::Heavy;

        return HasAny(Weapon, Keywords.Pistol) && !HasAny(Weapon, Keywords.Rifle) ? Magazine::Pistol : Magazine::Rifle;
    }

    [[nodiscard]] std::string_view NameOf(Magazine Kind)
    {
        switch (Kind)
        {
        case Magazine::Rifle:
            return "rifle"sv;
        case Magazine::Pistol:
            return "pistol"sv;
        case Magazine::Energy:
            return "energy"sv;
        case Magazine::Heavy:
            return "heavy"sv;
        }

        return "?"sv;
    }

    std::mutex FrameLock;
    Frame LatestFrame;

    std::optional<RE::BSFixedString> DefaultBone;

    const void* LoggedClip{ nullptr };

    using Clock = std::chrono::steady_clock;
    constexpr auto FollowAfterHide = std::chrono::milliseconds(500);
    Clock::time_point FollowUntil{};

    constexpr auto StaticDuration = std::chrono::milliseconds(2500);

    struct StaticCheck
    {
        const RE::TESObjectWEAP*               Base{ nullptr };
        const RE::TESObjectWEAP::InstanceData* Instance{ nullptr };
        Clock::time_point                      Until{};
    };

    std::mutex  StaticLock;
    StaticCheck Static;
    const int   StaticClip{ 0 };

    [[nodiscard]] StaticCheck CurrentStatic()
    {
        const std::scoped_lock Lock{ StaticLock };

        return Static;
    }

    void EndStatic()
    {
        const std::scoped_lock Lock{ StaticLock };
        Static.Until = {};
    }

    [[nodiscard]] Fill FillOf(int Rounds, int Capacity)
    {
        if (Rounds <= 0)
            return Fill::Empty;

        if (Capacity <= 0 || Rounds == Capacity)
            return Fill::Full;

        if (Rounds > Capacity)
            return Fill::FullChambered;

        const int TwiceFromHalf = 2 * Rounds - Capacity;

        if (TwiceFromHalf >= -1 && TwiceFromHalf <= 1)
            return Fill::Half;

        return TwiceFromHalf > 0 ? Fill::MoreThanHalf : Fill::LessThanHalf;
    }

    [[nodiscard]] std::string_view NameOf(Fill Level)
    {
        switch (Level)
        {
        case Fill::Empty:
            return "empty"sv;
        case Fill::LessThanHalf:
            return "less than half"sv;
        case Fill::Half:
            return "half"sv;
        case Fill::MoreThanHalf:
            return "more than half"sv;
        case Fill::Full:
            return "full"sv;
        case Fill::FullChambered:
            return "full +1"sv;
        }

        return "?"sv;
    }

    struct Toggles
    {
        std::array<std::pair<float, bool>, 16> Items{};
        std::size_t Count{ 0 };
        bool HasShow{ false };
    };

    [[nodiscard]] bool ShownAt(const void* Clip, float Time)
    {
        Toggles ClipToggles;
        Hooks::VisitClipAnnotations(
            Clip,
            [](void* Context, float At, const char* Text)
            {
                auto& List = *static_cast<Toggles*>(Context);
                const bool IsShow = _stricmp(Text, ShowAnnotation) == 0;

                if ((IsShow || _stricmp(Text, HideAnnotation) == 0) && List.Count < List.Items.size())
                {
                    List.Items[List.Count++] = { At, IsShow };
                    List.HasShow |= IsShow;
                }
            },
            &ClipToggles);

        std::sort(ClipToggles.Items.begin(), ClipToggles.Items.begin() + ClipToggles.Count);
        bool Shown = !ClipToggles.HasShow;

        for (std::size_t i = 0; i < ClipToggles.Count && ClipToggles.Items[i].first <= Time; ++i)
            Shown = ClipToggles.Items[i].second;

        return Shown;
    }

    [[nodiscard]] float HalfTan(float Degrees)
    {
        return std::tan(Degrees * 0.5f * std::numbers::pi_v<float> / 180.0f);
    }

    constexpr std::ptrdiff_t FirstPersonStateCamera = 0x50;

    [[nodiscard]] const RE::NiCamera* FirstPersonCamera()
    {
        const auto PlayerCamera = RE::PlayerCamera::GetSingleton();
        const auto FirstPersonState = PlayerCamera ? PlayerCamera->cameraStates[RE::CameraState::kFirstPerson].get() : nullptr;

        return FirstPersonState ? *reinterpret_cast<const RE::NiCamera* const*>(reinterpret_cast<std::uintptr_t>(FirstPersonState) + FirstPersonStateCamera) : nullptr;
    }

    constexpr int LogEvery = 20;
    int LogCountdown{ 0 };

    constexpr float MaxFrustumSpan = 20.0f;
    constexpr float MinFallbackFOV = 1.0f;
    constexpr float MaxFallbackFOV = 179.0f;
    constexpr float DefaultFallbackFOV = 80.0f;
    constexpr float MinForwardDistance = 0.01f;

    enum class Anchor
    {
        Bone,
        Counter,
        Center,
    };

    struct CounterPoint
    {
        float X{ 0.0f };
        float Y{ 0.0f };
        bool  Known{ false };
    };

    std::mutex   CounterLock;
    CounterPoint Counter;

    [[nodiscard]] CounterPoint CurrentCounter()
    {
        const std::scoped_lock Lock{ CounterLock };

        return Counter;
    }

    [[nodiscard]] bool OnScreen(float X, float Y)
    {
        return X >= 0.0f && X <= 1.0f && Y >= 0.0f && Y <= 1.0f;
    }

    void Project(RE::PlayerCharacter* Player, const WeaponConfig::ActionRule& Rule, Frame& Result, const void* Clip)
    {
        const auto& Bone = Rule.Bone.empty() ? *DefaultBone : Rule.Bone;
        const auto Root = Player->firstPerson3D.get();
        const auto Node = Root ? Root->GetObjectByName(Bone) : nullptr;
        const auto Camera = FirstPersonCamera();
        bool       Placed = false;
        float      Forward = 0.0f;
        float      Up = 0.0f;
        float      Right = 0.0f;

        if (Node && Camera)
        {
            const auto& From = Camera->world;
            const auto& Point = Node->world.translate;
            const RE::NiPoint3 Delta{ Point.x - From.translate.x, Point.y - From.translate.y, Point.z - From.translate.z };
            const auto Local = From.rotate * Delta;
            Right = Local.x;
            Forward = Local.y;
            Up = Local.z;

            const auto& Frustum = Camera->viewFrustum;
            const bool Usable = !Frustum.ortho && Frustum.right > Frustum.left && Frustum.top > Frustum.bottom && Frustum.right - Frustum.left < MaxFrustumSpan &&
                                Frustum.top - Frustum.bottom < MaxFrustumSpan;
            float Left = Frustum.left;
            float RightEdge = Frustum.right;
            float Top = Frustum.top;
            float Bottom = Frustum.bottom;

            if (!Usable)
            {
                const auto PlayerCamera = RE::PlayerCamera::GetSingleton();
                const float FOV = PlayerCamera && PlayerCamera->firstPersonFOV > MinFallbackFOV && PlayerCamera->firstPersonFOV < MaxFallbackFOV ? PlayerCamera->firstPersonFOV : DefaultFallbackFOV;
                RightEdge = HalfTan(FOV);
                Left = -RightEdge;
                Top = RightEdge * 9.0f / 16.0f;
                Bottom = -Top;
            }

            if (Forward > MinForwardDistance)
            {
                Result.X = (Right / Forward - Left) / (RightEdge - Left);
                Result.Y = (Top - Up / Forward) / (Top - Bottom);
                Placed = OnScreen(Result.X, Result.Y);
            }
        }

        auto Where = Anchor::Bone;

        if (!Placed)
        {
            const auto AtCounter = CurrentCounter();
            Where = AtCounter.Known ? Anchor::Counter : Anchor::Center;
            Result.AtCounter = AtCounter.Known;
            Result.X = AtCounter.Known ? AtCounter.X : 0.5f;
            Result.Y = AtCounter.Known ? AtCounter.Y : 0.5f;
        }

        if (Settings::VerboseLog() && (Clip != LoggedClip || --LogCountdown <= 0))
        {
            LogCountdown = LogEvery;
            const auto Why = !Node ? std::format("no '{}' in the first person model", Bone.c_str()) :
                             !Camera ? "no first person camera"s :
                                       std::format("bone '{}' in the view's space forward {:.1f}, up {:.1f}, right {:.1f}", Bone.c_str(), Forward, Up, Right);
            REX::INFO("Ammo check display: {}; shown {} at ({:.3f}, {:.3f}) from the top left", Why,
                Where == Anchor::Bone ? "on the bone"sv : Where == Anchor::Counter ? "at the ammo counter (the bone is off the screen)"sv : "at the screen center"sv,
                Result.X, Result.Y);
        }
    }

    void Publish(const Frame& NewFrame)
    {
        const std::scoped_lock Lock{ FrameLock };
        LatestFrame = NewFrame;
    }

    [[nodiscard]] Frame Published()
    {
        const std::scoped_lock Lock{ FrameLock };

        return LatestFrame;
    }

    class AmmoCheckMenu : public RE::IMenu
    {
    public:
        AmmoCheckMenu()
        {
            menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving);
            depthPriority = RE::UI_DEPTH_PRIORITY::kHUD;
            inputContext = RE::UserEvents::INPUT_CONTEXT_ID::kNone;
            inputEventHandlingEnabled = false;

            const auto ScaleformManager = RE::BSScaleformManager::GetSingleton();

            if (!ScaleformManager || !ScaleformManager->LoadMovie(*this, uiMovie, Movie, "root1") || !uiMovie || !menuObj.IsObject() || !menuObj.HasMember("Update"))
            {
                REX::WARN("Ammo check display: could not load Interface\\{}.swf", Movie);
                return;
            }

            REX::INFO("Ammo check display: {}.swf loaded", Movie);
        }

        ~AmmoCheckMenu() override
        {
            if (uiMovie)
            {
                uiMovie->SetCaptureThread(Scaleform::GetCurrentThreadId());

                if (const auto Heap = uiMovie->GetHeap())
                    Heap->AssignToCurrentThread();
            }

            menuObj = nullptr;
        }

        void AdvanceMovie(float TimeDelta, std::uint64_t Time) override
        {
            if (uiMovie && menuObj.IsObject())
                Show(Published());

            IMenu::AdvanceMovie(TimeDelta, Time);
        }

    private:
        void Show(const Frame& Current)
        {
            if (!Current.Visible && !Shown)
                return;

            ShownFrames = Current.Visible ? ShownFrames + 1 : 0;

            const auto Color = RE::HUDMenuUtils::GetGameplayHUDColor();
            menuObj.Invoke("SetColor", std::array<Value, 3>{ Value{ static_cast<double>(Color.r) }, Value{ static_cast<double>(Color.g) },
                                           Value{ static_cast<double>(Color.b) } });

            Value StillShown;
            menuObj.Invoke("Update", &StillShown,
                std::array<Value, 6>{ Value{ Current.Visible }, Value{ static_cast<double>(static_cast<int>(Current.Level)) },
                    Value{ static_cast<double>(Current.X) }, Value{ static_cast<double>(Current.Y) },
                    Value{ static_cast<double>(static_cast<int>(Current.Kind)) }, Value{ Current.AtCounter } }
                    .data(),
                6);
            Shown = Current.Visible || (StillShown.IsBoolean() && StillShown.GetBoolean());

            if (Settings::VerboseLog())
            {
                Value Error;

                if (menuObj.GetMember("lastError", &Error) && Error.IsString() && *Error.GetString())
                {
                    REX::WARN("Ammo check display: the movie reports '{}'", Error.GetString());
                    menuObj.SetMember("lastError", "");
                }

                Value Debug;

                if (ShownFrames == 30 && menuObj.Invoke("DebugInfo", &Debug) && Debug.IsString())
                    REX::INFO("Ammo check display: widget {}", Debug.GetString());
            }
        }

        bool Shown{ false };
        int ShownFrames{ 0 };
    };

    void OpenMenu()
    {
        if (const auto Queue = RE::UIMessageQueue::GetSingleton())
            Queue->AddMessage(MenuName, RE::UI_MESSAGE_TYPE::kShow);
    }
}

namespace AmmoCheckDisplay
{
    void Initialize()
    {
        DefaultBone.emplace(DefaultBoneName);

        if (const auto UI = RE::UI::GetSingleton())
            UI->RegisterMenu(MenuName.data(), [](const RE::UIMessage&) -> RE::IMenu* { return new AmmoCheckMenu(); });
    }

    void EnsureOpen()
    {
        const auto UI = RE::UI::GetSingleton();

        if (UI && !UI->GetMenuOpen(MenuName))
            OpenMenu();
    }

    void SetCounterPoint(float X, float Y)
    {
        if (!OnScreen(X, Y))
            return;

        const std::scoped_lock Lock{ CounterLock };
        Counter = { .X = X, .Y = Y, .Known = true };
    }

    std::optional<bool> ShowWithoutAnimation()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const auto Weapon = Player ? Core::GetEquippedWeapon(Player) : std::nullopt;

        if (!Weapon || !Weapon->Base || !Weapon->Instance || !Weapon->Data)
            return std::nullopt;

        if (!Settings::AmmoCheckEveryGun() || WeaponConfig::MatchAction(WeaponConfig::WeaponAction::AmmoCheck, Weapon->Base, Weapon->Instance))
            return std::nullopt;

        auto Refuse = [&](std::string_view Why)
        {
            if (Settings::VerboseLog())
                REX::INFO("Ammo check display: no animation for {:08X}, not shown: {}", Weapon->Base->GetFormID(), Why);

            return false;
        };

        if (WeaponConfig::Excluded(WeaponConfig::Feature::AmmoCheck, Weapon->Base, Weapon->Instance))
            return Refuse("excluded (\"ammoCheck\" in MWB\\Exclusions)"sv);

        if (Weapon->Base->weaponData.type.get() != RE::WEAPON_TYPE::kGun || Weapon->Instance->ammoCapacity == 0)
            return Refuse("not a gun with a magazine"sv);

        if (Player->Is3rdPersonVisible())
            return Refuse("third person"sv);

        if (const auto Busy = FirstPerson::WeaponBusy(Player))
            return Refuse(*Busy);

        {
            const std::scoped_lock Lock{ StaticLock };
            Static = { .Base = Weapon->Base, .Instance = Weapon->Instance, .Until = Clock::now() + StaticDuration };
        }

        if (Settings::VerboseLog())
            REX::INFO("Ammo check display: no animation for {:08X}, shown on '{}' for {} ms", Weapon->Base->GetFormID(), DefaultBoneName,
                StaticDuration.count());

        return true;
    }

    void Tick(RE::PlayerCharacter* Player)
    {
        if (!DefaultBone)
            return;

        Frame Current;
        const bool InFirstPerson = Player && !Player->Is3rdPersonVisible();
        const void* Clip = InFirstPerson ? Hooks::FindActiveClip(Clips) : nullptr;
        const auto Now = Clock::now();
        const bool Following = InFirstPerson && Now < FollowUntil;
        const auto Pressed = CurrentStatic();
        const bool StaticOn = InFirstPerson && Now < Pressed.Until;

        if (!Clip && !Following && !StaticOn)
        {
            LoggedClip = nullptr;
            Publish(Current);
            return;
        }

        std::optional<Core::EquippedWeapon> Weapon;

        if (!Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        const bool Usable = Weapon && Weapon->Data && Weapon->Instance;
        auto Rule = Usable ? WeaponConfig::MatchAction(WeaponConfig::WeaponAction::AmmoCheck, Weapon->Base, Weapon->Instance) : std::nullopt;
        const bool SameWeapon = Usable && Pressed.Base == Weapon->Base && Pressed.Instance == Weapon->Instance;
        bool StaticShown = false;

        if (!Rule && SameWeapon && (StaticOn || Following))
        {
            Rule = WeaponConfig::ActionRule{};

            if (StaticOn)
            {
                if (const auto Busy = FirstPerson::WeaponBusy(Player))
                {
                    if (Settings::VerboseLog())
                        REX::INFO("Ammo check display: hidden, {}", *Busy);

                    EndStatic();
                }
                else
                    StaticShown = true;
            }
        }
        else if (StaticOn && !SameWeapon)
            EndStatic();

        if (!Rule)
        {
            FollowUntil = {};
            Publish(Current);
            return;
        }

        if (StaticShown)
            Clip = &StaticClip;

        Current.Visible = StaticShown || (Clip && Clip != &StaticClip && Hooks::ClipDuration(Clip) > 0.0f && ShownAt(Clip, Hooks::ClipLocalTime(Clip)));
        Project(Player, *Rule, Current, Clip);

        if (Current.Visible)
        {
            FollowUntil = Now + FollowAfterHide;
            const int Rounds = static_cast<int>(Weapon->Data->ammoCount);
            const int Capacity = Weapon->Instance->ammoCapacity;
            Current.Level = FillOf(Rounds, Capacity);
            Current.Kind = MagazineOf(*Weapon);

            if (Settings::VerboseLog() && LoggedClip != Clip)
                REX::INFO("Ammo check display: {} of {} loaded -> {}, {} magazine", Rounds, Capacity, NameOf(Current.Level),
                    NameOf(Current.Kind));

            LoggedClip = Clip;
            EnsureOpen();
        }

        Publish(Current);
    }
}
