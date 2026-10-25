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

#include "WeaponConfig.h"
#include "Settings/Settings.h"

#include <glaze/json.hpp>

#include <shared_mutex>
#include <variant>

namespace
{
    struct LoopOptions
    {
        std::optional<float> Delay;
    };

    struct SprintOptions
    {
        std::optional<bool> Start;
        std::optional<bool> Stop;
        std::optional<bool> Equip;
        std::optional<bool> TacticalStart;
        std::optional<bool> Tactical;
        std::optional<bool> TacticalStop;
    };

    struct FireLastOptions
    {
        std::optional<bool> Ready;
        std::optional<bool> Sighted;
    };

    struct TacticalOptions
    {
        std::optional<bool> FirstPerson;
        std::optional<bool> ThirdPerson;
    };

    struct ActionOptions
    {
        std::optional<bool> Empty;
    };

    struct AmmoCheckOptions
    {
        std::optional<bool>        Empty;
        std::optional<std::string> Bone;
    };

    struct BoneOptions
    {
        std::string          Name;
        std::optional<float> X;
        std::optional<float> Y;
        std::optional<float> Z;
        std::optional<float> Pitch;
        std::optional<float> Yaw;
        std::optional<float> Roll;
    };

    struct SlideLockOptions
    {
        std::optional<std::vector<BoneOptions>> Bones;
        std::optional<bool>                     DisableInEmptyReloads;
        std::optional<float>                    BlendIn;
        std::optional<float>                    BlendOut;
        std::optional<std::string>              LockSound;
        std::optional<std::string>              ReleaseSound;
        std::optional<std::vector<std::string>> HideBones;
        std::optional<std::vector<std::string>> ShowBones;
    };

    struct FireModeOptions
    {
        std::optional<std::vector<BoneOptions>> AutoBones;
        std::optional<std::vector<BoneOptions>> SingleBones;
        std::optional<float>                    Blend;
    };

    template <class Options>
    using FeatureValue = std::optional<std::variant<bool, Options>>;

    struct WeaponEntry
    {
        std::optional<std::string>     Plugin;
        std::optional<std::string>     Keyword;
        std::optional<std::string>     Weapon;
        std::optional<std::string>     FormList;
        FeatureValue<LoopOptions>      LoopedReload;
        FeatureValue<SprintOptions>    SprintTransitions;
        std::optional<bool>            SightedReload;
        FeatureValue<TacticalOptions>  TacticalReload;
        FeatureValue<ActionOptions>    Inspect;
        FeatureValue<AmmoCheckOptions> AmmoCheck;
        std::optional<bool>            SpecialAttack;
        FeatureValue<SlideLockOptions> SlideLock;
        FeatureValue<FireModeOptions>  FireModeSwitch;
        std::optional<bool>            AimDownSight;
        std::optional<bool>            Slide;
        FeatureValue<FireLastOptions>  FireLast;
    };

    struct ConfigFile
    {
        std::vector<WeaponEntry> Entries;
    };

    struct ExclusionEntry
    {
        std::optional<std::string> Plugin;
        std::optional<std::string> Keyword;
        std::optional<std::string> Weapon;
        std::optional<std::string> FormList;
        std::vector<std::string>   Exclude;
    };

    struct ExclusionFile
    {
        std::vector<ExclusionEntry> Entries;
    };
}

template <>
struct glz::meta<LoopOptions>
{
    using T = LoopOptions;
    static constexpr auto value = object("delay", &T::Delay);
};

template <>
struct glz::meta<SprintOptions>
{
    using T = SprintOptions;
    static constexpr auto value = object("start", &T::Start, "stop", &T::Stop, "equip", &T::Equip, "tacticalStart", &T::TacticalStart, "tactical", &T::Tactical,
        "tacticalStop", &T::TacticalStop);
};

template <>
struct glz::meta<FireLastOptions>
{
    using T = FireLastOptions;
    static constexpr auto value = object("ready", &T::Ready, "sighted", &T::Sighted);
};

template <>
struct glz::meta<TacticalOptions>
{
    using T = TacticalOptions;
    static constexpr auto value = object("firstPerson", &T::FirstPerson, "thirdPerson", &T::ThirdPerson);
};

template <>
struct glz::meta<ActionOptions>
{
    using T = ActionOptions;
    static constexpr auto value = object("empty", &T::Empty);
};

template <>
struct glz::meta<AmmoCheckOptions>
{
    using T = AmmoCheckOptions;
    static constexpr auto value = object("empty", &T::Empty, "bone", &T::Bone);
};

template <>
struct glz::meta<BoneOptions>
{
    using T = BoneOptions;
    static constexpr auto value = object("name", &T::Name, "x", &T::X, "y", &T::Y, "z", &T::Z, "pitch", &T::Pitch, "yaw", &T::Yaw, "roll", &T::Roll);
};

template <>
struct glz::meta<SlideLockOptions>
{
    using T = SlideLockOptions;
    static constexpr auto value = object("bones", &T::Bones, "disableInEmptyReloads", &T::DisableInEmptyReloads, "blendIn", &T::BlendIn,
        "blendOut", &T::BlendOut, "lockSound", &T::LockSound, "releaseSound", &T::ReleaseSound, "hideBones", &T::HideBones, "showBones",
        &T::ShowBones);
};

template <>
struct glz::meta<FireModeOptions>
{
    using T = FireModeOptions;
    static constexpr auto value = object("autoBones", &T::AutoBones, "singleBones", &T::SingleBones, "blend", &T::Blend);
};

template <>
struct glz::meta<WeaponEntry>
{
    using T = WeaponEntry;
    static constexpr auto value = object("plugin", &T::Plugin, "keyword", &T::Keyword, "weapon", &T::Weapon, "formList", &T::FormList,
        "loopedReload", &T::LoopedReload,
        "sprintTransitions", &T::SprintTransitions, "sightedReload", &T::SightedReload, "tacticalReload", &T::TacticalReload,
        "inspect", &T::Inspect, "ammoCheck", &T::AmmoCheck, "specialAttack", &T::SpecialAttack, "slideLock", &T::SlideLock,
        "fireModeSwitch", &T::FireModeSwitch, "aimDownSight", &T::AimDownSight, "slide", &T::Slide,
        "fireLast", &T::FireLast);
};

template <>
struct glz::meta<ConfigFile>
{
    using T = ConfigFile;
    static constexpr auto value = object("entries", &T::Entries);
};

template <>
struct glz::meta<ExclusionEntry>
{
    using T = ExclusionEntry;
    static constexpr auto value = object("plugin", &T::Plugin, "keyword", &T::Keyword, "weapon", &T::Weapon, "formList", &T::FormList,
        "exclude", &T::Exclude);
};

template <>
struct glz::meta<ExclusionFile>
{
    using T = ExclusionFile;
    static constexpr auto value = object("entries", &T::Entries);
};

namespace
{
    constexpr auto ConfigFolder = "Data/F4SE/Plugins/MWB"sv;
    constexpr auto ExclusionFolder = "Data/F4SE/Plugins/MWB/Exclusions"sv;

    using WeaponConfig::Feature;

    constexpr std::array<std::pair<std::string_view, Feature>, 15> FeatureNames{ {
        { "loopedReload"sv, Feature::LoopedReload },
        { "sprintTransitions"sv, Feature::SprintTransitions },
        { "sightedReload"sv, Feature::SightedReload },
        { "tacticalReload"sv, Feature::TacticalReload },
        { "inspect"sv, Feature::Inspect },
        { "ammoCheck"sv, Feature::AmmoCheck },
        { "specialAttack"sv, Feature::SpecialAttack },
        { "slideLock"sv, Feature::SlideLock },
        { "fireModeSwitch"sv, Feature::FireModeSwitch },
        { "bulletInChamber"sv, Feature::BulletInChamber },
        { "dropUnspentAmmo"sv, Feature::DropUnspentAmmo },
        { "aimDownSight"sv, Feature::AimDownSight },
        { "manualReload"sv, Feature::ManualReload },
        { "slide"sv, Feature::Slide },
        { "fireLast"sv, Feature::FireLast },
    } };

    constexpr glz::opts ReadOptions{ .comments = true };

    constexpr auto UTF8BOM = "\xEF\xBB\xBF"sv;

    using WeaponConfig::ActionRule;
    using WeaponConfig::BoneOffset;
    using WeaponConfig::FireLastRule;
    using WeaponConfig::FireModeRule;
    using WeaponConfig::LoopRule;
    using WeaponConfig::SightedRule;
    using WeaponConfig::SlideLockRule;
    using WeaponConfig::SprintRule;
    using WeaponConfig::TacticalRule;

    template <class Visitor>
    bool AnyListForm(const RE::BGSListForm& List, Visitor&& Visit)
    {
        for (const auto Form : List.arrayOfForms)
        {
            if (Form && Visit(*Form))
                return true;
        }

        if (List.scriptAddedTempForms)
        {
            for (const auto AddedFormID : *List.scriptAddedTempForms)
            {
                const auto Form = RE::TESForm::GetFormByID(AddedFormID);

                if (Form && Visit(*Form))
                    return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool ListHasWeapon(const RE::BGSListForm& List, const RE::TESObjectWEAP* Base)
    {
        return AnyListForm(List, [&](const RE::TESForm& Form) { return &Form == Base; });
    }

    [[nodiscard]] bool ListHasKeywordOf(const RE::BGSListForm& List, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return AnyListForm(List, [&](const RE::TESForm& Form)
        {
            const auto Keyword = Form.As<RE::BGSKeyword>();

            return Keyword && Base->HasKeyword(Keyword, Instance);
        });
    }

    struct RuleSource
    {
        const void*              Form{ nullptr };
        WeaponConfig::SourceKind Kind{ WeaponConfig::SourceKind::None };
        std::string              File;
        std::string              Name;
    };

    template <class Rule>
    struct RuleList
    {
        std::vector<std::pair<const RE::BGSKeyword*, Rule>>    Keywords;
        std::vector<std::pair<const RE::TESObjectWEAP*, Rule>> Weapons;
        std::vector<std::pair<const RE::BGSListForm*, Rule>>   FormLists;
        std::vector<RuleSource>                                Sources;

        void clear()
        {
            Keywords.clear();
            Weapons.clear();
            FormLists.clear();
            Sources.clear();
        }

        [[nodiscard]] const void* MatchForm(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance) const
        {
            if (!Base)
                return nullptr;

            for (const auto& Entry : Weapons)
            {
                if (Entry.first == Base)
                    return Entry.first;
            }

            for (const auto& Entry : FormLists)
            {
                if (ListHasWeapon(*Entry.first, Base))
                    return Entry.first;
            }

            for (const auto& Entry : Keywords)
            {
                if (Base->HasKeyword(Entry.first, Instance))
                    return Entry.first;
            }

            for (const auto& Entry : FormLists)
            {
                if (ListHasKeywordOf(*Entry.first, Base, Instance))
                    return Entry.first;
            }

            return nullptr;
        }

        [[nodiscard]] RuleSource SourceOf(const void* Form) const
        {
            const auto Known = std::ranges::find(Sources, Form, &RuleSource::Form);

            return Known != Sources.end() ? *Known : RuleSource{ .Form = Form };
        }

        [[nodiscard]] std::size_t size() const
        {
            return Keywords.size() + Weapons.size() + FormLists.size();
        }

        [[nodiscard]] std::optional<Rule> MatchWeapon(const RE::TESObjectWEAP* Base) const
        {
            for (const auto& [Weapon, WeaponRule] : Weapons)
            {
                if (Weapon == Base)
                    return WeaponRule;
            }

            for (const auto& [List, ListRule] : FormLists)
            {
                if (ListHasWeapon(*List, Base))
                    return ListRule;
            }

            return std::nullopt;
        }

        template <class Visitor>
        void VisitKeywordMatches(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance, Visitor&& Visit) const
        {
            for (const auto& [Keyword, KeywordRule] : Keywords)
            {
                if (Base->HasKeyword(Keyword, Instance) && Visit(KeywordRule))
                    return;
            }

            for (const auto& [List, ListRule] : FormLists)
            {
                if (ListHasKeywordOf(*List, Base, Instance) && Visit(ListRule))
                    return;
            }
        }

        [[nodiscard]] std::optional<Rule> Match(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance) const
        {
            if (!Base)
                return std::nullopt;

            if (auto WeaponRule = MatchWeapon(Base))
                return WeaponRule;

            std::optional<Rule> KeywordRule;

            VisitKeywordMatches(Base, Instance, [&](const Rule& Found)
            {
                KeywordRule = Found;
                return true;
            });

            return KeywordRule;
        }
    };

    RuleList<LoopRule>      LoopRules;
    RuleList<SprintRule>    SprintRules;
    RuleList<SightedRule>   SightedRules;
    RuleList<TacticalRule>  TacticalRules;
    RuleList<ActionRule>    InspectRules;
    RuleList<ActionRule>    AmmoCheckRules;
    RuleList<SightedRule>   SpecialAttackRules;
    RuleList<SlideLockRule> SlideLockRules;
    RuleList<FireModeRule>  FireModeRules;
    RuleList<SightedRule>   AimDownSightRules;
    RuleList<SightedRule>   SlideRules;
    RuleList<FireLastRule>  FireLastRules;

    std::array<RuleList<SightedRule>, static_cast<std::size_t>(Feature::Count)> ExclusionRules;

    std::shared_mutex                                                            PlayerLock;
    std::array<RuleList<SightedRule>, static_cast<std::size_t>(Feature::Count)> PlayerExclusionRules;
    std::vector<WeaponConfig::PlayerExclusion>                                   PlayerData;
    std::atomic<std::uint32_t>                                                   ConfigGeneration{ 0 };

    [[nodiscard]] bool IsExcluded(Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        const auto Index = static_cast<std::size_t>(TargetFeature);

        if (ExclusionRules[Index].Match(Base, Instance))
            return true;

        std::shared_lock Lock{ PlayerLock };

        return PlayerExclusionRules[Index].Match(Base, Instance).has_value();
    }

    template <class Rule>
    [[nodiscard]] std::optional<Rule> MatchUnlessExcluded(const RuleList<Rule>& List, Feature TargetFeature, RE::TESObjectWEAP* Base,
        const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return IsExcluded(TargetFeature, Base, Instance) ? std::nullopt : List.Match(Base, Instance);
    }

    [[nodiscard]] bool IsFormID(std::string_view Text)
    {
        return Text.starts_with("0x"sv) || Text.starts_with("0X"sv);
    }

    [[nodiscard]] std::optional<std::uint32_t> ParseHex(std::string_view Text)
    {
        Text.remove_prefix(2);

        std::uint32_t Value = 0;
        const auto [End, Error] = std::from_chars(Text.data(), Text.data() + Text.size(), Value, 16);

        if (Text.empty() || Error != std::errc{} || End != Text.data() + Text.size())
            return std::nullopt;

        return Value;
    }

    [[nodiscard]] bool IsLoaded(std::string_view Plugin)
    {
        const auto DataHandler = RE::TESDataHandler::GetSingleton();

        return DataHandler && DataHandler->LookupModByName(Plugin);
    }

    [[nodiscard]] RE::TESForm* LookupForm(std::string_view Plugin, std::uint32_t FormID)
    {
        const auto DataHandler = RE::TESDataHandler::GetSingleton();
        const auto File = DataHandler ? DataHandler->LookupModByName(Plugin) : nullptr;

        if (!File || File->compileIndex == 0xFF)
            return nullptr;

        const bool IsLight = File->compileIndex == 0xFE;
        const auto ResolvedFormID = IsLight ?
            (0xFEu << 24) | (static_cast<std::uint32_t>(File->smallFileCompileIndex) << 12) | (FormID & 0xFFF) :
            (static_cast<std::uint32_t>(File->compileIndex) << 24) | (FormID & 0xFFFFFF);

        return RE::TESForm::GetFormByID(ResolvedFormID);
    }

    template <class T>
    [[nodiscard]] const T* LookupFormID(const std::optional<std::string>& Plugin, std::string_view ID, std::string_view Kind, std::string_view Where)
    {
        if (!Plugin)
        {
            REX::WARN("  {}: FormID {} needs \"plugin\"", Where, ID);
            return nullptr;
        }

        const auto ParsedFormID = ParseHex(ID);

        if (!ParsedFormID)
        {
            REX::WARN("  {}: '{}' is not a hexadecimal FormID", Where, ID);
            return nullptr;
        }

        const auto Form = LookupForm(*Plugin, *ParsedFormID);

        if (!Form)
        {
            REX::WARN("  {}: no form {:X} in '{}'", Where, *ParsedFormID, *Plugin);
            return nullptr;
        }

        const auto Typed = Form->As<T>();

        if (!Typed)
            REX::WARN("  {}: {:08X} is not a {}", Where, Form->GetFormID(), Kind);

        return Typed;
    }

    template <class Entry>
    [[nodiscard]] const RE::BGSKeyword* FindKeyword(const Entry& FormEntry, std::string_view Where)
    {
        const std::string& ID = *FormEntry.Keyword;

        if (IsFormID(ID))
            return LookupFormID<RE::BGSKeyword>(FormEntry.Plugin, ID, "keyword", Where);

        const auto Keyword = RE::TESForm::GetFormByEditorID<RE::BGSKeyword>(ID);

        if (!Keyword)
            REX::WARN("  {}: no keyword with editor ID '{}'", Where, ID);

        return Keyword;
    }

    template <class Entry>
    [[nodiscard]] const RE::TESObjectWEAP* FindWeapon(const Entry& FormEntry, std::string_view Where)
    {
        const std::string& ID = *FormEntry.Weapon;

        if (!IsFormID(ID))
        {
            REX::WARN("  {}: weapon '{}' needs a FormID starting with \"0x\": the game drops weapon editor IDs", Where, ID);
            return nullptr;
        }

        return LookupFormID<RE::TESObjectWEAP>(FormEntry.Plugin, ID, "weapon", Where);
    }

    template <class Entry>
    [[nodiscard]] const RE::BGSListForm* FindFormList(const Entry& FormEntry, std::string_view Where)
    {
        const std::string& ID = *FormEntry.FormList;

        if (!IsFormID(ID))
        {
            REX::WARN("  {}: form list '{}' needs a FormID starting with \"0x\": the game drops form list editor IDs", Where, ID);
            return nullptr;
        }

        const auto List = LookupFormID<RE::BGSListForm>(FormEntry.Plugin, ID, "form list", Where);

        if (!List)
            return nullptr;

        std::size_t Weapons = 0;
        std::size_t Keywords = 0;
        std::size_t Ignored = 0;

        AnyListForm(*List, [&](const RE::TESForm& Form)
        {
            if (Form.Is<RE::TESObjectWEAP>())
                ++Weapons;
            else if (Form.Is<RE::BGSKeyword>())
                ++Keywords;
            else
                ++Ignored;

            return false;
        });

        REX::INFO("  {}: form list {:08X} holds {} weapon(s) and {} keyword(s){}", Where, List->GetFormID(), Weapons, Keywords,
            Ignored ? std::format("; {} other item(s) ignored", Ignored) : std::string{});

        return List;
    }

    template <class Entry>
    [[nodiscard]] bool HasOneForm(const Entry& FormEntry, std::string_view Where)
    {
        const int Count = FormEntry.Keyword.has_value() + FormEntry.Weapon.has_value() + FormEntry.FormList.has_value();

        if (Count != 1)
            REX::WARN("  {}: needs one of \"keyword\", \"weapon\" or \"formList\"", Where);

        return Count == 1;
    }

    template <class Entry, class Visitor>
    void VisitEntryForm(const Entry& FormEntry, std::string_view Where, Visitor&& Visit)
    {
        if (FormEntry.Keyword)
        {
            if (const auto Keyword = FindKeyword(FormEntry, Where))
                Visit(Keyword);
        }
        else if (FormEntry.Weapon)
        {
            if (const auto Weapon = FindWeapon(FormEntry, Where))
                Visit(Weapon);
        }
        else if (const auto List = FindFormList(FormEntry, Where))
        {
            Visit(List);
        }
    }

    [[nodiscard]] std::optional<LoopRule> MakeRule(const LoopOptions& Options, std::string_view Where)
    {
        if (Options.Delay && *Options.Delay < 0.0f)
        {
            REX::WARN("  {}: delay {} is not a number of seconds >= 0", Where, *Options.Delay);
            return std::nullopt;
        }

        return LoopRule{ .Delay = Options.Delay };
    }

    [[nodiscard]] std::optional<SprintRule> MakeRule(const SprintOptions& Options, std::string_view Where)
    {
        const SprintRule Rule{
            .Start = Options.Start.value_or(true),
            .Stop = Options.Stop.value_or(true),
            .Equip = Options.Equip.value_or(false),
            .TacticalStart = Options.TacticalStart.value_or(false),
            .Tactical = Options.Tactical.value_or(false),
            .TacticalStop = Options.TacticalStop.value_or(false),
        };

        if (Rule == SprintRule{ .Start = false, .Stop = false })
        {
            REX::WARN("  {}: every clip is off, it does nothing", Where);
            return std::nullopt;
        }

        return Rule;
    }

    [[nodiscard]] std::optional<FireLastRule> MakeRule(const FireLastOptions& Options, std::string_view Where)
    {
        const FireLastRule Rule{ .Ready = Options.Ready.value_or(true), .Sighted = Options.Sighted.value_or(true) };

        if (!Rule.Ready && !Rule.Sighted)
        {
            REX::WARN("  {}: \"ready\" and \"sighted\" both false, it does nothing", Where);
            return std::nullopt;
        }

        return Rule;
    }

    [[nodiscard]] std::optional<TacticalRule> MakeRule(const TacticalOptions& Options, std::string_view Where)
    {
        const TacticalRule Rule{ .FirstPerson = Options.FirstPerson.value_or(true), .ThirdPerson = Options.ThirdPerson.value_or(false) };

        if (!Rule.FirstPerson && !Rule.ThirdPerson)
        {
            REX::WARN("  {}: \"firstPerson\" and \"thirdPerson\" both false, it does nothing", Where);
            return std::nullopt;
        }

        return Rule;
    }

    [[nodiscard]] std::optional<std::vector<BoneOffset>> MakeBones(const std::vector<BoneOptions>& BoneList, std::string_view Key, std::string_view Where)
    {
        std::vector<BoneOffset> Bones;

        for (std::size_t i = 0; i < BoneList.size(); ++i)
        {
            const auto& Bone = BoneList[i];

            if (Bone.Name.empty())
            {
                REX::WARN("  {}: {}[{}] needs \"name\"", Where, Key, i);
                return std::nullopt;
            }

            RE::BSFixedString CaseInsensitiveName{ Bone.Name };

            if (std::ranges::any_of(Bones, [&](const auto& Known) { return Known.Name == CaseInsensitiveName; }))
            {
                REX::WARN("  {}: bone '{}' listed twice in {}", Where, Bone.Name, Key);
                return std::nullopt;
            }

            Bones.push_back({ std::move(CaseInsensitiveName), { Bone.X.value_or(0.0f), Bone.Y.value_or(0.0f), Bone.Z.value_or(0.0f) },
                { Bone.Pitch.value_or(0.0f), Bone.Roll.value_or(0.0f), Bone.Yaw.value_or(0.0f) } });
        }

        return Bones;
    }

    [[nodiscard]] const RE::BGSSoundDescriptorForm* FindSound(const std::optional<std::string>& EntryPlugin, std::string_view Reference, std::string_view Key,
        std::string_view Where)
    {
        const auto KeyWhere = std::format("{} {}", Where, Key);

        if (Reference.empty())
        {
            REX::WARN("  {}: empty sound", KeyWhere);
            return nullptr;
        }

        if (const auto Separator = Reference.find('|'); Separator != std::string_view::npos)
        {
            const std::optional<std::string> Plugin{ std::string{ Reference.substr(0, Separator) } };
            const auto                       ID = Reference.substr(Separator + 1);

            if (!IsFormID(ID))
            {
                REX::WARN("  {}: '{}' needs a FormID starting with \"0x\" after the '|'", KeyWhere, Reference);
                return nullptr;
            }

            return LookupFormID<RE::BGSSoundDescriptorForm>(Plugin, ID, "sound descriptor", KeyWhere);
        }

        if (IsFormID(Reference))
            return LookupFormID<RE::BGSSoundDescriptorForm>(EntryPlugin, Reference, "sound descriptor", KeyWhere);

        const auto Sound = RE::TESForm::GetFormByEditorID<RE::BGSSoundDescriptorForm>(Reference);

        if (!Sound)
            REX::WARN("  {}: no sound descriptor with editor ID '{}'", KeyWhere, Reference);

        return Sound;
    }

    [[nodiscard]] std::optional<std::vector<RE::BSFixedString>> MakeNodeNames(const std::vector<std::string>& Names, std::string_view Key, std::string_view Where)
    {
        std::vector<RE::BSFixedString> Nodes;

        for (const auto& Name : Names)
        {
            if (Name.empty())
            {
                REX::WARN("  {}: empty name in {}", Where, Key);
                return std::nullopt;
            }

            RE::BSFixedString CaseInsensitiveName{ Name };

            if (std::ranges::find(Nodes, CaseInsensitiveName) != Nodes.end())
            {
                REX::WARN("  {}: '{}' listed twice in {}", Where, Name, Key);
                return std::nullopt;
            }

            Nodes.push_back(std::move(CaseInsensitiveName));
        }

        return Nodes;
    }

    [[nodiscard]] std::optional<SlideLockRule> MakeRule(const SlideLockOptions& Options, const std::optional<std::string>& Plugin, std::string_view Where)
    {
        if (!Options.Bones || Options.Bones->empty())
        {
            REX::WARN("  {}: needs \"bones\", the list of bones to move", Where);
            return std::nullopt;
        }

        for (const auto [Key, Value] : { std::pair{ "blendIn"sv, Options.BlendIn }, std::pair{ "blendOut"sv, Options.BlendOut } })
        {
            if (Value && *Value < 0.0f)
            {
                REX::WARN("  {}: {} {} is not a number of seconds >= 0", Where, Key, *Value);
                return std::nullopt;
            }
        }

        auto Bones = MakeBones(*Options.Bones, "bones"sv, Where);

        if (!Bones)
            return std::nullopt;

        SlideLockRule Rule{ .Bones = std::move(*Bones),
            .DisableInEmptyReloads = Options.DisableInEmptyReloads.value_or(false),
            .BlendIn = Options.BlendIn.value_or(0.0f),
            .BlendOut = Options.BlendOut.value_or(0.0f) };

        for (const auto& [Key, Source, Target] : { std::tuple{ "lockSound"sv, &Options.LockSound, &Rule.LockSound },
                 std::tuple{ "releaseSound"sv, &Options.ReleaseSound, &Rule.ReleaseSound } })
        {
            if (!*Source)
                continue;

            *Target = FindSound(Plugin, **Source, Key, Where);

            if (!*Target)
                return std::nullopt;
        }

        for (const auto& [Key, Source, Target] : { std::tuple{ "hideBones"sv, &Options.HideBones, &Rule.HideNodes },
                 std::tuple{ "showBones"sv, &Options.ShowBones, &Rule.ShowNodes } })
        {
            if (!*Source)
                continue;

            auto Made = MakeNodeNames(**Source, Key, Where);

            if (!Made)
                return std::nullopt;

            *Target = std::move(*Made);
        }

        for (const auto& Hidden : Rule.HideNodes)
        {
            if (std::ranges::find(Rule.ShowNodes, Hidden) != Rule.ShowNodes.end())
            {
                REX::WARN("  {}: '{}' is in both hideBones and showBones", Where, Hidden.c_str());
                return std::nullopt;
            }
        }

        return Rule;
    }

    [[nodiscard]] std::optional<FireModeRule> MakeRule(const FireModeOptions& Options, std::string_view Where)
    {
        if (Options.Blend && *Options.Blend < 0.0f)
        {
            REX::WARN("  {}: blend {} is not a number of seconds >= 0", Where, *Options.Blend);
            return std::nullopt;
        }

        FireModeRule Rule{ .Blend = Options.Blend.value_or(0.0f) };

        for (const auto& [Key, Source, Target] : { std::tuple{ "autoBones"sv, &Options.AutoBones, &Rule.AutoBones },
                 std::tuple{ "singleBones"sv, &Options.SingleBones, &Rule.SingleBones } })
        {
            if (!*Source)
                continue;

            auto Made = MakeBones(**Source, Key, Where);

            if (!Made)
                return std::nullopt;

            *Target = std::move(*Made);
        }

        return Rule;
    }

    [[nodiscard]] std::string Describe(const LoopRule& Rule)
    {
        return Rule.Delay ? std::format(", delay {:.3f}s", *Rule.Delay) : std::string{};
    }

    [[nodiscard]] std::string Describe(const SprintRule& Rule)
    {
        const auto OnOff = [](bool Value) { return Value ? "on" : "off"; };

        return std::format(", start {}, stop {}, equip {}, tactical start {}, tactical {}, tactical stop {}", OnOff(Rule.Start), OnOff(Rule.Stop),
            OnOff(Rule.Equip), OnOff(Rule.TacticalStart), OnOff(Rule.Tactical), OnOff(Rule.TacticalStop));
    }

    [[nodiscard]] std::string Describe(const FireLastRule& Rule)
    {
        const auto OnOff = [](bool Value) { return Value ? "on" : "off"; };

        return std::format(", ready {}, sighted {}", OnOff(Rule.Ready), OnOff(Rule.Sighted));
    }

    [[nodiscard]] std::string Describe(const SightedRule&)
    {
        return {};
    }

    [[nodiscard]] ActionRule MakeRule(const AmmoCheckOptions& Options, std::string_view Where)
    {
        ActionRule Rule{ .Empty = Options.Empty.value_or(false) };

        if (Options.Bone)
        {
            if (Options.Bone->empty())
                REX::WARN("  {}: \"bone\" is empty; the default bone is used", Where);
            else
                Rule.Bone = *Options.Bone;
        }

        return Rule;
    }

    [[nodiscard]] std::string Describe(const ActionRule& Rule)
    {
        auto Text = Rule.Empty ? ", empty clip"s : std::string{};

        if (!Rule.Bone.empty())
            Text += std::format(", display at {}", Rule.Bone.c_str());

        return Text;
    }

    [[nodiscard]] std::string Describe(const TacticalRule& Rule)
    {
        return Rule.FirstPerson && Rule.ThirdPerson ? ", first and third person"s :
               Rule.ThirdPerson                     ? ", third person only"s :
                                                      std::string{};
    }

    [[nodiscard]] std::string DescribeBones(const std::vector<BoneOffset>& Bones, std::string_view Key)
    {
        std::string Text;

        for (const auto& Bone : Bones)
        {
            Text += std::format("{}{} ({}, {}, {}; pitch {}, roll {}, yaw {})", Text.empty() ? std::format(", {} ", Key) : ", "s, Bone.Name.c_str(),
                Bone.Offset[0], Bone.Offset[1], Bone.Offset[2], Bone.Rotation[0], Bone.Rotation[1], Bone.Rotation[2]);
        }

        return Text;
    }

    [[nodiscard]] std::string DescribeNodes(const std::vector<RE::BSFixedString>& Nodes, std::string_view Key)
    {
        std::string Text;

        for (const auto& Node : Nodes)
            Text += std::format("{}{}", Text.empty() ? std::format(", {} ", Key) : ", "s, Node.c_str());

        return Text;
    }

    [[nodiscard]] std::string Describe(const SlideLockRule& Rule)
    {
        const auto Sound = [](std::string_view Key, const RE::BGSSoundDescriptorForm* Form)
        {
            return Form ? std::format(", {} {:08X}", Key, Form->GetFormID()) : std::string{};
        };

        return std::format("{}{}, blend in {:.3f}s, out {:.3f}s{}{}{}{}", DescribeBones(Rule.Bones, "bones"sv),
            Rule.DisableInEmptyReloads ? ", off in empty reloads" : "", Rule.BlendIn, Rule.BlendOut, Sound("lock sound"sv, Rule.LockSound),
            Sound("release sound"sv, Rule.ReleaseSound), DescribeNodes(Rule.HideNodes, "hides"sv), DescribeNodes(Rule.ShowNodes, "shows"sv));
    }

    [[nodiscard]] std::string Describe(const FireModeRule& Rule)
    {
        if (Rule.AutoBones.empty() && Rule.SingleBones.empty())
            return {};

        return std::format("{}{}, blend {:.3f}s", DescribeBones(Rule.AutoBones, "autoBones"sv), DescribeBones(Rule.SingleBones, "singleBones"sv),
            Rule.Blend);
    }

    template <class Options>
    [[nodiscard]] std::optional<Options> Resolve(const FeatureValue<Options>& Value)
    {
        if (!Value)
            return std::nullopt;

        if (const auto On = std::get_if<bool>(&*Value))
            return *On ? std::optional<Options>{ Options{} } : std::nullopt;

        return std::get<Options>(*Value);
    }

    template <class T, class Rule>
    bool AddRule(std::vector<std::pair<const T*, Rule>>& List, const T* Form, const Rule& NewRule, std::string_view Kind, std::string_view Where)
    {
        const auto Known = std::ranges::find(List, Form, &std::pair<const T*, Rule>::first);

        if (Known != List.end())
        {
            REX::INFO("  {}: {} {:08X} already listed, ignored here", Where, Kind, Form->GetFormID());
            return false;
        }

        List.emplace_back(Form, NewRule);
        REX::INFO("  {}: {} {:08X}{}", Where, Kind, Form->GetFormID(), Describe(NewRule));

        return true;
    }

    [[nodiscard]] std::string FormName(const RE::TESForm& Form)
    {
        if (Form.Is<RE::BGSKeyword>())
        {
            const auto EditorID = Form.GetFormEditorID();

            if (EditorID && *EditorID)
                return EditorID;
        }
        else if (Form.Is<RE::TESObjectWEAP>())
        {
            const auto Name = RE::TESFullName::GetFullName(Form);

            if (!Name.empty())
                return std::string{ Name };
        }

        return std::format("{:08X}", Form.GetFormID());
    }

    [[nodiscard]] std::string_view FileOf(std::string_view Where)
    {
        return Where.substr(0, Where.find(" entries["sv));
    }

    template <class T, class Rule>
    void AddRule(RuleList<Rule>& List, const T* Form, const Rule& NewRule, std::string_view Where)
    {
        using WeaponConfig::SourceKind;

        bool       Added = false;
        SourceKind Kind = SourceKind::Weapon;

        if constexpr (std::is_same_v<T, RE::BGSKeyword>)
        {
            Added = AddRule(List.Keywords, Form, NewRule, "keyword", Where);
            Kind = SourceKind::Keyword;
        }
        else if constexpr (std::is_same_v<T, RE::BGSListForm>)
        {
            Added = AddRule(List.FormLists, Form, NewRule, "form list", Where);
            Kind = SourceKind::FormList;
        }
        else
        {
            Added = AddRule(List.Weapons, Form, NewRule, "weapon", Where);
        }

        if (Added)
            List.Sources.push_back({ .Form = Form, .Kind = Kind, .File = std::string{ FileOf(Where) }, .Name = FormName(*Form) });
    }

    template <class T>
    void AddFeatures(const WeaponEntry& Entry, const T* Form, std::string_view Where)
    {
        if (const auto Options = Resolve(Entry.LoopedReload))
        {
            const auto FeatureWhere = std::format("{} loopedReload", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(LoopRules, Form, *Rule, FeatureWhere);
        }

        if (const auto Options = Resolve(Entry.SprintTransitions))
        {
            const auto FeatureWhere = std::format("{} sprintTransitions", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(SprintRules, Form, *Rule, FeatureWhere);
        }

        if (Entry.SightedReload.value_or(false))
            AddRule(SightedRules, Form, SightedRule{}, std::format("{} sightedReload", Where));

        if (const auto Options = Resolve(Entry.TacticalReload))
        {
            const auto FeatureWhere = std::format("{} tacticalReload", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(TacticalRules, Form, *Rule, FeatureWhere);
        }

        if (const auto Options = Resolve(Entry.Inspect))
            AddRule(InspectRules, Form, ActionRule{ .Empty = Options->Empty.value_or(false) }, std::format("{} inspect", Where));

        if (const auto Options = Resolve(Entry.AmmoCheck))
        {
            const auto FeatureWhere = std::format("{} ammoCheck", Where);

            AddRule(AmmoCheckRules, Form, MakeRule(*Options, FeatureWhere), FeatureWhere);
        }

        if (Entry.SpecialAttack.value_or(false))
            AddRule(SpecialAttackRules, Form, SightedRule{}, std::format("{} specialAttack", Where));

        if (const auto Options = Resolve(Entry.SlideLock))
        {
            const auto FeatureWhere = std::format("{} slideLock", Where);

            if (const auto Rule = MakeRule(*Options, Entry.Plugin, FeatureWhere))
                AddRule(SlideLockRules, Form, *Rule, FeatureWhere);
        }

        if (const auto Options = Resolve(Entry.FireModeSwitch))
        {
            const auto FeatureWhere = std::format("{} fireModeSwitch", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(FireModeRules, Form, *Rule, FeatureWhere);
        }

        if (Entry.AimDownSight.value_or(false))
            AddRule(AimDownSightRules, Form, SightedRule{}, std::format("{} aimDownSight", Where));

        if (Entry.Slide.value_or(false))
            AddRule(SlideRules, Form, SightedRule{}, std::format("{} slide", Where));

        if (const auto Options = Resolve(Entry.FireLast))
        {
            const auto FeatureWhere = std::format("{} fireLast", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(FireLastRules, Form, *Rule, FeatureWhere);
        }
    }

    void AddEntry(const WeaponEntry& Entry, std::string_view Where)
    {
        if (!HasOneForm(Entry, Where))
            return;

        const bool TurnsAnyFeatureOn = Resolve(Entry.LoopedReload) || Resolve(Entry.SprintTransitions) || Entry.SightedReload.value_or(false) ||
                                       Resolve(Entry.TacticalReload) || Resolve(Entry.Inspect) || Resolve(Entry.AmmoCheck) ||
                                       Entry.SpecialAttack.value_or(false) || Resolve(Entry.SlideLock) || Resolve(Entry.FireModeSwitch) ||
                                       Entry.AimDownSight.value_or(false) || Entry.Slide.value_or(false) || Resolve(Entry.FireLast);

        if (!TurnsAnyFeatureOn)
        {
            REX::WARN("  {}: turns no feature on", Where);
            return;
        }

        if (Entry.Plugin && !IsLoaded(*Entry.Plugin))
        {
            REX::INFO("  {}: '{}' is not loaded, skipped", Where, *Entry.Plugin);
            return;
        }

        VisitEntryForm(Entry, Where, [&](const auto* Form) { AddFeatures(Entry, Form, Where); });
    }

    void AddExclusion(const ExclusionEntry& Entry, std::string_view Where)
    {
        if (!HasOneForm(Entry, Where))
            return;

        std::vector<Feature> Features;

        for (const auto& Name : Entry.Exclude)
        {
            const auto Known = std::ranges::find(FeatureNames, std::string_view{ Name }, &std::pair<std::string_view, Feature>::first);

            if (Known == FeatureNames.end())
            {
                REX::WARN("  {}: '{}' is not a feature, ignored", Where, Name);
                continue;
            }

            Features.push_back(Known->second);
        }

        if (Features.empty())
        {
            REX::WARN("  {}: excludes no feature", Where);
            return;
        }

        if (Entry.Plugin && !IsLoaded(*Entry.Plugin))
        {
            REX::INFO("  {}: '{}' is not loaded, skipped", Where, *Entry.Plugin);
            return;
        }

        const auto Add = [&](const auto* Form)
        {
            for (const auto ExcludedFeature : Features)
            {
                const auto Name = std::ranges::find(FeatureNames, ExcludedFeature, &std::pair<std::string_view, Feature>::second)->first;

                AddRule(ExclusionRules[static_cast<std::size_t>(ExcludedFeature)], Form, SightedRule{}, std::format("{} excludes {}", Where, Name));
            }
        };

        VisitEntryForm(Entry, Where, Add);
    }

    [[nodiscard]] std::optional<std::string> ReadText(const std::filesystem::path& Path)
    {
        std::ifstream File{ Path, std::ios::binary };

        if (!File.is_open())
        {
            REX::WARN("Cannot open {}", Path.filename().string());
            return std::nullopt;
        }

        std::string Buffer{ std::istreambuf_iterator<char>{ File }, std::istreambuf_iterator<char>{} };

        if (Buffer.starts_with(UTF8BOM))
            Buffer.erase(0, UTF8BOM.size());

        return Buffer;
    }

    template <class File>
    void ParseFile(const std::filesystem::path& Path, std::string_view Label)
    {
        const auto Name = std::format("{}{}", Label, Path.filename().string());
        const auto Buffer = ReadText(Path);

        if (!Buffer)
            return;

        REX::INFO("Reading {}", Name);

        File Parsed;

        if (const auto Error = glz::read<ReadOptions>(Parsed, *Buffer))
        {
            REX::WARN("  {}: not read, nothing in it is used\n{}", Name, glz::format_error(Error, *Buffer));
            return;
        }

        for (std::size_t i = 0; i < Parsed.Entries.size(); ++i)
        {
            const auto Where = std::format("{} entries[{}]", Name, i);

            if constexpr (std::is_same_v<File, ExclusionFile>)
                AddExclusion(Parsed.Entries[i], Where);
            else
                AddEntry(Parsed.Entries[i], Where);
        }
    }

    [[nodiscard]] std::vector<std::filesystem::path> JsonFiles(std::string_view Folder)
    {
        std::vector<std::filesystem::path> Files;
        std::error_code                    Error;

        if (!std::filesystem::is_directory(Folder, Error))
            return Files;

        for (const auto& Entry : std::filesystem::directory_iterator(Folder, Error))
        {
            const auto Extension = Entry.path().extension().string();

            if (Entry.is_regular_file() && _stricmp(Extension.c_str(), ".json") == 0)
                Files.push_back(Entry.path());
        }

        std::ranges::sort(Files);

        return Files;
    }

    using WeaponConfig::PlayerExclusion;

    const std::filesystem::path PlayerFile{ "Data/MCM/Settings/MWB_Exclusions.json" };

    template <class Visitor>
    void VisitFeatureList(Feature TargetFeature, Visitor&& Visit)
    {
        switch (TargetFeature)
        {
            case Feature::LoopedReload:
                Visit(LoopRules);
                break;
            case Feature::SprintTransitions:
                Visit(SprintRules);
                break;
            case Feature::SightedReload:
                Visit(SightedRules);
                break;
            case Feature::TacticalReload:
                Visit(TacticalRules);
                break;
            case Feature::Inspect:
                Visit(InspectRules);
                break;
            case Feature::AmmoCheck:
                Visit(AmmoCheckRules);
                break;
            case Feature::SpecialAttack:
                Visit(SpecialAttackRules);
                break;
            case Feature::SlideLock:
                Visit(SlideLockRules);
                break;
            case Feature::FireModeSwitch:
                Visit(FireModeRules);
                break;
            case Feature::AimDownSight:
                Visit(AimDownSightRules);
                break;
            case Feature::Slide:
                Visit(SlideRules);
                break;
            case Feature::FireLast:
                Visit(FireLastRules);
                break;
            default:
                break;
        }
    }

    [[nodiscard]] bool KeywordUsed(const RE::BGSKeyword* Keyword)
    {
        const auto InList = [&](const auto& List)
        {
            return std::ranges::any_of(List.Keywords, [&](const auto& Entry) { return Entry.first == Keyword; }) ||
                   std::ranges::any_of(List.FormLists, [&](const auto& Entry)
                   {
                       return AnyListForm(*Entry.first, [&](const RE::TESForm& Form) { return &Form == Keyword; });
                   });
        };

        bool Used = false;

        for (std::size_t i = 0; i < static_cast<std::size_t>(Feature::Count) && !Used; ++i)
        {
            VisitFeatureList(static_cast<Feature>(i), [&](const auto& List) { Used = InList(List); });
            Used = Used || InList(ExclusionRules[i]);
        }

        return Used;
    }

    [[nodiscard]] std::optional<std::pair<std::string, std::uint32_t>> FileAndLocalID(const RE::TESForm& Form)
    {
        if (Form.IsCreated())
            return std::nullopt;

        const auto File = Form.GetFile(0);

        if (!File)
            return std::nullopt;

        const bool IsLight = File->compileIndex == 0xFE;
        const auto LocalID = Form.GetFormID() & (IsLight ? 0xFFFu : 0xFFFFFFu);

        return std::pair{ std::string{ File->GetFilename() }, LocalID };
    }

    [[nodiscard]] bool SamePlugin(std::string_view Left, std::string_view Right)
    {
        return Left.size() == Right.size() && _strnicmp(Left.data(), Right.data(), Left.size()) == 0;
    }

    [[nodiscard]] bool Hits(const PlayerExclusion& Entry, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        if (!Entry.Form || !Base)
            return false;

        if (Entry.Keyword)
            return Base->HasKeyword(Entry.Form->As<RE::BGSKeyword>(), Instance);

        return Entry.Form == Base;
    }

    void RebuildPlayerRules()
    {
        for (auto& List : PlayerExclusionRules)
            List.clear();

        for (const auto& Entry : PlayerData)
        {
            if (!Entry.Form)
                continue;

            for (const auto ExcludedFeature : Entry.Features)
            {
                auto& List = PlayerExclusionRules[static_cast<std::size_t>(ExcludedFeature)];

                if (Entry.Keyword)
                    List.Keywords.emplace_back(Entry.Form->As<RE::BGSKeyword>(), SightedRule{});
                else
                    List.Weapons.emplace_back(Entry.Form->As<RE::TESObjectWEAP>(), SightedRule{});
            }
        }

        ++ConfigGeneration;
    }

    void SavePlayerExclusions()
    {
        ExclusionFile Out;

        for (const auto& Entry : PlayerData)
        {
            if (Entry.Features.empty())
                continue;

            ExclusionEntry Written{ .Plugin = Entry.Plugin };
            const auto     ID = std::format("0x{:06X}", Entry.LocalID);

            if (Entry.Keyword)
                Written.Keyword = ID;
            else
                Written.Weapon = ID;

            for (const auto ExcludedFeature : Entry.Features)
                Written.Exclude.emplace_back(WeaponConfig::FeatureKey(ExcludedFeature));

            Out.Entries.push_back(std::move(Written));
        }

        std::string Buffer;

        if (const auto Error = glz::write<glz::opts{ .prettify = true }>(Out, Buffer))
        {
            REX::WARN("Player exclusions: cannot write the JSON");
            return;
        }

        std::error_code Error;
        std::filesystem::create_directories(PlayerFile.parent_path(), Error);

        auto Temporary = PlayerFile;
        Temporary += ".tmp";

        {
            std::ofstream File{ Temporary, std::ios::binary | std::ios::trunc };

            if (!File.is_open())
            {
                REX::WARN("Player exclusions: cannot write {}", Temporary.string());
                return;
            }

            File << Buffer;
        }

        std::filesystem::rename(Temporary, PlayerFile, Error);

        if (Error)
            REX::WARN("Player exclusions: cannot replace {} ({})", PlayerFile.string(), Error.message());
        else
            REX::INFO("Player exclusions: {} entries saved", Out.Entries.size());
    }

    void AddPlayerEntry(std::vector<PlayerExclusion>& Entries, const ExclusionEntry& Entry, std::string_view Where)
    {
        if (!Entry.Plugin || Entry.FormList || Entry.Keyword.has_value() == Entry.Weapon.has_value())
        {
            REX::WARN("  {}: needs \"plugin\" and one of \"keyword\" or \"weapon\"", Where);
            return;
        }

        const bool  Keyword = Entry.Keyword.has_value();
        const auto& ID = Keyword ? *Entry.Keyword : *Entry.Weapon;
        const auto  LocalID = IsFormID(ID) ? ParseHex(ID) : std::nullopt;

        if (!LocalID)
        {
            REX::WARN("  {}: '{}' is not a FormID starting with \"0x\"", Where, ID);
            return;
        }

        PlayerExclusion Made{ .Plugin = *Entry.Plugin, .LocalID = *LocalID, .Keyword = Keyword };

        if (IsLoaded(Made.Plugin))
        {
            const auto Form = LookupForm(Made.Plugin, Made.LocalID);

            if (Form && (Keyword ? Form->Is<RE::BGSKeyword>() : Form->Is<RE::TESObjectWEAP>()))
                Made.Form = Form;
            else
                REX::WARN("  {}: no {} {} in '{}'", Where, Keyword ? "keyword" : "weapon", ID, Made.Plugin);
        }

        for (const auto& Name : Entry.Exclude)
        {
            const auto Known = WeaponConfig::FeatureFromKey(Name);

            if (!Known)
            {
                REX::WARN("  {}: '{}' is not a feature, ignored", Where, Name);
                continue;
            }

            if (std::ranges::find(Made.Features, *Known) == Made.Features.end())
                Made.Features.push_back(*Known);
        }

        if (Made.Features.empty())
            return;

        const auto Same = std::ranges::find_if(Entries, [&](const PlayerExclusion& Known)
        {
            return Known.Keyword == Made.Keyword && Known.LocalID == Made.LocalID && SamePlugin(Known.Plugin, Made.Plugin);
        });

        if (Same == Entries.end())
        {
            Entries.push_back(std::move(Made));
            return;
        }

        for (const auto Added : Made.Features)
        {
            if (std::ranges::find(Same->Features, Added) == Same->Features.end())
                Same->Features.push_back(Added);
        }
    }

    void LoadPlayerExclusions()
    {
        std::vector<PlayerExclusion> Loaded;
        std::error_code              Error;

        if (std::filesystem::is_regular_file(PlayerFile, Error))
        {
            if (const auto Buffer = ReadText(PlayerFile))
            {
                ExclusionFile Parsed;

                if (const auto ParseError = glz::read<ReadOptions>(Parsed, *Buffer))
                {
                    REX::WARN("Player exclusions: {} not read, nothing in it is used\n{}", PlayerFile.string(), glz::format_error(ParseError, *Buffer));
                }
                else
                {
                    for (std::size_t i = 0; i < Parsed.Entries.size(); ++i)
                        AddPlayerEntry(Loaded, Parsed.Entries[i], std::format("MWB_Exclusions.json entries[{}]", i));
                }
            }
        }

        std::unique_lock Lock{ PlayerLock };
        PlayerData = std::move(Loaded);

        for (auto& Entry : PlayerData)
            std::ranges::sort(Entry.Features);

        RebuildPlayerRules();
        REX::INFO("Player exclusions: {} entries", PlayerData.size());
    }
}

namespace WeaponConfig
{
    void Load()
    {
        LoopRules.clear();
        SprintRules.clear();
        SightedRules.clear();
        TacticalRules.clear();
        InspectRules.clear();
        AmmoCheckRules.clear();
        SpecialAttackRules.clear();
        SlideLockRules.clear();
        FireModeRules.clear();
        AimDownSightRules.clear();
        SlideRules.clear();
        FireLastRules.clear();

        for (auto& List : ExclusionRules)
            List.clear();

        std::error_code Error;

        if (!std::filesystem::is_directory(ConfigFolder, Error))
        {
            REX::WARN("{} does not exist: no weapon uses any feature", ConfigFolder);
            return;
        }

        const auto Files = JsonFiles(ConfigFolder);

        for (const auto& Path : Files)
            ParseFile<ConfigFile>(Path, ""sv);

        const auto ExclusionFiles = JsonFiles(ExclusionFolder);

        for (const auto& Path : ExclusionFiles)
            ParseFile<ExclusionFile>(Path, "Exclusions/"sv);

        std::size_t ExclusionCount = 0;

        for (const auto& List : ExclusionRules)
            ExclusionCount += List.size();

        REX::INFO("Exclusions: {} from {} file(s)", ExclusionCount, ExclusionFiles.size());

        const std::size_t FormListCount = LoopRules.FormLists.size() + SprintRules.FormLists.size() + SightedRules.FormLists.size() +
                                          TacticalRules.FormLists.size() + InspectRules.FormLists.size() + AmmoCheckRules.FormLists.size() +
                                          SpecialAttackRules.FormLists.size() + SlideLockRules.FormLists.size() + FireModeRules.FormLists.size() +
                                          AimDownSightRules.FormLists.size() + SlideRules.FormLists.size() + FireLastRules.FormLists.size();

        REX::INFO("Form lists: {} feature(s) turned on by a form list", FormListCount);

        REX::INFO("Looped reload: {} keyword(s) and {} weapon(s); sprint transitions: {} keyword(s) and {} weapon(s); "
                  "sighted reload: {} keyword(s) and {} weapon(s); tactical reload: {} keyword(s) and {} weapon(s); inspect / ammo check "
                  "/ special attack: {} / {} / {} entries; slide lock: {} keyword(s) and {} weapon(s); fire mode switch: {} keyword(s) and {} "
                  "weapon(s); aim down sight clips: {} keyword(s) and {} weapon(s); slide clip: {} keyword(s) and {} weapon(s); last shot clips: {} keyword(s) and {} "
                  "weapon(s); from {} file(s)",
            LoopRules.Keywords.size(), LoopRules.Weapons.size(), SprintRules.Keywords.size(), SprintRules.Weapons.size(),
            SightedRules.Keywords.size(), SightedRules.Weapons.size(), TacticalRules.Keywords.size(), TacticalRules.Weapons.size(),
            InspectRules.size(), AmmoCheckRules.size(), SpecialAttackRules.size(), SlideLockRules.Keywords.size(), SlideLockRules.Weapons.size(),
            FireModeRules.Keywords.size(), FireModeRules.Weapons.size(), AimDownSightRules.Keywords.size(), AimDownSightRules.Weapons.size(),
            SlideRules.Keywords.size(), SlideRules.Weapons.size(), FireLastRules.Keywords.size(), FireLastRules.Weapons.size(), Files.size());

        LoadPlayerExclusions();
    }

    std::optional<LoopRule> MatchLoop(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(LoopRules, Feature::LoopedReload, Base, Instance);
    }

    std::optional<SprintRule> MatchSprint(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(SprintRules, Feature::SprintTransitions, Base, Instance);
    }

    bool MatchSighted(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(SightedRules, Feature::SightedReload, Base, Instance).has_value();
    }

    bool MatchAimDownSight(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(AimDownSightRules, Feature::AimDownSight, Base, Instance).has_value();
    }

    bool MatchSlide(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(SlideRules, Feature::Slide, Base, Instance).has_value();
    }

    std::optional<FireLastRule> MatchFireLast(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(FireLastRules, Feature::FireLast, Base, Instance);
    }

    std::optional<ActionRule> MatchAction(WeaponAction Action, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        switch (Action)
        {
            case WeaponAction::Inspect:
                return MatchUnlessExcluded(InspectRules, Feature::Inspect, Base, Instance);
            case WeaponAction::AmmoCheck:
                return MatchUnlessExcluded(AmmoCheckRules, Feature::AmmoCheck, Base, Instance);
            case WeaponAction::SpecialAttack:
                return MatchUnlessExcluded(SpecialAttackRules, Feature::SpecialAttack, Base, Instance) ? std::optional<ActionRule>{ ActionRule{} } : std::nullopt;
        }

        return std::nullopt;
    }

    std::optional<TacticalRule> MatchTactical(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        if (!Base || IsExcluded(Feature::TacticalReload, Base, Instance))
            return std::nullopt;

        if (auto WeaponRule = TacticalRules.MatchWeapon(Base))
            return WeaponRule;

        std::optional<TacticalRule> MergedFromKeywords;

        TacticalRules.VisitKeywordMatches(Base, Instance, [&](const TacticalRule& KeywordRule)
        {
            MergedFromKeywords = MergedFromKeywords ? TacticalRule{ .FirstPerson = MergedFromKeywords->FirstPerson || KeywordRule.FirstPerson,
                                                          .ThirdPerson = MergedFromKeywords->ThirdPerson || KeywordRule.ThirdPerson } :
                                                      KeywordRule;
            return false;
        });

        return MergedFromKeywords;
    }

    std::optional<SlideLockRule> MatchSlideLock(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(SlideLockRules, Feature::SlideLock, Base, Instance);
    }

    std::optional<FireModeRule> MatchFireMode(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return MatchUnlessExcluded(FireModeRules, Feature::FireModeSwitch, Base, Instance);
    }

    bool Excluded(Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return IsExcluded(TargetFeature, Base, Instance);
    }

    RE::TESForm* LookupPluginForm(std::string_view Plugin, std::uint32_t FormID)
    {
        return LookupForm(Plugin, FormID);
    }

    std::uint32_t Generation()
    {
        return ConfigGeneration.load();
    }

    std::string_view FeatureKey(Feature TargetFeature)
    {
        const auto Known = std::ranges::find(FeatureNames, TargetFeature, &std::pair<std::string_view, Feature>::second);

        return Known != FeatureNames.end() ? Known->first : std::string_view{};
    }

    std::optional<Feature> FeatureFromKey(std::string_view Key)
    {
        const auto Known = std::ranges::find(FeatureNames, Key, &std::pair<std::string_view, Feature>::first);

        return Known != FeatureNames.end() ? std::optional{ Known->second } : std::nullopt;
    }

    std::vector<FeatureReport> Report(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        std::vector<FeatureReport> Reports;

        if (!Base)
            return Reports;

        std::shared_lock Lock{ PlayerLock };

        for (const auto& Named : FeatureNames)
        {
            const auto Which = Named.second;
            const auto Index = static_cast<std::size_t>(Which);
            FeatureReport Made{ .Which = Which };
            std::optional<RuleSource> Raw;

            VisitFeatureList(Which, [&](const auto& List)
            {
                if (const auto Form = List.MatchForm(Base, Instance))
                    Raw = List.SourceOf(Form);
            });

            bool OptionOn = true;

            if (!Raw)
            {
                switch (Which)
                {
                    case Feature::AmmoCheck:
                        Raw = RuleSource{ .Kind = SourceKind::EveryGun };
                        OptionOn = Settings::AmmoCheckEveryGun();
                        break;
                    case Feature::BulletInChamber:
                        Raw = RuleSource{ .Kind = SourceKind::PlayerOption };
                        OptionOn = Settings::BulletInChamber();
                        break;
                    case Feature::DropUnspentAmmo:
                        Raw = RuleSource{ .Kind = SourceKind::PlayerOption };
                        OptionOn = Settings::DropUnspentAmmo();
                        break;
                    case Feature::ManualReload:
                        Raw = RuleSource{ .Kind = SourceKind::PlayerOption };
                        OptionOn = Settings::ManualReload();
                        break;
                    default:
                        break;
                }
            }

            if (!Raw)
            {
                Reports.push_back(std::move(Made));
                continue;
            }

            RuleSource Shown = *Raw;
            Made.State = OptionOn ? FeatureState::On : FeatureState::OptionOff;

            if (const auto Form = ExclusionRules[Index].MatchForm(Base, Instance))
            {
                Made.State = FeatureState::ExcludedByMod;
                Shown = ExclusionRules[Index].SourceOf(Form);
            }
            else if (const auto PlayerForm = PlayerExclusionRules[Index].MatchForm(Base, Instance))
            {
                const auto Excluder = static_cast<const RE::TESForm*>(PlayerForm);
                Made.State = FeatureState::ExcludedByPlayer;
                Shown = RuleSource{ .Kind = Excluder->Is<RE::BGSKeyword>() ? SourceKind::Keyword : SourceKind::Weapon, .Name = FormName(*Excluder) };
            }

            Made.Kind = Shown.Kind;
            Made.File = std::move(Shown.File);
            Made.Name = std::move(Shown.Name);
            Reports.push_back(std::move(Made));
        }

        return Reports;
    }

    std::vector<const RE::BGSKeyword*> TargetKeywords(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        std::vector<const RE::BGSKeyword*> Keywords;

        if (!Base)
            return Keywords;

        const auto Add = [&](RE::BGSKeyword* Keyword)
        {
            if (!Keyword || std::ranges::find(Keywords, Keyword) != Keywords.end())
                return RE::BSContainer::ForEachResult::kContinue;

            const auto EditorID = Keyword->GetFormEditorID();
            const bool Anims = EditorID && _strnicmp(EditorID, "Anims", 5) == 0;

            if ((Anims || KeywordUsed(Keyword)) && FileAndLocalID(*Keyword))
                Keywords.push_back(Keyword);

            return RE::BSContainer::ForEachResult::kContinue;
        };

        Base->ForEachKeyword(Add);

        if (Instance && Instance->keywords)
            Instance->keywords->ForEachKeyword(Add);

        return Keywords;
    }

    bool TogglePlayerExclusion(const RE::TESForm* Target, Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        if (!Target || !Base || TargetFeature >= Feature::Count)
            return false;

        std::unique_lock Lock{ PlayerLock };

        if (PlayerExclusionRules[static_cast<std::size_t>(TargetFeature)].Match(Base, Instance))
        {
            for (auto& Entry : PlayerData)
            {
                if (Hits(Entry, Base, Instance))
                    std::erase(Entry.Features, TargetFeature);
            }

            REX::INFO("Player exclusions: {} back on for {:08X}", FeatureKey(TargetFeature), Base->GetFormID());
        }
        else
        {
            const auto Origin = FileAndLocalID(*Target);

            if (!Origin)
                return false;

            auto Entry = std::ranges::find_if(PlayerData, [&](const PlayerExclusion& Known) { return Known.Form == Target; });

            if (Entry == PlayerData.end())
            {
                PlayerData.push_back({ .Plugin = Origin->first, .LocalID = Origin->second, .Keyword = Target->Is<RE::BGSKeyword>(), .Form = Target });
                Entry = std::prev(PlayerData.end());
            }

            if (std::ranges::find(Entry->Features, TargetFeature) == Entry->Features.end())
            {
                Entry->Features.push_back(TargetFeature);
                std::ranges::sort(Entry->Features);
            }

            REX::INFO("Player exclusions: {} excluded for {} {:08X}", FeatureKey(TargetFeature), Entry->Keyword ? "keyword" : "weapon", Target->GetFormID());
        }

        std::erase_if(PlayerData, [](const PlayerExclusion& Entry) { return Entry.Features.empty(); });
        RebuildPlayerRules();
        SavePlayerExclusions();

        return true;
    }

    std::vector<PlayerExclusion> PlayerExclusions()
    {
        std::shared_lock Lock{ PlayerLock };

        return PlayerData;
    }

    bool RemovePlayerExclusion(std::size_t Index, std::optional<Feature> TargetFeature)
    {
        std::unique_lock Lock{ PlayerLock };

        if (Index >= PlayerData.size())
            return false;

        if (TargetFeature)
            std::erase(PlayerData[Index].Features, *TargetFeature);
        else
            PlayerData[Index].Features.clear();

        REX::INFO("Player exclusions: {} removed from {} 0x{:06X} ({})", TargetFeature ? FeatureKey(*TargetFeature) : "everything"sv,
            PlayerData[Index].Plugin, PlayerData[Index].LocalID, PlayerData[Index].Keyword ? "keyword" : "weapon");

        std::erase_if(PlayerData, [](const PlayerExclusion& Entry) { return Entry.Features.empty(); });
        RebuildPlayerRules();
        SavePlayerExclusions();

        return true;
    }
}
