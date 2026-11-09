#pragma once

#include "pch.h"

namespace Spellmaking
{
    // Real-time diagnostics event listener to catch spell impact, target interior state, and active keywords
    class MagicDiagnostics : 
        public RE::BSTEventSink<RE::TESHitEvent>,
        public RE::BSTEventSink<RE::TESSpellCastEvent>
    {
    public:
        [[nodiscard]] static MagicDiagnostics* GetSingleton()
        {
            static MagicDiagnostics singleton;
            return &singleton;
        }

        // Registers event sinks with the engine's ScriptEventSourceHolder
        static void Register();

    protected:
        // Intercepts spell cast start events
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>* a_eventSource) override;

        // Intercepts physical and magic impact events
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

    private:
        MagicDiagnostics() = default;
        ~MagicDiagnostics() override = default;
        MagicDiagnostics(const MagicDiagnostics&) = delete;
        MagicDiagnostics& operator=(const MagicDiagnostics&) = delete;
    };
}