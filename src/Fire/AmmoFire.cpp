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

#include "AmmoFire.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

#include <unordered_map>

namespace
{
    constexpr std::string_view AnnotationTag = "MWB_WeaponFireAmmo";

    using FireSignature = void (*)(RE::BGSObjectInstance&, RE::TESObjectREFR*, std::uint32_t, RE::TESAmmo*, RE::AlchemyItem*);

    constexpr REL::ID        PlayFireSoundID{ 831387, 2232492 };
    constexpr REL::ID        SetOriginAndDirectionID{ 1314608, 2236960 };
    constexpr std::size_t    FireScanSize = 0x1000;
    constexpr std::int32_t   UseAmmoOffset = 0x780;
    constexpr std::size_t    SetFormEditorIDIndex = 0x3B;
    constexpr RE::TESFormID  DefaultUnarmedWeapon = 0x1F4;
    constexpr std::uint32_t  NoEquipIndex = 0xFFFFFFFF;

    constexpr std::uint8_t   CallRel32Opcode = 0xE8;
    constexpr std::size_t    CallRel32DisplacementOffset = 1;
    constexpr std::size_t    CallRel32Size = 5;

    constexpr std::uint8_t   IndirectCallOpcode = 0xFF;
    constexpr std::uint8_t   ModRMRegisterMask = 0xF8;
    constexpr std::uint8_t   ModRMCallRegisterDisp32 = 0x90;
    constexpr std::uint8_t   ModRMCallRspNeedsSIB = 0x94;
    constexpr std::size_t    CallDisp32DisplacementOffset = 2;
    constexpr std::size_t    CallDisp32Size = 6;

    thread_local RE::TESAmmo* FiringAmmo = nullptr;
    bool                      Installed = false;

    std::mutex                                    Lock;
    std::unordered_map<std::string, RE::TESAmmo*> AmmoByEditorID;
    std::unordered_set<std::string>               ReportedMissing;

    [[nodiscard]] std::string Lowercase(std::string_view Text)
    {
        std::string Result{ Text };

        std::ranges::transform(Result, Result.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

        return Result;
    }

    [[nodiscard]] bool IEquals(std::string_view Left, std::string_view Right)
    {
        return std::ranges::equal(Left, Right, [](unsigned char LeftChar, unsigned char RightChar) { return std::tolower(LeftChar) == std::tolower(RightChar); });
    }

    struct SetFormEditorID
    {
        static bool Thunk(RE::TESAmmo* Ammo, const char* EditorID)
        {
            if (Ammo && EditorID && *EditorID)
            {
                const std::scoped_lock Guard{ Lock };
                AmmoByEditorID[Lowercase(EditorID)] = Ammo;
            }

            return Original(Ammo, EditorID);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct PlayFireSound
    {
        static void Thunk(void* Process, RE::Actor* Actor, std::uint32_t EquipIndex)
        {
            if (!FiringAmmo)
                Original(Process, Actor, EquipIndex);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct SetOriginAndDirection
    {
        static bool Thunk(RE::ProjectileLaunchData& Data)
        {
            if (FiringAmmo && FiringAmmo->data.projectile)
                Data.projectileBase = FiringAmmo->data.projectile;

            return Original(Data);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    struct UseAmmo
    {
        using UseAmmoSignature = std::uint32_t (*)(RE::Actor*, const RE::BGSObjectInstance&, std::uint32_t, std::uint32_t);

        static std::uint32_t Thunk(RE::Actor* Actor, const RE::BGSObjectInstance& Weapon, std::uint32_t EquipIndex, std::uint32_t Flag)
        {
            if (FiringAmmo)
                return 0;

            const auto VirtualTable = *reinterpret_cast<const UseAmmoSignature* const*>(Actor);

            return VirtualTable[UseAmmoOffset / sizeof(void*)](Actor, Weapon, EquipIndex, Flag);
        }
    };

    [[nodiscard]] std::string_view VtableClassName(std::uintptr_t Vtable)
    {
        struct CompleteObjectLocator
        {
            std::uint32_t Signature;
            std::uint32_t Offset;
            std::uint32_t CDOffset;
            std::uint32_t TypeDescriptorRVA;
        };

        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator)
            return {};

        const auto ModuleBase = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(ModuleBase + Locator->TypeDescriptorRVA + 2 * sizeof(void*));
    }

    [[nodiscard]] std::optional<std::uintptr_t> FindDirectCall(std::uintptr_t Begin, std::uintptr_t Target)
    {
        for (std::size_t i = 0; i + CallRel32Size <= FireScanSize; ++i)
        {
            const auto Address = Begin + i;

            if (*reinterpret_cast<const std::uint8_t*>(Address) != CallRel32Opcode)
                continue;

            const auto Displacement = *reinterpret_cast<const std::int32_t*>(Address + CallRel32DisplacementOffset);

            if (static_cast<std::uintptr_t>(static_cast<std::intptr_t>(Address + CallRel32Size) + Displacement) == Target)
                return Address;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::uintptr_t> FindVirtualCall(std::uintptr_t Begin, std::int32_t Offset)
    {
        for (std::size_t i = 0; i + CallDisp32Size <= FireScanSize; ++i)
        {
            const auto Address = Begin + i;
            const auto Bytes = reinterpret_cast<const std::uint8_t*>(Address);

            if (Bytes[0] == IndirectCallOpcode && (Bytes[1] & ModRMRegisterMask) == ModRMCallRegisterDisp32 && Bytes[1] != ModRMCallRspNeedsSIB &&
                *reinterpret_cast<const std::int32_t*>(Address + CallDisp32DisplacementOffset) == Offset)
            {
                return Address;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] RE::TESAmmo* FindAmmo(std::string_view EditorID)
    {
        const auto             Key = Lowercase(EditorID);
        const std::scoped_lock Guard{ Lock };

        if (const auto Found = AmmoByEditorID.find(Key); Found != AmmoByEditorID.end())
            return Found->second;

        if (ReportedMissing.insert(Key).second)
            REX::WARN("MWB_WeaponFireAmmo: no AMMO with editor ID '{}'", EditorID);

        return nullptr;
    }

    void Fire(RE::Actor* Actor, RE::TESAmmo* Ammo)
    {
        static REL::Relocation<FireSignature> GameFire{ RE::ID::TESObjectWEAP::Fire };

        RE::TESObjectWEAP*    Base = nullptr;
        RE::TBO_InstanceData* Data = nullptr;
        std::uint32_t         EquipIndex = NoEquipIndex;

        if (const auto Weapon = Core::GetEquippedWeapon(Actor); Weapon && Weapon->Item)
        {
            Base = Weapon->Base;
            Data = Weapon->Item->item.instanceData.get();
            EquipIndex = Weapon->Item->equipIndex.index;
        }
        else
        {
            Base = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(DefaultUnarmedWeapon);

            if (!Base)
            {
                REX::WARN("MWB_WeaponFireAmmo: no weapon drawn and no DefaultUnarmedWeapon");
                return;
            }
        }

        RE::BGSObjectInstance Instance{ Base, Data };

        FiringAmmo = Ammo;
        GameFire(Instance, Actor, EquipIndex, Ammo, nullptr);
        FiringAmmo = nullptr;

        if (Settings::VerboseLog())
        {
            REX::INFO("MWB_WeaponFireAmmo: {:08X} fired {:08X} from {:08X}{}", Actor->GetFormID(), Ammo->GetFormID(), Base->GetFormID(),
                EquipIndex == NoEquipIndex ? " (no weapon drawn)" : "");
        }
    }
}

namespace AmmoFire
{
    void InstallEditorIDHook()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::TESAmmo[0] };

        const auto ClassName = VtableClassName(Vtable.address());

        if (ClassName != ".?AVTESAmmo@@"sv)
        {
            REX::ERROR("TESAmmo vtable id points at '{}'. MWB_WeaponFireAmmo disabled.", ClassName);
            return;
        }

        SetFormEditorID::Original = Vtable.write_vfunc(SetFormEditorIDIndex, SetFormEditorID::Thunk);
    }

    void Install()
    {
        if (!SetFormEditorID::Original.address())
            return;

        const auto FireAddress = REL::Relocation<std::uintptr_t>{ RE::ID::TESObjectWEAP::Fire }.address();
        const auto SoundCall = FindDirectCall(FireAddress, PlayFireSoundID.address());
        const auto UseAmmoCall = FindVirtualCall(FireAddress, UseAmmoOffset);
        const auto LaunchCall = FindDirectCall(FireAddress, SetOriginAndDirectionID.address());

        if (!SoundCall || !UseAmmoCall || !LaunchCall)
        {
            REX::ERROR("MWB_WeaponFireAmmo: TESObjectWEAP::Fire at {:X} does not look as expected (sound call {}, ammo call {}, "
                       "launch call {}). Disabled.",
                FireAddress, SoundCall.has_value(), UseAmmoCall.has_value(), LaunchCall.has_value());
            return;
        }

        auto& Trampoline = REL::GetTrampoline();

        PlayFireSound::Original = Trampoline.write_call<CallRel32Size>(*SoundCall, PlayFireSound::Thunk);
        SetOriginAndDirection::Original = Trampoline.write_call<CallRel32Size>(*LaunchCall, SetOriginAndDirection::Thunk);
        static_cast<void>(Trampoline.write_call<CallDisp32Size>(*UseAmmoCall, UseAmmo::Thunk));
        Installed = true;

        const std::scoped_lock Guard{ Lock };
        REX::INFO("MWB_WeaponFireAmmo installed ({} ammo editor IDs)", AmmoByEditorID.size());
    }

    void OnAnimationEvent(RE::Actor* Actor, const RE::BSAnimationGraphEvent& Event)
    {
        if (!Installed || !Actor)
            return;

        const std::string_view Tag = Event.tag;
        std::string_view       EditorID;

        if (IEquals(Tag, AnnotationTag))
            EditorID = Event.payload;
        else if (Tag.size() > AnnotationTag.size() && Tag[AnnotationTag.size()] == '.' && IEquals(Tag.substr(0, AnnotationTag.size()), AnnotationTag))
            EditorID = Tag.substr(AnnotationTag.size() + 1);
        else
            return;

        if (Settings::VerboseLog())
            REX::INFO("MWB_WeaponFireAmmo: event from {:08X} (tag '{}', payload '{}')", Actor->GetFormID(), Tag, std::string_view{ Event.payload });

        if (EditorID.empty())
        {
            REX::WARN("MWB_WeaponFireAmmo without an editor ID");
            return;
        }

        if (const auto Ammo = FindAmmo(EditorID))
            Fire(Actor, Ammo);
    }
}
