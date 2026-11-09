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

#include "NameInjection.h"
#include "BehaviorNames.h"
#include "Settings/Settings.h"

namespace
{
    constexpr std::size_t    LoaderVtableIndex = 1;
    constexpr std::size_t    LoaderVtableOffset = 0x20;
    constexpr std::size_t    LoadSlot = 2;
    constexpr std::ptrdiff_t ContainerOffset = 0x8;
    constexpr std::ptrdiff_t NodeNameOffset = 0x38;
    constexpr std::ptrdiff_t GraphDataOffset = 0xC8;
    constexpr std::ptrdiff_t VariableInfosOffset = 0x20;
    constexpr std::ptrdiff_t EventInfosOffset = 0x40;
    constexpr std::ptrdiff_t InitialValuesOffset = 0x60;
    constexpr std::ptrdiff_t StringDataOffset = 0x68;
    constexpr std::ptrdiff_t EventNamesOffset = 0x10;
    constexpr std::ptrdiff_t VariableNamesOffset = 0x30;
    constexpr std::ptrdiff_t WordValuesOffset = 0x10;
    constexpr std::uint32_t  DontDeallocate = 0x80000000;
    constexpr std::uintptr_t HavokOwnedBit = 1;

    struct HavokArray
    {
        void*        Data;
        std::int32_t Size;
        std::int32_t CapacityAndFlags;
    };
    static_assert(sizeof(HavokArray) == 0x10);

    struct NamedVariant
    {
        std::uintptr_t Name;
        std::uintptr_t ClassName;
        void*          Variant;
    };
    static_assert(sizeof(NamedVariant) == 0x18);

    struct VariableInfo
    {
        std::int16_t                Role;
        std::int16_t                Flags;
        BehaviorNames::VariableType Type;
        std::int8_t                 Padding;
    };
    static_assert(sizeof(VariableInfo) == 6);

    struct CompleteObjectLocator
    {
        std::uint32_t Signature;
        std::uint32_t Offset;
        std::uint32_t CdOffset;
        std::uint32_t TypeDescriptor;
    };

    std::mutex                                BlocksLock;
    std::vector<std::unique_ptr<std::byte[]>> Blocks;
    std::vector<std::uintptr_t>               EventTexts;
    std::vector<std::uintptr_t>               VariableTexts;

    template <class T>
    [[nodiscard]] T& At(void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    [[nodiscard]] const char* Text(std::uintptr_t StringPointer)
    {
        return reinterpret_cast<const char*>(StringPointer & ~HavokOwnedBit);
    }

    [[nodiscard]] std::byte* NewBlock(std::size_t Size)
    {
        const std::scoped_lock Guard{ BlocksLock };

        return Blocks.emplace_back(std::make_unique<std::byte[]>(Size)).get();
    }

    [[nodiscard]] std::uintptr_t CopyText(std::string_view Name)
    {
        const auto Block = NewBlock(Name.size() + 1);
        std::memcpy(Block, Name.data(), Name.size());

        return reinterpret_cast<std::uintptr_t>(Block);
    }

    template <class T>
    T* Grow(HavokArray& Array, std::int32_t Extra)
    {
        const auto Old = Array.Size;
        const auto Size = Old + Extra;
        const auto Block = NewBlock(static_cast<std::size_t>(Size) * sizeof(T));

        if (Old > 0)
            std::memcpy(Block, Array.Data, static_cast<std::size_t>(Old) * sizeof(T));

        Array.Data = Block;
        Array.Size = Size;
        Array.CapacityAndFlags = static_cast<std::int32_t>(static_cast<std::uint32_t>(Size) | DontDeallocate);

        return reinterpret_cast<T*>(Block) + Old;
    }

    [[nodiscard]] bool Declares(const HavokArray& Names, std::string_view Name)
    {
        const auto Entries = static_cast<const std::uintptr_t*>(Names.Data);

        for (std::int32_t i = 0; i < Names.Size; ++i)
        {
            const auto Declared = Text(Entries[i]);

            if (Declared && std::strlen(Declared) == Name.size() && _strnicmp(Declared, Name.data(), Name.size()) == 0)
                return true;
        }

        return false;
    }

    [[nodiscard]] std::vector<std::int32_t> Duplicates(const HavokArray& Names)
    {
        std::vector<std::int32_t>            Later;
        std::unordered_set<std::string_view> Seen;
        const auto                           Entries = static_cast<const std::uintptr_t*>(Names.Data);

        for (std::int32_t i = 0; i < Names.Size; ++i)
        {
            const auto Declared = Text(Entries[i]);

            if (Declared && !Seen.insert(Declared).second)
                Later.push_back(i);
        }

        return Later;
    }

    void RenameDuplicates(HavokArray& Names, const std::vector<std::int32_t>& Later, std::string_view Kind, const char* GraphName)
    {
        const auto Entries = static_cast<std::uintptr_t*>(Names.Data);

        for (const auto i : Later)
        {
            const std::string Old{ Text(Entries[i]) ? Text(Entries[i]) : "" };
            const auto        Renamed = std::format("MWB_Duplicate{}_{}", i, Old);
            Entries[i] = CopyText(Renamed);
            REX::INFO("Behavior names: '{}' declares the {} '{}' twice; #{} renamed '{}'", GraphName, Kind, Old, i, Renamed);
        }
    }

    std::int32_t InjectEvents(void* Data, void* Strings)
    {
        auto& Names = At<HavokArray>(Strings, EventNamesOffset);
        auto& Infos = At<HavokArray>(Data, EventInfosOffset);

        std::vector<std::uintptr_t> Missing;

        for (std::size_t i = 0; i < BehaviorNames::Events.size(); ++i)
        {
            if (!Declares(Names, BehaviorNames::Events[i]))
                Missing.push_back(EventTexts[i]);
        }

        if (Missing.empty())
            return 0;

        const auto Count = static_cast<std::int32_t>(Missing.size());
        const auto Gap = (std::max)(0, Names.Size - Infos.Size);
        const auto NewNames = Grow<std::uintptr_t>(Names, Count);
        const auto NewInfos = Grow<std::uint32_t>(Infos, Gap + Count);

        for (std::int32_t i = 0; i < Gap + Count; ++i)
            NewInfos[i] = 0;

        for (std::int32_t i = 0; i < Count; ++i)
            NewNames[i] = Missing[i];

        return Count;
    }

    std::int32_t InjectVariables(void* Data, void* Strings, const char* GraphName)
    {
        auto&      Names = At<HavokArray>(Strings, VariableNamesOffset);
        auto&      Infos = At<HavokArray>(Data, VariableInfosOffset);
        const auto Values = At<void*>(Data, InitialValuesOffset);

        std::vector<std::size_t> Missing;

        for (std::size_t i = 0; i < BehaviorNames::Variables.size(); ++i)
        {
            if (!Declares(Names, BehaviorNames::Variables[i].Name))
                Missing.push_back(i);
        }

        if (Missing.empty())
            return 0;

        if (!Values)
        {
            if (Settings::VerboseLog())
                REX::INFO("Behavior names: '{}' has no variable value set, variables left out", GraphName);

            return 0;
        }

        auto& Words = At<HavokArray>(Values, WordValuesOffset);

        if (Names.Size != Infos.Size || Names.Size != Words.Size)
        {
            REX::WARN("Behavior names: '{}' has {} variable names, {} infos and {} values; variables left out", GraphName, Names.Size, Infos.Size, Words.Size);
            return 0;
        }

        const auto Count = static_cast<std::int32_t>(Missing.size());
        const auto NewNames = Grow<std::uintptr_t>(Names, Count);
        const auto NewInfos = Grow<VariableInfo>(Infos, Count);
        const auto NewWords = Grow<std::int32_t>(Words, Count);

        for (std::int32_t i = 0; i < Count; ++i)
        {
            const auto& Variable = BehaviorNames::Variables[Missing[i]];
            NewNames[i] = VariableTexts[Missing[i]];
            NewInfos[i] = VariableInfo{ 0, 0, Variable.Type, 0 };
            NewWords[i] = Variable.InitialValue;
        }

        return Count;
    }

    [[nodiscard]] bool StartsWith(std::string_view Text, std::string_view Prefix)
    {
        return Text.size() >= Prefix.size() && _strnicmp(Text.data(), Prefix.data(), Prefix.size()) == 0;
    }

    [[nodiscard]] bool IsObjectGraph(std::string_view Name)
    {
        constexpr std::string_view TemplateName = "Behavior";
        constexpr std::string_view Extension = ".hkb";

        if (StartsWith(Name, "Pipboy"sv))
            return true;

        if (!StartsWith(Name, TemplateName) || Name.size() < TemplateName.size() + Extension.size() ||
            _stricmp(Name.substr(Name.size() - Extension.size()).data(), Extension.data()) != 0)
        {
            return false;
        }

        const auto Number = Name.substr(TemplateName.size(), Name.size() - TemplateName.size() - Extension.size());

        return std::ranges::all_of(Number, [](char Character) { return Character >= '0' && Character <= '9'; });
    }

    void InjectGraph(void* Graph)
    {
        const auto Data = At<void*>(Graph, GraphDataOffset);
        const auto Strings = Data ? At<void*>(Data, StringDataOffset) : nullptr;

        if (!Strings)
            return;

        const auto Name = Text(At<std::uintptr_t>(Graph, NodeNameOffset));
        const auto GraphName = Name ? Name : "(unnamed)";

        if (IsObjectGraph(GraphName))
        {
            if (Settings::VerboseLog())
                REX::INFO("Behavior names: '{}' is an object's graph (weapon, effect, Pip-Boy), left as it is", GraphName);

            return;
        }
        auto&      EventNames = At<HavokArray>(Strings, EventNamesOffset);
        auto&      VariableNames = At<HavokArray>(Strings, VariableNamesOffset);

        RenameDuplicates(EventNames, Duplicates(EventNames), "event", GraphName);
        RenameDuplicates(VariableNames, Duplicates(VariableNames), "variable", GraphName);

        const auto Events = InjectEvents(Data, Strings);
        const auto Variables = InjectVariables(Data, Strings, GraphName);

        if (Settings::VerboseLog() && (Events > 0 || Variables > 0))
            REX::INFO("Behavior names: {} event(s) and {} variable(s) added to '{}'", Events, Variables, GraphName);
    }

    struct Load
    {
        static bool Thunk(void* This, void* Arguments, void* Stream)
        {
            const bool Loaded = Original(This, Arguments, Stream);

            if (!Loaded)
                return Loaded;

            const auto Container = At<HavokArray*>(This, ContainerOffset);

            if (!Container)
                return Loaded;

            const auto Variants = static_cast<NamedVariant*>(Container->Data);

            for (std::int32_t i = 0; i < Container->Size; ++i)
            {
                const auto ClassName = Text(Variants[i].ClassName);

                if (Variants[i].Variant && ClassName && std::strcmp(ClassName, "hkbBehaviorGraph") == 0)
                    InjectGraph(Variants[i].Variant);
            }

            return Loaded;
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    [[nodiscard]] bool IsLoaderVtable(std::uintptr_t Vtable)
    {
        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator || Locator->Offset != LoaderVtableOffset)
            return false;

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*)) == ".?AVhkxDBData@BShkbHkxDB@@"sv;
    }
}

namespace NameInjection
{
    void Install()
    {
        REL::Relocation<std::uintptr_t> Vtable{ RE::VTABLE::BShkbHkxDB__hkxDBData[LoaderVtableIndex] };

        if (!IsLoaderVtable(Vtable.address()))
        {
            REX::ERROR("BShkbHkxDB::hkxDBData vtable id {:X} is not the loader's. MWB's events and variables only reach the behaviors that "
                       "declare them in their files.",
                Vtable.address());
            return;
        }

        for (const auto Name : BehaviorNames::Events)
            EventTexts.push_back(CopyText(Name));

        for (const auto& Variable : BehaviorNames::Variables)
            VariableTexts.push_back(CopyText(Variable.Name));

        Load::Original = Vtable.write_vfunc(LoadSlot, Load::Thunk);
        REX::INFO("Behavior names: {} events and {} variables added to every behavior loaded", EventTexts.size(), VariableTexts.size());
    }
}
