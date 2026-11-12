#pragma once

#include "pch.h"

namespace Spellmaking
{
    class MagicDiagnostics :
        public RE::BSTEventSink<RE::TESSpellCastEvent>,
        public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        [[nodiscard]] static MagicDiagnostics* GetSingleton()
        {
            static MagicDiagnostics singleton;
            return &singleton;
        }

        // Registers event sinks into engine's ScriptEventSourceHolder
        static void Register();

        // Event callbacks
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSpellCastEvent* a_event, RE::BSTEventSource<RE::TESSpellCastEvent>* a_eventSource) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

    private:
        MagicDiagnostics() = default;
        ~MagicDiagnostics() override = default;

        MagicDiagnostics(const MagicDiagnostics&) = delete;
        MagicDiagnostics& operator=(const MagicDiagnostics&) = delete;
    };
}