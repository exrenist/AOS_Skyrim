#include "MagicDiagnostics.h"
#include "CompatibilityManager.h"
#include <unordered_set>
#include <chrono>

namespace Spellmaking
{
    namespace
    {
        // Prevents engine dual-event spam within the same animation frame (150ms window)
        static inline RE::FormID s_lastCastSpellID = 0;
        static inline std::chrono::steady_clock::time_point s_lastCastTimestamp{};
    }

    void MagicDiagnostics::Register()
    {
        auto* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!eventHolder) {
            SKSE::log::error("[HitInterceptor] ScriptEventSourceHolder is null. Failed to register.");
            return;
        }

        eventHolder->AddEventSink<RE::TESSpellCastEvent>(GetSingleton());
        eventHolder->AddEventSink<RE::TESHitEvent>(GetSingleton());
        SKSE::log::info("[HitInterceptor] Successfully hooked into live spell cast and hit event pipelines.");
    }

    RE::BSEventNotifyControl MagicDiagnostics::ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>*)
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* caster = a_event->object ? a_event->object->As<RE::Actor>() : nullptr;
        if (!caster || !caster->IsPlayerRef()) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_event->spell);
        if (!spell) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto castFormID = spell->GetFormID();

        // Only process player-crafted dynamic spells (0xFFxxxxxx)
        if ((castFormID >> 24) == 0xFF) {
            // Frame-level anti-stacking guard: block duplicate TESSpellCastEvent dispatches within 150ms
            const auto now = std::chrono::steady_clock::now();
            if (castFormID == s_lastCastSpellID &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - s_lastCastTimestamp).count() < 150) {
                return RE::BSEventNotifyControl::kContinue;
            }
            s_lastCastSpellID = castFormID;
            s_lastCastTimestamp = now;

            SKSE::log::info("[CAST] Player cast spell '{}' (FormID: 0x{:08X}) | Cost: {}",
                spell->GetFullName(), castFormID, spell->data.costOverride);

            auto* compatMgr = CompatibilityManager::GetSingleton();
            if (!compatMgr) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // Retrieve ONLY the exact authentic parent spells that the player explicitly combined into this spell
            // Index 0 in authenticSources is ALWAYS the primary visual projectile leader already fired by the engine!
            const auto authenticSources = compatMgr->GetCraftedSpellSources(castFormID);
            if (authenticSources.empty()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            for (std::size_t i = 0; i < authenticSources.size(); ++i) {
                const auto sourceSpellID = authenticSources[i];
                auto* origSpell = RE::TESForm::LookupByID<RE::SpellItem>(sourceSpellID);
                if (!origSpell) continue;

                // Check if this parent spell has a conflicting delivery companion (like Lightning Strike's Location Disintegrate)
                // that had to be delegated out of the composite SpellItem
                bool hasDelegatedCompanion = false;
                for (auto* origEff : origSpell->effects) {
                    if (origEff && origEff->baseEffect && origEff->baseEffect->data.delivery != spell->data.delivery) {
                        hasDelegatedCompanion = true;
                        break;
                    }
                }

                // Anti-Stacking Rule:
                // - If i == 0 (Primary Visual Leader, e.g., Bolide in "Bolide + Firebolt"), the dynamic spell ITSELF
                //   already fires Bolide's projectile and all of Bolide's distance companions! Do NOT fire it a second time
                //   unless it had a delegated ground-location companion (like Lightning Strike).
                // - If i > 0 (Secondary combined spells, e.g., Firebolt or Lightning Strike), co-launch it!
                if (i > 0 || hasDelegatedCompanion) {
                    if (auto* magicCaster = caster->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant)) {
                        magicCaster->CastSpellImmediate(origSpell, false, nullptr, 1.0f, false, 0.0f, caster);
                        SKSE::log::info("[CAST-PROC] Co-launched secondary/delegated spell '{}' (0x{:08X}) along player aim!",
                            origSpell->GetFullName(), origSpell->GetFormID());
                    }
                }
            }
        } else {
            SKSE::log::info("[CAST] Player cast spell '{}' (FormID: 0x{:08X}) | Cost: {}",
                spell->GetFullName(), castFormID, spell->data.costOverride);
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

        auto* sourceForm = RE::TESForm::LookupByID(a_event->source);
        if (!sourceForm) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* spell = sourceForm->As<RE::SpellItem>();
        if (!spell) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto hitFormID = spell->GetFormID();

        if ((hitFormID >> 24) == 0xFF) {
            auto* compatMgr = CompatibilityManager::GetSingleton();
            if (!compatMgr) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto authenticSources = compatMgr->GetCraftedSpellSources(hitFormID);
            if (!authenticSources.empty()) {
                auto* mutableHitEvent = const_cast<RE::TESHitEvent*>(a_event);
                mutableHitEvent->source = authenticSources.front();
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
}