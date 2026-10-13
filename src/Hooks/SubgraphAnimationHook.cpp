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

#include "SubgraphAnimationHook.h"
#include "Settings/Settings.h"

namespace
{
    constexpr REL::ID LoadedFilesID{ 1374178, 2258157 };
    constexpr REL::ID FileLoadedID{ 78256, 2258172 };
    constexpr REL::ID NextNodeID{ 849404, 2258082 };
    constexpr REL::ID SubgraphFilesID{ 522222, 2257796 };

    constexpr std::size_t LoadedFilesScanSize = 0x1C0;
    constexpr std::size_t FileLoadedScanSize = 0x8A0;
    constexpr std::size_t NextNodeSiteCount = 2;
    constexpr std::size_t MaxLoggedNames = 16;

    constexpr std::ptrdiff_t HasFileListOffset = 0x1A4;
    constexpr std::ptrdiff_t SubgraphIdentifierOffset = 0x198;
    constexpr std::ptrdiff_t NodeTypeOffset = 0x43;
    constexpr std::ptrdiff_t ReferenceBehaviorNameOffset = 0x88;
    constexpr std::ptrdiff_t ClipAnimationNameOffset = 0x90;
    constexpr std::uint8_t   ReferenceNodeType = 2;
    constexpr std::uint8_t   ClipNodeType = 4;

    struct FileList
    {
        const RE::BSFixedString* Data;
        std::uint32_t            Capacity;
        std::uint32_t            Padding;
        std::uint32_t            Size;
    };

    using SubgraphFilesSignature = const FileList*(const void*);
    using FileLoadedSignature = std::int32_t(void**, const RE::BSFixedString*, void*);
    using NextNodeSignature = void*(void*);

    struct Scope
    {
        std::uint8_t*                   Data;
        bool                            Built{ false };
        std::unordered_set<std::string> Listed;
        std::vector<std::string>        Missing;
        std::size_t                     MissingCount{ 0 };
    };

    thread_local Scope* Active = nullptr;

    template <class T>
    [[nodiscard]] T Read(const void* Base, std::ptrdiff_t Offset)
    {
        return *reinterpret_cast<const T*>(reinterpret_cast<std::uintptr_t>(Base) + Offset);
    }

    [[nodiscard]] std::string LowerStem(std::string_view Path)
    {
        if (const auto Slash = Path.find_last_of("\\/"); Slash != std::string_view::npos)
            Path.remove_prefix(Slash + 1);

        if (const auto Dot = Path.find_last_of('.'); Dot != std::string_view::npos)
            Path = Path.substr(0, Dot);

        std::string Stem{ Path };
        std::ranges::transform(Stem, Stem.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

        return Stem;
    }

    void BuildListed(Scope& Current)
    {
        Current.Built = true;

        static REL::Relocation<SubgraphFilesSignature> SubgraphFiles{ SubgraphFilesID };
        const auto List = SubgraphFiles(Current.Data + SubgraphIdentifierOffset);

        if (!List || !List->Data)
            return;

        for (std::uint32_t i = 0; i < List->Size; ++i)
        {
            if (const auto Path = List->Data[i].c_str(); Path && *Path)
                Current.Listed.insert(LowerStem(Path));
        }
    }

    [[nodiscard]] const char* FileNameOf(const void* Node, std::uint8_t Type)
    {
        const auto Offset = Type == ClipNodeType ? ClipAnimationNameOffset : ReferenceBehaviorNameOffset;
        const auto Name = Read<std::uintptr_t>(Node, Offset) & ~std::uintptr_t{ 1 };

        return reinterpret_cast<const char*>(Name);
    }

    [[nodiscard]] bool Skip(const void* Node)
    {
        const auto Type = Read<std::uint8_t>(Node, NodeTypeOffset);

        if (Type != ClipNodeType && Type != ReferenceNodeType)
            return false;

        const auto Name = FileNameOf(Node, Type);

        if (!Name || !*Name)
            return false;

        if (!Active->Built)
            BuildListed(*Active);

        auto Stem = LowerStem(Name);

        if (Active->Listed.contains(Stem))
            return true;

        ++Active->MissingCount;

        if (Active->Missing.size() < MaxLoggedNames)
            Active->Missing.push_back(std::move(Stem));

        return false;
    }

    template <std::size_t N>
    struct NextNode
    {
        static void* Thunk(void* Iterator)
        {
            auto Node = Original(Iterator);

            if (!Active)
                return Node;

            while (Node && Skip(Node))
                Node = Original(Iterator);

            return Node;
        }

        inline static REL::Relocation<NextNodeSignature> Original;
    };

    struct FileLoaded
    {
        static std::int32_t Thunk(void** Context, const RE::BSFixedString* File, void* Resource)
        {
            const auto Data = Context ? static_cast<std::uint8_t*>(*Context) : nullptr;

            if (!Data || Active || Data[HasFileListOffset] == 0)
                return Original(Context, File, Resource);

            const auto HasFileList = Data[HasFileListOffset];
            Scope      Current{ Data };

            Active = &Current;
            Data[HasFileListOffset] = 0;

            const auto Result = Original(Context, File, Resource);

            Data[HasFileListOffset] = HasFileList;
            Active = nullptr;

            if (Current.MissingCount > 0 && Settings::VerboseLog())
            {
                std::string Names;

                for (const auto& Name : Current.Missing)
                    Names += Names.empty() ? Name : ", " + Name;

                if (Current.MissingCount > Current.Missing.size())
                    Names += ", ...";

                REX::INFO("Subgraph animations: {} has {} file(s) missing from its AnimTextData, searched in the subgraph folders: {}",
                    File && File->c_str() ? File->c_str() : "?", Current.MissingCount, Names);
            }

            return Result;
        }

        inline static REL::Relocation<FileLoadedSignature> Original;
    };

    [[nodiscard]] std::vector<std::uintptr_t> FindCalls(std::uintptr_t Function, std::size_t ScanSize, std::uintptr_t Target)
    {
        std::vector<std::uintptr_t> Found;

        for (std::size_t i = 0; i + 5 <= ScanSize; ++i)
        {
            const auto At = Function + i;

            if (*reinterpret_cast<const std::uint8_t*>(At) != 0xE8)
                continue;

            const auto Callee = static_cast<std::uintptr_t>(static_cast<std::intptr_t>(At + 5) + *reinterpret_cast<const std::int32_t*>(At + 1));

            if (Callee == Target)
                Found.push_back(At);
        }

        return Found;
    }

    template <std::size_t N>
    void WriteNextNode(std::uintptr_t Site)
    {
        NextNode<N>::Original = REL::GetTrampoline().write_call<5>(Site, NextNode<N>::Thunk);
        REX::INFO("Subgraph animation node hook installed at {:X} (chaining to {:X})", Site, NextNode<N>::Original.address());
    }
}

namespace Hooks
{
    void InstallSubgraphAnimationHook()
    {
        const auto LoadedFiles = REL::Relocation<std::uintptr_t>{ LoadedFilesID }.address();
        const auto FileLoadedFunction = REL::Relocation<std::uintptr_t>{ FileLoadedID }.address();
        const auto NextNodeFunction = REL::Relocation<std::uintptr_t>{ NextNodeID }.address();

        const auto FileSites = FindCalls(LoadedFiles, LoadedFilesScanSize, FileLoadedFunction);
        const auto NodeSites = FindCalls(FileLoadedFunction, FileLoadedScanSize, NextNodeFunction);

        if (FileSites.size() != 1 || NodeSites.size() != NextNodeSiteCount)
        {
            REX::WARN("Subgraph animation hook: {} call(s) to the file handler and {} to the node iterator, expected 1 and {} "
                      "(another mod may have them). Clips missing from a weapon's AnimTextData won't load.",
                FileSites.size(), NodeSites.size(), NextNodeSiteCount);
            return;
        }

        static_assert(NextNodeSiteCount == 2);

        WriteNextNode<0>(NodeSites[0]);
        WriteNextNode<1>(NodeSites[1]);

        FileLoaded::Original = REL::GetTrampoline().write_call<5>(FileSites.front(), FileLoaded::Thunk);
        REX::INFO("Subgraph animation hook installed at {:X} (chaining to {:X})", FileSites.front(), FileLoaded::Original.address());
    }
}
