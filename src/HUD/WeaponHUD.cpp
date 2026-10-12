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

#include "WeaponHUD.h"
#include "HUDValues.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    using Value = Scaleform::GFx::Value;
    using HUDValues::Abandon;
    using HUDValues::AsValue;
    using HUDValues::ClipOf;
    using HUDValues::MovieOf;
    using HUDValues::SetFadeTimer;

    constexpr std::size_t UpdateDisplayObjectIndex = 4;

    constexpr std::size_t ShaderEnabledStates = 0x8C;
    constexpr std::size_t BackgroundColorType = 0xAC;
    constexpr std::size_t ShaderChildren = 0x90;
    constexpr std::size_t ShaderChildCount = 0xA0;
    constexpr std::size_t ExplosiveCount = 0x100;
    constexpr std::int32_t MaxExplosiveCount = 99;
    constexpr std::size_t ReceivedAmmoCounts = 0x1C0;
    constexpr std::size_t ReceivedAmmoCountsSet = 0x1C8;
    constexpr std::uint32_t MaxShownAmmoCount = 999;
    constexpr std::uint32_t NoAmmoCount = 0xFFFFFFFF;
    constexpr std::uint32_t BackgroundEnabledBit = 1u;
    constexpr int MaxShaderChildDepth = 3;
    constexpr std::uint32_t MaxShaderChildren = 64;

    constexpr auto Movie = "MWB_WeaponHUD.swf";
    constexpr auto ExplosiveRow = "MWB_ExplosiveRow";

    constexpr int GrenadeIconFrame = 1;
    constexpr int MineIconFrame = 2;
    constexpr int MaxSortingTagGroups = 8;

    struct WeaponText
    {
        std::string Name;
        std::string Ammo;
        std::string Mode;

        bool operator==(const WeaponText&) const = default;
    };

    struct Counts
    {
        int Clip{ -1 };
        int Reserve{ -1 };
        bool Chambered{ false };

        bool operator==(const Counts&) const = default;
    };

    struct Grenades
    {
        int Count{ -1 };
        int Type{ -1 };

        bool operator==(const Grenades&) const = default;
    };

    struct State
    {
        const void* AmmoClip{ nullptr };
        const void* Movie{ nullptr };
        Value Loader;
        Value Widget;
        bool Failed{ false };
        bool Active{ false };

        WeaponText Weapon;
        Counts ShownCounts;
        Grenades ShownGrenades;

        std::string LastExplosiveDebug;
        std::string LastWidgetDebug;

        const RE::TESObjectWEAP* NameBase{ nullptr };
        std::string Name;

        const RE::TESAmmo* AmmoNameBase{ nullptr };
        std::string AmmoName;
    };

    State& HUD = *new State;

    std::atomic<std::int64_t> PackedGrenades{ -1 };

    struct ExplosiveState
    {
        const void* Component{ nullptr };
        std::uint32_t EnabledStates{ 0 };
        std::uint32_t BackgroundType{ 0 };
        bool Hidden{ false };
    };

    ExplosiveState Explosive;

    std::atomic<bool> ThrownIsMine{ false };

    [[nodiscard]] std::optional<Grenades> SeenGrenades()
    {
        const auto Packed = PackedGrenades.load(std::memory_order_relaxed);

        if (Packed < 0)
            return std::nullopt;

        return Grenades{ .Count = static_cast<int>(Packed & 0xFFFFFFFF), .Type = static_cast<int>(Packed >> 32) };
    }

    template <class T>
    [[nodiscard]] T& Field(void* Component, std::size_t Offset)
    {
        return *reinterpret_cast<T*>(static_cast<std::byte*>(Component) + Offset);
    }

    [[nodiscard]] bool GetChild(const Value& Parent, const char* Child, Value& Out)
    {
        return Parent.IsObject() && Parent.GetMember(Child, &Out) && Out.IsDisplayObject();
    }

    void Hide(const Value& Parent, const char* Child)
    {
        Value ChildClip;
        Value Visible;

        if (GetChild(Parent, Child, ChildClip) && (!ChildClip.GetMember("visible", &Visible) || !Visible.IsBoolean() || Visible.GetBoolean()))
            ChildClip.SetMember("visible", false);
    }

    void Show(const Value& Parent, const char* Child)
    {
        Value ChildClip;

        if (GetChild(Parent, Child, ChildClip))
            ChildClip.SetMember("visible", true);
    }

    [[nodiscard]] bool FindChild(Value& Parent, const char* Name, Value& Out)
    {
        Value NameArg{ Name };

        return Parent.IsDisplayObject() && Parent.Invoke("getChildByName", &Out, &NameArg, 1) && Out.IsDisplayObject();
    }

    void HideVanillaAmmo(const Value& Counter)
    {
        Hide(Counter, "ClipCount_tf");
        Hide(Counter, "ReserveCount_tf");
        Hide(Counter, "AmmoLineInstance");
    }

    [[nodiscard]] std::optional<int> ReadNumber(const Value& Parent, const char* Child, const char* Member)
    {
        Value ChildClip;
        Value MemberValue;

        if (!GetChild(Parent, Child, ChildClip) || !ChildClip.GetMember(Member, &MemberValue))
            return std::nullopt;

        if (MemberValue.IsNumber())
            return static_cast<int>(MemberValue.GetNumber());

        if (!MemberValue.IsString())
            return std::nullopt;

        int Number = 0;
        const std::string_view Text{ MemberValue.GetString() };
        const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Number);

        return Error == std::errc{} && End == Text.data() + Text.size() ? std::optional{ Number } : std::nullopt;
    }

    struct ReceivedCounts
    {
        std::uint32_t Clip{ 0 };
        std::uint32_t Reserve{ 0 };
    };

    [[nodiscard]] std::optional<ReceivedCounts> ReadReceivedCounts(void* Component)
    {
        if (!Field<bool>(Component, ReceivedAmmoCountsSet))
            return std::nullopt;

        const std::uint64_t Packed = std::atomic_ref{ Field<std::uint64_t>(Component, ReceivedAmmoCounts) }.load(std::memory_order_relaxed);

        return ReceivedCounts{ .Clip = static_cast<std::uint32_t>(Packed & 0xFFFFFFFF), .Reserve = static_cast<std::uint32_t>(Packed >> 32) };
    }

    [[nodiscard]] int Uncapped(int Shown, std::optional<std::uint32_t> Received)
    {
        if (!Received || *Received == NoAmmoCount || *Received <= MaxShownAmmoCount || Shown != static_cast<int>(MaxShownAmmoCount))
            return Shown;

        return static_cast<int>(std::min<std::uint32_t>(*Received, std::numeric_limits<int>::max()));
    }

    void LogWidgetError()
    {
        Value Error;

        if (!HUD.Widget.IsObject() || !HUD.Widget.GetMember("lastError", &Error) || !Error.IsString() || !*Error.GetString())
            return;

        REX::WARN("Weapon HUD: the movie reports '{}'", Error.GetString());
        HUD.Widget.SetMember("lastError", "");
    }

    void Inject(Value& Counter)
    {
        Abandon(HUD.Loader);
        Abandon(HUD.Widget);
        HUD.AmmoClip = ClipOf(Counter);
        HUD.Movie = MovieOf(Counter);
        HUD.Failed = false;
        HUD.Active = true;
        HUD.Weapon = {};
        HUD.ShownCounts = {};
        HUD.ShownGrenades = {};

        const auto CounterMovie = MovieOf(Counter);

        if (!CounterMovie)
        {
            REX::WARN("Weapon HUD: the ammo counter has no movie; the vanilla counter stays");
            HUD.Failed = true;
            return;
        }

        Value MovieURL{ Movie };
        Value Request;
        CounterMovie->CreateObject(&Request, "flash.net.URLRequest", &MovieURL, 1);
        CounterMovie->CreateObject(&HUD.Loader, "flash.display.Loader");

        if (!Request.IsObject() || !HUD.Loader.IsDisplayObject() || !HUD.Loader.Invoke("load", nullptr, &Request, 1) ||
            !Counter.Invoke("addChild", nullptr, &HUD.Loader, 1))
        {
            REX::WARN("Weapon HUD: could not load {} into the ammo counter; the vanilla counter stays", Movie);
            HUD.Failed = true;
            return;
        }

        REX::INFO("Weapon HUD: loading {} into the ammo counter", Movie);
    }

    [[nodiscard]] bool WidgetReady(Value& Counter)
    {
        if (HUD.Widget.IsDisplayObject())
            return true;

        Value Content;

        if (!HUD.Loader.IsObject() || !HUD.Loader.GetMember("content", &Content) || !Content.IsDisplayObject() ||
            !Content.HasMember("SetWeapon"))
            return false;

        HUD.Widget = Content;

        HideVanillaAmmo(Counter);
        REX::INFO("Weapon HUD: {} loaded; the vanilla ammo counter is hidden", Movie);
        LogWidgetError();

        return true;
    }

    void Deactivate(Value& Counter)
    {
        HUD.Active = false;

        if (HUD.Loader.IsDisplayObject())
            HUD.Loader.SetMember("visible", false);

        if (HUD.Widget.IsDisplayObject())
        {
            Value Enabled{ false };
            HUD.Widget.Invoke("SetEnabled", nullptr, &Enabled, 1);
        }

        for (const auto Child : { "ClipCount_tf", "ReserveCount_tf", "AmmoLineInstance" })
            Show(Counter, Child);

        REX::INFO("Weapon HUD: turned off; the vanilla ammo counter is back");
    }

    void Reactivate()
    {
        HUD.Active = true;
        HUD.Loader.SetMember("visible", true);
        Value Enabled{ true };
        HUD.Widget.Invoke("SetEnabled", nullptr, &Enabled, 1);
        REX::INFO("Weapon HUD: turned on; the vanilla ammo counter is hidden");
    }

    [[nodiscard]] bool IsTagSeparator(char Character)
    {
        return Character == ' ' || Character == '-' || Character == '_' || Character == ':' || Character == '|';
    }

    [[nodiscard]] char ClosingOf(char Open)
    {
        switch (Open)
        {
        case '[':
            return ']';
        case '(':
            return ')';
        case '{':
            return '}';
        case '<':
            return '>';
        case '|':
            return '|';
        default:
            return '\0';
        }
    }

    [[nodiscard]] std::string StripSortingTags(std::string_view Name)
    {
        auto Trim = [](std::string_view Text)
        {
            while (!Text.empty() && IsTagSeparator(Text.front()))
                Text.remove_prefix(1);

            while (!Text.empty() && (Text.back() == ' ' || Text.back() == '-' || Text.back() == '|'))
                Text.remove_suffix(1);

            return Text;
        };

        auto Remaining = Trim(Name);

        for (int i = 0; i < MaxSortingTagGroups && !Remaining.empty(); ++i)
        {
            const auto Close = ClosingOf(Remaining.front());

            if (!Close)
                break;

            auto End = Remaining.find(Close, 1);

            if (End == std::string_view::npos)
                break;

            while (End + 1 < Remaining.size() && Remaining[End + 1] == Close)
                ++End;

            const auto Rest = Trim(Remaining.substr(End + 1));

            if (Rest.empty())
                break;

            Remaining = Rest;
        }

        for (int i = 0; i < MaxSortingTagGroups && Remaining.size() > 1 && (Remaining.back() == ']' || Remaining.back() == '}'); ++i)
        {
            const auto Open = Remaining.back() == ']' ? '[' : '{';
            auto Start = Remaining.rfind(Open);

            if (Start == std::string_view::npos)
                break;

            while (Start > 0 && Remaining[Start - 1] == Open)
                --Start;

            const auto Rest = Trim(Remaining.substr(0, Start));

            if (Rest.empty())
                break;

            Remaining = Rest;
        }

        return std::string{ Remaining.empty() ? Name : Remaining };
    }

    [[nodiscard]] std::string WeaponName(const Core::EquippedWeapon& Weapon)
    {
        if (Weapon.Base != HUD.NameBase)
        {
            HUD.NameBase = Weapon.Base;
            HUD.Name = StripSortingTags(RE::TESFullName::GetFullName(*Weapon.Base));
        }

        return HUD.Name;
    }

    [[nodiscard]] std::string AmmoName(const RE::TESAmmo& Ammo)
    {
        if (&Ammo != HUD.AmmoNameBase)
        {
            HUD.AmmoNameBase = &Ammo;
            HUD.AmmoName = StripSortingTags(RE::TESFullName::GetFullName(Ammo));
        }

        return HUD.AmmoName;
    }

    [[nodiscard]] WeaponText ReadWeapon(const Core::EquippedWeapon& Weapon)
    {
        WeaponText Text{ .Name = WeaponName(Weapon) };

        const RE::TESAmmo* Ammo = Weapon.Data && Weapon.Data->ammo ? Weapon.Data->ammo : nullptr;

        if (!Ammo)
            Ammo = Weapon.Instance->ammo ? Weapon.Instance->ammo : Weapon.Base->weaponData.ammo;

        if (Ammo)
            Text.Ammo = AmmoName(*Ammo);

        if (Weapon.Base->weaponData.type.get() == RE::WEAPON_TYPE::kGun)
            Text.Mode = Weapon.Instance->flags.any(RE::WEAPON_FLAGS::kAutomatic) ? "AUTO" : "SEMI";

        return Text;
    }

    void OnAmmoCounterUpdated(void* Component)
    {
        auto& Counter = AsValue(Component);

        if (!Counter.IsDisplayObject())
            return;

        const bool SameClip = ClipOf(Counter) == HUD.AmmoClip && MovieOf(Counter) == HUD.Movie;

        if (!Settings::WeaponHUD())
        {
            if (SameClip && HUD.Active)
                Deactivate(Counter);

            return;
        }

        if (!SameClip)
            Inject(Counter);

        if (HUD.Failed || !WidgetReady(Counter))
            return;

        if (!HUD.Active)
            Reactivate();

        HideVanillaAmmo(Counter);

        const auto Player = RE::PlayerCharacter::GetSingleton();
        std::optional<Core::EquippedWeapon> Weapon;

        if (!Player || !Core::TryGetEquippedWeapon(Player, Weapon))
            return;

        if (Weapon && Weapon->Base && Weapon->Instance)
        {
            if (const auto Text = ReadWeapon(*Weapon); Text != HUD.Weapon)
            {
                HUD.Weapon = Text;
                const std::array<Value, 3> Args{ Value{ Text.Name.c_str() }, Value{ Text.Ammo.c_str() }, Value{ Text.Mode.c_str() } };
                HUD.Widget.Invoke("SetWeapon", Args);
                SetFadeTimer(Component);
            }
        }

        auto Clip = ReadNumber(Counter, "ClipCount_tf", "text");
        auto Reserve = ReadNumber(Counter, "ReserveCount_tf", "text");

        if (Clip && Reserve)
        {
            const auto Received = ReadReceivedCounts(Component);
            Clip = Uncapped(*Clip, Received ? std::optional{ Received->Clip } : std::nullopt);
            Reserve = Uncapped(*Reserve, Received ? std::optional{ Received->Reserve } : std::nullopt);

            const int Capacity = Weapon && Weapon->Instance ? Weapon->Instance->ammoCapacity : 0;
            const bool Chambered = Capacity > 0 && *Clip > Capacity;
            const Counts NewCounts{ .Clip = Chambered ? *Clip - 1 : *Clip, .Reserve = *Reserve, .Chambered = Chambered };

            if (NewCounts != HUD.ShownCounts)
            {
                HUD.ShownCounts = NewCounts;
                const std::array<Value, 3> Args{ Value{ static_cast<double>(NewCounts.Clip) }, Value{ static_cast<double>(NewCounts.Reserve) },
                    Value{ NewCounts.Chambered } };
                HUD.Widget.Invoke("SetCounts", Args);
            }
        }

        if (const auto Seen = SeenGrenades(); Seen && *Seen != HUD.ShownGrenades)
        {
            HUD.ShownGrenades = *Seen;
            const std::array<Value, 2> Args{ Value{ static_cast<double>(HUD.ShownGrenades.Count) },
                Value{ static_cast<double>(HUD.ShownGrenades.Type) } };
            HUD.Widget.Invoke("SetGrenades", Args);
        }

        HUD.Widget.Invoke("Tick");

        if (Settings::VerboseLog())
        {
            LogWidgetError();
            Value Debug;

            if (HUD.Widget.Invoke("DebugInfo", &Debug) && Debug.IsString() && HUD.LastWidgetDebug != Debug.GetString())
            {
                HUD.LastWidgetDebug = Debug.GetString();
                REX::INFO("Weapon HUD: widget {}", HUD.LastWidgetDebug);
            }
        }
    }

    void DisableBackground(void* Target, int Depth)
    {
        Field<std::uint32_t>(Target, ShaderEnabledStates) &= ~BackgroundEnabledBit;
        Field<std::uint32_t>(Target, BackgroundColorType) = 0;

        if (Depth >= MaxShaderChildDepth)
            return;

        const auto Children = Field<void**>(Target, ShaderChildren);
        const auto Count = Field<std::uint32_t>(Target, ShaderChildCount);

        for (std::uint32_t i = 0; Children && i < Count && i < MaxShaderChildren; ++i)
        {
            if (Children[i])
                DisableBackground(Children[i], Depth + 1);
        }
    }

    [[nodiscard]] std::optional<int> ReadExplosiveCount(void* Component)
    {
        const auto Count = Field<std::int32_t>(Component, ExplosiveCount);

        if (Count < 0)
            return std::nullopt;

        return std::min(Count, MaxExplosiveCount);
    }

    void OnExplosiveCounterUpdated(void* Component)
    {
        auto& ExplosiveCounter = AsValue(Component);

        if (!ExplosiveCounter.IsDisplayObject())
            return;

        const auto Count = ReadExplosiveCount(Component);

        if (bool Mine = false; Core::TryGetThrownIsMine(RE::PlayerCharacter::GetSingleton(), Mine))
            ThrownIsMine.store(Mine, std::memory_order_relaxed);

        const int Type = ThrownIsMine.load(std::memory_order_relaxed) ? MineIconFrame : GrenadeIconFrame;

        if (Count && *Count >= 0)
        {
            const auto Icon = Type;
            PackedGrenades.store((static_cast<std::int64_t>(Icon) << 32) | static_cast<std::uint32_t>(*Count), std::memory_order_relaxed);
        }

        if (Explosive.Component != Component)
        {
            Explosive = { .Component = Component,
                .EnabledStates = Field<std::uint32_t>(Component, ShaderEnabledStates),
                .BackgroundType = Field<std::uint32_t>(Component, BackgroundColorType) };
        }

        Value Row;
        const bool Ours = Settings::WeaponHUD() && FindChild(ExplosiveCounter, ExplosiveRow, Row);

        if (Ours)
        {
            Hide(ExplosiveCounter, "AvailableCount_tf");
            Hide(ExplosiveCounter, "TypeIcon_mc");
            DisableBackground(Component, 0);
            Explosive.Hidden = true;
        }
        else if (Explosive.Hidden)
        {
            Show(ExplosiveCounter, "AvailableCount_tf");
            Show(ExplosiveCounter, "TypeIcon_mc");
            Field<std::uint32_t>(Component, ShaderEnabledStates) = Explosive.EnabledStates;
            Field<std::uint32_t>(Component, BackgroundColorType) = Explosive.BackgroundType;
            Explosive.Hidden = false;
        }

        if (Settings::VerboseLog())
        {
            auto Debug = std::format("count {}, type {}, our row {}, shader states {:X}, background type {}, {} child target(s)", Count.value_or(-1),
                Type, Ours, Field<std::uint32_t>(Component, ShaderEnabledStates), Field<std::uint32_t>(Component, BackgroundColorType),
                Field<std::uint32_t>(Component, ShaderChildCount));

            if (Debug != HUD.LastExplosiveDebug)
            {
                REX::INFO("Weapon HUD: explosive counter {}", Debug);
                HUD.LastExplosiveDebug = std::move(Debug);
            }
        }
    }

    struct AmmoCounterUpdate
    {
        static void Thunk(void* This)
        {
            Original(This);

            if (This)
                OnAmmoCounterUpdated(This);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct ExplosiveCounterUpdate
    {
        static void Thunk(void* This)
        {
            Original(This);

            if (This)
                OnExplosiveCounterUpdated(This);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] bool VtableIs(std::uintptr_t Vtable, std::string_view ClassName)
    {
        struct CompleteObjectLocator
        {
            std::uint32_t Signature;
            std::uint32_t Offset;
            std::uint32_t CdOffset;
            std::uint32_t TypeDescriptor;
        };

        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator || Locator->Offset != 0)
            return false;

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*)) == ClassName;
    }
}

namespace WeaponHUD
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> AmmoVtable{ RE::VTABLE::HUDAmmoCounter[0] };
        REL::Relocation<std::uintptr_t> ExplosiveVtable{ RE::VTABLE::HUDExplosiveAmmoCounter[0] };

        if (!VtableIs(AmmoVtable.address(), ".?AVHUDAmmoCounter@@"sv) || !VtableIs(ExplosiveVtable.address(), ".?AVHUDExplosiveAmmoCounter@@"sv))
        {
            REX::ERROR("Weapon HUD: the HUD counter vtables are not where expected; the vanilla counters stay");
            return;
        }

        AmmoCounterUpdate::Original = AmmoVtable.write_vfunc(UpdateDisplayObjectIndex, AmmoCounterUpdate::Thunk);
        ExplosiveCounterUpdate::Original = ExplosiveVtable.write_vfunc(UpdateDisplayObjectIndex, ExplosiveCounterUpdate::Thunk);
        REX::INFO("Weapon HUD installed");
    }
}
