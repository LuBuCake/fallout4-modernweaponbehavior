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

#include <glaze/json.hpp>

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
        FeatureValue<LoopOptions>      LoopedReload;
        FeatureValue<SprintOptions>    SprintTransitions;
        std::optional<bool>            SightedReload;
        FeatureValue<TacticalOptions>  TacticalReload;
        FeatureValue<ActionOptions>    Inspect;
        FeatureValue<AmmoCheckOptions> AmmoCheck;
        std::optional<bool>            SpecialAttack;
        FeatureValue<SlideLockOptions> SlideLock;
        FeatureValue<FireModeOptions>  FireModeSwitch;
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
    static constexpr auto value = object("start", &T::Start, "stop", &T::Stop, "equip", &T::Equip);
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
        "blendOut", &T::BlendOut);
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
    static constexpr auto value = object("plugin", &T::Plugin, "keyword", &T::Keyword, "weapon", &T::Weapon, "loopedReload", &T::LoopedReload,
        "sprintTransitions", &T::SprintTransitions, "sightedReload", &T::SightedReload, "tacticalReload", &T::TacticalReload,
        "inspect", &T::Inspect, "ammoCheck", &T::AmmoCheck, "specialAttack", &T::SpecialAttack, "slideLock", &T::SlideLock,
        "fireModeSwitch", &T::FireModeSwitch);
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
    static constexpr auto value = object("plugin", &T::Plugin, "keyword", &T::Keyword, "weapon", &T::Weapon, "exclude", &T::Exclude);
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

    constexpr std::array<std::pair<std::string_view, Feature>, 11> FeatureNames{ {
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
    } };

    constexpr glz::opts ReadOptions{ .comments = true };

    constexpr auto UTF8BOM = "\xEF\xBB\xBF"sv;

    using WeaponConfig::ActionRule;
    using WeaponConfig::BoneOffset;
    using WeaponConfig::FireModeRule;
    using WeaponConfig::LoopRule;
    using WeaponConfig::SightedRule;
    using WeaponConfig::SlideLockRule;
    using WeaponConfig::SprintRule;
    using WeaponConfig::TacticalRule;

    template <class Rule>
    struct RuleList
    {
        std::vector<std::pair<const RE::BGSKeyword*, Rule>>    Keywords;
        std::vector<std::pair<const RE::TESObjectWEAP*, Rule>> Weapons;

        void clear()
        {
            Keywords.clear();
            Weapons.clear();
        }

        [[nodiscard]] std::optional<Rule> Match(RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance) const
        {
            if (!Base)
                return std::nullopt;

            for (const auto& [Weapon, WeaponRule] : Weapons)
            {
                if (Weapon == Base)
                    return WeaponRule;
            }

            for (const auto& [Keyword, KeywordRule] : Keywords)
            {
                if (Base->HasKeyword(Keyword, Instance))
                    return KeywordRule;
            }

            return std::nullopt;
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

    std::array<RuleList<SightedRule>, static_cast<std::size_t>(Feature::Count)> ExclusionRules;

    [[nodiscard]] bool IsExcluded(Feature TargetFeature, RE::TESObjectWEAP* Base, const RE::TESObjectWEAP::InstanceData* Instance)
    {
        return ExclusionRules[static_cast<std::size_t>(TargetFeature)].Match(Base, Instance).has_value();
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
        const SprintRule Rule{ .Start = Options.Start.value_or(true), .Stop = Options.Stop.value_or(true), .Equip = Options.Equip.value_or(false) };

        if (!Rule.Start && !Rule.Stop && !Rule.Equip)
        {
            REX::WARN("  {}: \"start\", \"stop\" and \"equip\" all false, it does nothing", Where);
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

    [[nodiscard]] std::optional<SlideLockRule> MakeRule(const SlideLockOptions& Options, std::string_view Where)
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

        return SlideLockRule{ .Bones = std::move(*Bones),
            .DisableInEmptyReloads = Options.DisableInEmptyReloads.value_or(false),
            .BlendIn = Options.BlendIn.value_or(0.0f),
            .BlendOut = Options.BlendOut.value_or(0.0f) };
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
        return std::format(", start {}, stop {}, equip {}", Rule.Start ? "on" : "off", Rule.Stop ? "on" : "off", Rule.Equip ? "on" : "off");
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

    [[nodiscard]] std::string Describe(const SlideLockRule& Rule)
    {
        return std::format("{}{}, blend in {:.3f}s, out {:.3f}s", DescribeBones(Rule.Bones, "bones"sv),
            Rule.DisableInEmptyReloads ? ", off in empty reloads" : "", Rule.BlendIn, Rule.BlendOut);
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
    void AddRule(std::vector<std::pair<const T*, Rule>>& List, const T* Form, const Rule& NewRule, std::string_view Kind, std::string_view Where)
    {
        const auto Known = std::ranges::find(List, Form, &std::pair<const T*, Rule>::first);

        if (Known != List.end())
        {
            REX::INFO("  {}: {} {:08X} already listed, ignored here", Where, Kind, Form->GetFormID());
            return;
        }

        List.emplace_back(Form, NewRule);
        REX::INFO("  {}: {} {:08X}{}", Where, Kind, Form->GetFormID(), Describe(NewRule));
    }

    template <class T, class Rule>
    void AddRule(RuleList<Rule>& List, const T* Form, const Rule& NewRule, std::string_view Where)
    {
        if constexpr (std::is_same_v<T, RE::BGSKeyword>)
            AddRule(List.Keywords, Form, NewRule, "keyword", Where);
        else
            AddRule(List.Weapons, Form, NewRule, "weapon", Where);
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

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(SlideLockRules, Form, *Rule, FeatureWhere);
        }

        if (const auto Options = Resolve(Entry.FireModeSwitch))
        {
            const auto FeatureWhere = std::format("{} fireModeSwitch", Where);

            if (const auto Rule = MakeRule(*Options, FeatureWhere))
                AddRule(FireModeRules, Form, *Rule, FeatureWhere);
        }
    }

    void AddEntry(const WeaponEntry& Entry, std::string_view Where)
    {
        if (Entry.Keyword.has_value() == Entry.Weapon.has_value())
        {
            REX::WARN("  {}: needs either \"keyword\" or \"weapon\"", Where);
            return;
        }

        const bool TurnsAnyFeatureOn = Resolve(Entry.LoopedReload) || Resolve(Entry.SprintTransitions) || Entry.SightedReload.value_or(false) ||
                                       Resolve(Entry.TacticalReload) || Resolve(Entry.Inspect) || Resolve(Entry.AmmoCheck) ||
                                       Entry.SpecialAttack.value_or(false) || Resolve(Entry.SlideLock) || Resolve(Entry.FireModeSwitch);

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

        if (Entry.Keyword)
        {
            if (const auto Keyword = FindKeyword(Entry, Where))
                AddFeatures(Entry, Keyword, Where);
        }
        else if (const auto Weapon = FindWeapon(Entry, Where))
        {
            AddFeatures(Entry, Weapon, Where);
        }
    }

    void AddExclusion(const ExclusionEntry& Entry, std::string_view Where)
    {
        if (Entry.Keyword.has_value() == Entry.Weapon.has_value())
        {
            REX::WARN("  {}: needs either \"keyword\" or \"weapon\"", Where);
            return;
        }

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

        if (Entry.Keyword)
        {
            if (const auto Keyword = FindKeyword(Entry, Where))
                Add(Keyword);
        }
        else if (const auto Weapon = FindWeapon(Entry, Where))
        {
            Add(Weapon);
        }
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
            ExclusionCount += List.Keywords.size() + List.Weapons.size();

        REX::INFO("Exclusions: {} from {} file(s)", ExclusionCount, ExclusionFiles.size());

        REX::INFO("Looped reload: {} keyword(s) and {} weapon(s); sprint transitions: {} keyword(s) and {} weapon(s); "
                  "sighted reload: {} keyword(s) and {} weapon(s); tactical reload: {} keyword(s) and {} weapon(s); inspect / ammo check "
                  "/ special attack: {} / {} / {} entries; slide lock: {} keyword(s) and {} weapon(s); fire mode switch: {} keyword(s) and {} "
                  "weapon(s); from {} file(s)",
            LoopRules.Keywords.size(), LoopRules.Weapons.size(), SprintRules.Keywords.size(), SprintRules.Weapons.size(),
            SightedRules.Keywords.size(), SightedRules.Weapons.size(), TacticalRules.Keywords.size(), TacticalRules.Weapons.size(),
            InspectRules.Keywords.size() + InspectRules.Weapons.size(), AmmoCheckRules.Keywords.size() + AmmoCheckRules.Weapons.size(),
            SpecialAttackRules.Keywords.size() + SpecialAttackRules.Weapons.size(), SlideLockRules.Keywords.size(), SlideLockRules.Weapons.size(),
            FireModeRules.Keywords.size(), FireModeRules.Weapons.size(), Files.size());
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

        for (const auto& [Weapon, WeaponRule] : TacticalRules.Weapons)
        {
            if (Weapon == Base)
                return WeaponRule;
        }

        std::optional<TacticalRule> MergedFromKeywords;

        for (const auto& [Keyword, KeywordRule] : TacticalRules.Keywords)
        {
            if (Base->HasKeyword(Keyword, Instance))
            {
                MergedFromKeywords = MergedFromKeywords ? TacticalRule{ .FirstPerson = MergedFromKeywords->FirstPerson || KeywordRule.FirstPerson,
                                                              .ThirdPerson = MergedFromKeywords->ThirdPerson || KeywordRule.ThirdPerson } :
                                                          KeywordRule;
            }
        }

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
}
