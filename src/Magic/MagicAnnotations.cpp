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

#include "MagicAnnotations.h"
#include "Hooks/ClipGeneratorHook.h"
#include "Settings/Settings.h"

#include <unordered_map>

namespace
{
    constexpr std::size_t SetFormEditorIDIndex = 0x3B;

    [[nodiscard]] std::string_view VtableClassName(std::uintptr_t Vtable)
    {
        struct CompleteObjectLocator
        {
            std::uint32_t Signature;
            std::uint32_t Offset;
            std::uint32_t CdOffset;
            std::uint32_t TypeDescriptor;
        };

        const auto Locator = *reinterpret_cast<const CompleteObjectLocator* const*>(Vtable - sizeof(void*));

        if (!Locator)
            return {};

        const auto Base = REX::FModule::GetExecutingModule().GetBaseAddress();

        return reinterpret_cast<const char*>(Base + Locator->TypeDescriptor + 2 * sizeof(void*));
    }

    [[nodiscard]] std::string Lowercase(std::string_view Text)
    {
        std::string Result{ Text };
        std::ranges::transform(Result, Result.begin(), [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

        return Result;
    }

    [[nodiscard]] bool IEquals(std::string_view Lhs, std::string_view Rhs)
    {
        return std::ranges::equal(Lhs, Rhs, [](unsigned char L, unsigned char R) { return std::tolower(L) == std::tolower(R); });
    }

    struct Annotation
    {
        std::string_view Tag;
        std::string_view FormType;
        std::string_view VtableClass;

        std::mutex Lock;
        std::unordered_map<std::string, RE::MagicItem*> ByEditorID;
        std::unordered_set<std::string> ReportedMissing;
        bool Hooked{ false };

        [[nodiscard]] bool Active() const { return Hooked; }

        void Remember(RE::MagicItem* Form, const char* EditorID)
        {
            if (Form && EditorID && *EditorID)
            {
                const std::scoped_lock Guard{ Lock };
                ByEditorID[Lowercase(EditorID)] = Form;
            }
        }

        [[nodiscard]] RE::MagicItem* Find(std::string_view EditorID)
        {
            const auto Key = Lowercase(EditorID);
            const std::scoped_lock Guard{ Lock };

            if (const auto Found = ByEditorID.find(Key); Found != ByEditorID.end())
                return Found->second;

            if (ReportedMissing.insert(Key).second)
                REX::WARN("{}: no {} with editor ID '{}'", Tag, FormType, EditorID);

            return nullptr;
        }

        [[nodiscard]] std::optional<std::string_view> EditorIDIn(std::string_view Text) const
        {
            if (Text.size() > Tag.size() + 1 && Text[Tag.size()] == '.' && IEquals(Text.substr(0, Tag.size()), Tag))
                return Text.substr(Tag.size() + 1);

            return std::nullopt;
        }
    };

    Annotation UseAlchemyItemAnnotation{ "MWB_UseAlchemyItem", "ALCH", ".?AVAlchemyItem@@" };
    Annotation CastSpellAnnotation{ "MWB_CastSpell", "SPEL", ".?AVSpellItem@@" };

    constexpr std::array Annotations{ &UseAlchemyItemAnnotation, &CastSpellAnnotation };

    template <Annotation* A>
    struct SetFormEditorID
    {
        static bool Thunk(RE::MagicItem* This, const char* EditorID)
        {
            A->Remember(This, EditorID);

            return Original(This, EditorID);
        }

        inline static REL::Relocation<decltype(Thunk)> Original;
    };

    template <Annotation* A>
    void InstallEditorIDHook(std::uintptr_t Vtable)
    {
        const auto Name = VtableClassName(Vtable);

        if (Name != A->VtableClass)
        {
            REX::ERROR("{} vtable id points at '{}'. {} disabled.", A->FormType, Name, A->Tag);
            return;
        }

        SetFormEditorID<A>::Original = REL::Relocation<std::uintptr_t>{ Vtable }.write_vfunc(SetFormEditorIDIndex, SetFormEditorID<A>::Thunk);
        A->Hooked = true;
    }

    [[nodiscard]] RE::TESForm* EngineStimpak()
    {
        static RE::TESForm* const Stimpak = []
        {
            const auto Objects = RE::BGSDefaultObject::GetSingleton();

            if (Objects)
            {
                for (const auto Object : *Objects)
                {
                    if (Object && Object->formEditorID == "StimpackObject_DO"sv)
                        return Object->form;
                }
            }

            REX::WARN("MWB_UseAlchemyItem: StimpackObject_DO not found");

            return static_cast<RE::TESForm*>(nullptr);
        }();

        return Stimpak;
    }

    void UseItem(RE::Actor* AnimatedActor, RE::AlchemyItem* Item)
    {
        const auto Player = RE::PlayerCharacter::GetSingleton();
        const auto Pipboy = AnimatedActor == Player ? RE::PipboyManager::GetSingleton() : nullptr;

        const bool FromPipboy = Pipboy && Pipboy->itemAnimOnClose == Item;

        if (FromPipboy)
        {
            Pipboy->itemAnimOnClose = nullptr;

            if (Item == EngineStimpak())
            {
                if (Settings::VerboseLog())
                    REX::INFO("MWB_UseAlchemyItem: {:08X} already used from the Pip-Boy", Item->GetFormID());

                return;
            }
        }

        if (AnimatedActor->GetInventoryObjectCount(Item) == 0)
        {
            if (Settings::VerboseLog())
                REX::INFO("MWB_UseAlchemyItem: {:08X} has no {:08X}", AnimatedActor->GetFormID(), Item->GetFormID());

            return;
        }

        const RE::BGSObjectInstance Instance{ Item, nullptr };
        RE::ActorEquipManager::GetSingleton()->EquipObject(AnimatedActor, Instance, 0, 1, nullptr, false, true, true, false, false);

        if (Settings::VerboseLog())
            REX::INFO("MWB_UseAlchemyItem: {:08X} used {:08X}{}", AnimatedActor->GetFormID(), Item->GetFormID(), FromPipboy ? " (from the Pip-Boy)" : "");
    }

    void CastSpell(RE::Actor* AnimatedActor, RE::SpellItem* Spell)
    {
        constexpr std::uint32_t PermanentTypes = (1u << 1) | (1u << 4) | (1u << 10);
        const auto SpellType = static_cast<std::uint32_t>(Spell->GetSpellType());

        if (SpellType < 11 && (PermanentTypes >> SpellType & 1) != 0)
        {
            REX::WARN("MWB_CastSpell: {:08X} is a permanent spell (ability, addiction or disease) and cannot be cast", Spell->GetFormID());
            return;
        }

        constexpr auto Instant = static_cast<RE::MagicSystem::CastingSource>(3);
        const auto Caster = AnimatedActor->GetMagicCaster(Instant);

        if (!Caster)
        {
            if (Settings::VerboseLog())
                REX::INFO("MWB_CastSpell: {:08X} has no instant caster", AnimatedActor->GetFormID());

            return;
        }

        using InterruptCastSignature = void(RE::MagicCaster*, bool);
        static REL::Relocation<InterruptCastSignature*> InterruptCast{ REL::ID{ 788485, 2226294 } };
        InterruptCast(Caster, false);

        constexpr std::size_t CastSpellImmediateSlot = 2;
        using CastSpellImmediateSignature = void(RE::MagicCaster*, RE::MagicItem*, bool, RE::TESObjectREFR*, float, bool, float, RE::Actor*);
        const auto Vtable = *reinterpret_cast<CastSpellImmediateSignature* const* const*>(Caster);
        Vtable[CastSpellImmediateSlot](Caster, Spell, false, nullptr, 1.0f, false, 0.0f, nullptr);

        if (Settings::VerboseLog())
            REX::INFO("MWB_CastSpell: {:08X} cast {:08X}", AnimatedActor->GetFormID(), Spell->GetFormID());
    }

    void Apply(RE::Actor* AnimatedActor, const Annotation* Matched, RE::MagicItem* Item)
    {
        if (Matched == &UseAlchemyItemAnnotation)
            UseItem(AnimatedActor, static_cast<RE::AlchemyItem*>(Item));
        else
            CastSpell(AnimatedActor, static_cast<RE::SpellItem*>(Item));
    }
}

namespace
{
    struct PendingUse
    {
        float Time;
        const Annotation* Source;
        RE::MagicItem* Item;
        bool Heard;
    };

    struct WatchedClip
    {
        const void* Clip;
        RE::Actor* Actor;
        std::vector<PendingUse> Uses;
        bool Ended;
        float EndTime;
        std::uint32_t UpdatesSinceEnd;
    };

    constexpr std::uint32_t UpdatesBeforeFallback = 2;

    constexpr std::size_t MaxWatched = 64;
    std::mutex ClipLock;
    std::vector<WatchedClip> Watched;
    std::atomic<std::size_t> WatchedCount{ 0 };

    [[nodiscard]] bool AnyActive()
    {
        return std::ranges::any_of(Annotations, [](const Annotation* Candidate) { return Candidate->Active(); });
    }

    void CollectUse(void* Context, float Time, const char* Text)
    {
        if ((Text[0] | 0x20) != 'm' || (Text[1] | 0x20) != 'w' || (Text[2] | 0x20) != 'b')
            return;

        for (const auto Candidate : Annotations)
        {
            if (!Candidate->Active())
                continue;

            if (const auto EditorID = Candidate->EditorIDIn(Text))
            {
                if (const auto Item = Candidate->Find(*EditorID))
                    static_cast<std::vector<PendingUse>*>(Context)->push_back({ Time, Candidate, Item, false });

                return;
            }
        }
    }

    void Unwatch(const void* Clip)
    {
        std::erase_if(Watched, [&](const WatchedClip& Entry) { return Entry.Clip == Clip; });
        WatchedCount = Watched.size();
    }
}

namespace MagicAnnotations
{
    void InstallEditorIDHooks()
    {
        InstallEditorIDHook<&UseAlchemyItemAnnotation>(REL::Relocation<std::uintptr_t>{ RE::VTABLE::AlchemyItem[0] }.address());
        InstallEditorIDHook<&CastSpellAnnotation>(REL::Relocation<std::uintptr_t>{ RE::VTABLE::SpellItem[0] }.address());
    }

    void OnAnimationEvent(RE::Actor* AnimatedActor, const RE::BSAnimationGraphEvent& Event)
    {
        if (!AnimatedActor)
            return;

        const std::string_view Tag = Event.tag;
        Annotation* Matched = nullptr;
        std::string_view EditorID;

        for (const auto Candidate : Annotations)
        {
            if (!Candidate->Active())
                continue;

            if (IEquals(Tag, Candidate->Tag))
            {
                Matched = Candidate;
                EditorID = Event.payload;
                break;
            }

            if (const auto InTag = Candidate->EditorIDIn(Tag))
            {
                Matched = Candidate;
                EditorID = *InTag;
                break;
            }
        }

        if (!Matched)
            return;

        if (Settings::VerboseLog())
            REX::INFO("{}: event from {:08X} (tag '{}', payload '{}')", Matched->Tag, AnimatedActor->GetFormID(), Tag, std::string_view{ Event.payload });

        if (EditorID.empty())
        {
            REX::WARN("{} without an editor ID", Matched->Tag);
            return;
        }

        const auto Item = Matched->Find(EditorID);

        if (!Item)
            return;

        if (WatchedCount.load(std::memory_order_relaxed) > 0)
        {
            const std::scoped_lock Guard{ ClipLock };
            [&]
            {
                for (auto& Entry : Watched)
                {
                    if (Entry.Actor != AnimatedActor)
                        continue;

                    for (auto& Use : Entry.Uses)
                    {
                        if (Use.Item == Item && !Use.Heard)
                        {
                            Use.Heard = true;
                            return;
                        }
                    }
                }
            }();
        }

        Apply(AnimatedActor, Matched, Item);
    }

    void OnClipActivated(const void* Clip, const void* Character)
    {
        if (!Clip || !AnyActive())
            return;

        if (WatchedCount.load(std::memory_order_relaxed) > 0)
        {
            const std::scoped_lock Guard{ ClipLock };
            Unwatch(Clip);
        }

        std::vector<PendingUse> Uses;
        Hooks::VisitClipAnnotations(Clip, CollectUse, &Uses);

        if (Uses.empty())
            return;

        const auto Owner = Hooks::ActorOfCharacter(Character);

        if (!Owner)
            return;

        const std::scoped_lock Guard{ ClipLock };

        if (Watched.size() >= MaxWatched)
            Watched.erase(Watched.begin());

        Watched.push_back({ Clip, Owner, std::move(Uses), false, 0.0f, 0 });
        WatchedCount = Watched.size();
    }

    void OnClipDeactivated(const void* Clip)
    {
        if (WatchedCount.load(std::memory_order_relaxed) == 0)
            return;

        const std::scoped_lock Guard{ ClipLock };

        for (auto& Entry : Watched)
        {
            if (Entry.Clip == Clip && !Entry.Ended)
            {
                Entry.Ended = true;
                Entry.EndTime = Hooks::ClipLocalTime(Clip);
            }
        }
    }

    void OnUpdateAnimation(RE::Actor* AnimatedActor)
    {
        if (WatchedCount.load(std::memory_order_relaxed) == 0 || !AnimatedActor)
            return;

        std::vector<PendingUse> ToApply;

        {
            const std::scoped_lock Guard{ ClipLock };

            for (auto& Entry : Watched)
            {
                if (Entry.Actor != AnimatedActor || !Entry.Ended || ++Entry.UpdatesSinceEnd < UpdatesBeforeFallback)
                    continue;

                for (const auto& Use : Entry.Uses)
                {
                    if (!Use.Heard && Use.Time <= Entry.EndTime + 0.0001f)
                        ToApply.push_back(Use);
                }
            }

            std::erase_if(Watched, [&](const WatchedClip& Finished)
            {
                return Finished.Actor == AnimatedActor && Finished.Ended && Finished.UpdatesSinceEnd >= UpdatesBeforeFallback;
            });
            WatchedCount = Watched.size();
        }

        for (const auto& Use : ToApply)
        {
            if (Settings::VerboseLog())
            {
                REX::INFO("{}: {:08X} annotation not heard (the behavior does not declare it); fallback at the end of the clip",
                    Use.Source->Tag, AnimatedActor->GetFormID());
            }

            Apply(AnimatedActor, Use.Source, Use.Item);
        }
    }

    void Reset()
    {
        const std::scoped_lock Guard{ ClipLock };
        Watched.clear();
        WatchedCount = 0;
    }
}
