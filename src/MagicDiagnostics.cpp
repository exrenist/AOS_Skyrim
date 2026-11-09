#include "MagicDiagnostics.h"

namespace Spellmaking
{
    void MagicDiagnostics::Register()
    {
        auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!eventHolder) {
            SKSE::log::error("MagicDiagnostics: ScriptEventSourceHolder is null. Failed to register diagnostics.");
            return;
        }

        eventHolder->AddEventSink<RE::TESSpellCastEvent>(GetSingleton());
        eventHolder->AddEventSink<RE::TESHitEvent>(GetSingleton());
        SKSE::log::info("MagicDiagnostics: Successfully registered live spell cast and hit event sinks.");
    }

    RE::BSEventNotifyControl MagicDiagnostics::ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // In CommonLibSSE-NG, the casting actor reference is stored in 'object', not 'caster'
        auto* caster = a_event->object ? a_event->object->As<RE::Actor>() : nullptr;
        if (!caster || !caster->IsPlayerRef()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell);
        if (spell) {
            SKSE::log::info("[DIAGNOSTIC - CAST] Player initiated spell '{}' (FormID: 0x{:08X}) | Cost Override: {}",
                spell->GetFullName(), spell->GetFormID(), spell->data.costOverride);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl MagicDiagnostics::ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* aggressor = a_event->cause ? a_event->cause->As<RE::Actor>() : nullptr;
        if (!aggressor || !aggressor->IsPlayerRef()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* target = a_event->target ? a_event->target->As<RE::Actor>() : nullptr;
        auto* sourceForm = RE::TESForm::LookupByID(a_event->source);

        if (!sourceForm) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* spell = sourceForm->As<RE::SpellItem>();
        if (!spell) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // TESObjectCELL::IsInteriorCell() evaluates whether the target is in an interior cell
        bool isInterior = false;
        if (target) {
            auto* parentCell = target->GetParentCell();
            isInterior = parentCell ? parentCell->IsInteriorCell() : false;
        }

        std::string keywordDump;
        auto* kwForm = static_cast<RE::BGSKeywordForm*>(static_cast<RE::MagicItem*>(spell));
        if (kwForm && kwForm->keywords) {
            for (std::uint32_t i = 0; i < kwForm->numKeywords; ++i) {
                if (auto* kw = kwForm->keywords[i]) {
                    keywordDump += kw->GetFormEditorID();
                    keywordDump += " ";
                }
            }
        }

        SKSE::log::info("[DIAGNOSTIC - HIT] Spell '{}' hit target '{}' (FormID: 0x{:08X}) | Interior: {} | Keywords: [{}]",
            spell->GetFullName(),
            target ? target->GetName() : "Unknown",
            target ? target->GetFormID() : 0,
            isInterior ? "YES (Blocks Lightning!)" : "NO (Sky Clear)",
            keywordDump.empty() ? "NONE" : keywordDump);

        return RE::BSEventNotifyControl::kContinue;
    }
}