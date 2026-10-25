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

#include "ExclusionPanel.h"
#include "Config/WeaponConfig.h"
#include "Core/EquippedWeapon.h"
#include "Settings/Settings.h"

namespace
{
    using Value = Scaleform::GFx::Value;
    using Movie = Scaleform::GFx::Movie;
    using Params = Scaleform::GFx::FunctionHandler::Params;

    constexpr int WeaponLockTries = 50;

    [[nodiscard]] std::optional<Core::EquippedWeapon> WeaponInHand()
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();

        if (!Player)
            return std::nullopt;

        std::optional<Core::EquippedWeapon> Weapon;

        for (int Try = 0; Try < WeaponLockTries; ++Try)
        {
            if (Core::TryGetEquippedWeapon(Player, Weapon))
                return Weapon;

            std::this_thread::yield();
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string FormLabel(const RE::TESForm& Form)
    {
        if (Form.Is<RE::BGSKeyword>())
        {
            const auto EditorID = Form.GetFormEditorID();

            if (EditorID && *EditorID)
                return EditorID;
        }
        else
        {
            const auto Name = RE::TESFullName::GetFullName(Form);

            if (!Name.empty())
                return std::string{ Name };
        }

        return std::format("{:08X}", Form.GetFormID());
    }

    [[nodiscard]] std::string FormOrigin(const RE::TESForm& Form)
    {
        const auto File = Form.GetFile(0);

        if (!File)
            return std::format("{:08X}", Form.GetFormID());

        const bool IsLight = File->compileIndex == 0xFE;

        return std::format("{} 0x{:06X}", File->GetFilename(), Form.GetFormID() & (IsLight ? 0xFFFu : 0xFFFFFFu));
    }

    void SetText(Value& Object, std::string_view Name, const std::string& Text)
    {
        Object.SetMember(Name, Value{ Text.c_str() });
    }

    [[nodiscard]] Value Target(Movie* View, const RE::TESForm& Form, bool Keyword)
    {
        Value Made;
        View->CreateObject(&Made);
        Made.SetMember("id"sv, Value{ Form.GetFormID() });
        Made.SetMember("keyword"sv, Value{ Keyword });
        SetText(Made, "label"sv, FormLabel(Form));
        SetText(Made, "origin"sv, FormOrigin(Form));

        return Made;
    }

    void Build(Movie* View, Value& Panel)
    {
        View->CreateObject(&Panel);

        Value Targets;
        Value Features;
        Value Entries;
        View->CreateArray(&Targets);
        View->CreateArray(&Features);
        View->CreateArray(&Entries);

        const auto Weapon = WeaponInHand();
        const bool HasWeapon = Weapon && Weapon->Base;
        Panel.SetMember("weapon"sv, Value{ HasWeapon });

        if (HasWeapon)
        {
            SetText(Panel, "name"sv, FormLabel(*Weapon->Base));
            SetText(Panel, "origin"sv, FormOrigin(*Weapon->Base));
            Targets.PushBack(Target(View, *Weapon->Base, false));

            for (const auto Keyword : WeaponConfig::TargetKeywords(Weapon->Base, Weapon->Instance))
                Targets.PushBack(Target(View, *Keyword, true));

            for (const auto& Report : WeaponConfig::Report(Weapon->Base, Weapon->Instance))
            {
                Value Feature;
                View->CreateObject(&Feature);
                SetText(Feature, "key"sv, std::string{ WeaponConfig::FeatureKey(Report.Which) });
                Feature.SetMember("state"sv, Value{ static_cast<std::int32_t>(Report.State) });
                Feature.SetMember("kind"sv, Value{ static_cast<std::int32_t>(Report.Kind) });
                SetText(Feature, "file"sv, Report.File);
                SetText(Feature, "source"sv, Report.Name);
                Features.PushBack(Feature);
            }
        }

        for (const auto& Exclusion : WeaponConfig::PlayerExclusions())
        {
            Value Entry;
            View->CreateObject(&Entry);
            Entry.SetMember("keyword"sv, Value{ Exclusion.Keyword });
            Entry.SetMember("loaded"sv, Value{ Exclusion.Form != nullptr });
            SetText(Entry, "label"sv, Exclusion.Form ? FormLabel(*Exclusion.Form) : std::format("0x{:06X}", Exclusion.LocalID));
            SetText(Entry, "origin"sv, std::format("{} 0x{:06X}", Exclusion.Plugin, Exclusion.LocalID));

            Value Keys;
            View->CreateArray(&Keys);

            for (const auto Excluded : Exclusion.Features)
                Keys.PushBack(Value{ std::string{ WeaponConfig::FeatureKey(Excluded) }.c_str() });

            Entry.SetMember("features"sv, Keys);
            Entries.PushBack(Entry);
        }

        Panel.SetMember("targets"sv, Targets);
        Panel.SetMember("features"sv, Features);
        Panel.SetMember("entries"sv, Entries);
    }

    [[nodiscard]] std::optional<std::uint32_t> UIntArgument(const Params& Call, std::uint32_t Index)
    {
        if (Index >= Call.argCount)
            return std::nullopt;

        const auto& Argument = Call.args[Index];

        if (Argument.IsUInt())
            return Argument.GetUInt();

        if (Argument.IsInt())
            return static_cast<std::uint32_t>(Argument.GetInt());

        if (Argument.IsNumber())
            return static_cast<std::uint32_t>(Argument.GetNumber());

        return std::nullopt;
    }

    [[nodiscard]] std::string_view StringArgument(const Params& Call, std::uint32_t Index)
    {
        if (Index >= Call.argCount || !Call.args[Index].IsString())
            return {};

        const auto Text = Call.args[Index].GetString();

        return Text ? std::string_view{ Text } : std::string_view{};
    }

    void Toggle(const Params& Call)
    {
        const auto TargetID = UIntArgument(Call, 0);
        const auto Feature = WeaponConfig::FeatureFromKey(StringArgument(Call, 1));
        const auto Weapon = WeaponInHand();

        if (!TargetID || !Feature || !Weapon || !Weapon->Base)
            return;

        const auto Form = RE::TESForm::GetFormByID(*TargetID);
        const auto Keywords = WeaponConfig::TargetKeywords(Weapon->Base, Weapon->Instance);
        const bool Allowed = Form && (Form == Weapon->Base || std::ranges::find(Keywords, Form) != Keywords.end());

        if (!Allowed)
        {
            REX::WARN("Exclusion panel: {:08X} is neither the weapon in hand nor one of its keywords", *TargetID);
            return;
        }

        WeaponConfig::TogglePlayerExclusion(Form, *Feature, Weapon->Base, Weapon->Instance);
    }

    void Remove(const Params& Call)
    {
        const auto Index = UIntArgument(Call, 0);
        const auto Key = StringArgument(Call, 1);

        if (!Index)
            return;

        const auto Feature = Key.empty() ? std::nullopt : WeaponConfig::FeatureFromKey(Key);

        if (!Key.empty() && !Feature)
            return;

        WeaponConfig::RemovePlayerExclusion(*Index, Feature);
    }

    class PanelFunction :
        public Scaleform::GFx::FunctionHandler
    {
    public:
        using Action = void (*)(const Params&);

        explicit PanelFunction(Action Run) :
            Run(Run)
        {}

        void Call(const Params& Call) override
        {
            if (Run)
                Run(Call);

            if (Call.retVal && Call.movie)
                Build(Call.movie, *Call.retVal);
        }

    private:
        Action Run;
    };

    class LogFunction :
        public Scaleform::GFx::FunctionHandler
    {
    public:
        void Call(const Params& Call) override
        {
            if (Settings::VerboseLog())
                REX::INFO("Exclusion panel: {}", StringArgument(Call, 0));
        }
    };

    PanelFunction* GetFunction{ nullptr };
    PanelFunction* ToggleFunction{ nullptr };
    PanelFunction* RemoveFunction{ nullptr };
    LogFunction*   LogText{ nullptr };

    bool RegisterFunctions(Movie* View, Value* Root)
    {
        if (!View || !Root)
            return false;

        if (!GetFunction)
        {
            GetFunction = new PanelFunction{ nullptr };
            ToggleFunction = new PanelFunction{ Toggle };
            RemoveFunction = new PanelFunction{ Remove };
            LogText = new LogFunction{};
        }

        for (const auto& [Name, Function] : { std::pair<std::string_view, Scaleform::GFx::FunctionHandler*>{ "GetExclusionPanel"sv, GetFunction },
                 std::pair<std::string_view, Scaleform::GFx::FunctionHandler*>{ "ToggleExclusion"sv, ToggleFunction },
                 std::pair<std::string_view, Scaleform::GFx::FunctionHandler*>{ "RemoveExclusion"sv, RemoveFunction },
                 std::pair<std::string_view, Scaleform::GFx::FunctionHandler*>{ "Log"sv, LogText } })
        {
            Value Made;
            View->CreateFunction(&Made, Function);
            Root->SetMember(Name, Made);
        }

        return true;
    }
}

namespace ExclusionPanel
{
    void Register()
    {
        const auto Interface = F4SE::GetScaleformInterface();

        if (!Interface || !Interface->Register("MWB", RegisterFunctions))
            REX::WARN("Cannot register the Scaleform functions: the exclusion panel in the MCM will not work");
    }
}
