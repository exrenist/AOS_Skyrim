#pragma once

#include "pch.h"
#include "SpellFactory.h"
#include <vector>

namespace Spellmaking
{
    // Encapsulates a persisted spell recipe alongside its runtime dynamic FormID
    struct TrackedSpellEntry
    {
        RE::FormID spellFormID{ 0 };
        SpellCreationParams params;
    };

    class Serialization
    {
    public:
        // Registers custom record types with the SKSE serialization interface
        static void Register();

        // Tracks a dynamically forged spell and registers its FormID to historical purge cache
        static void TrackCreatedSpell(const SpellCreationParams& a_params, RE::SpellItem* a_spell = nullptr);

        // Instantiates clean spells and purges legacy duplicates after .ess loads (kPostLoadGame)
        static void ApplyPendingSpells();

        // Cleans up spell allocations when starting a brand new playthrough (kNewGame)
        static void OnNewGame();

        // Serialization callbacks
        static void OnSave(SKSE::SerializationInterface* a_serde);
        static void OnLoad(SKSE::SerializationInterface* a_serde);
        static void OnRevert(SKSE::SerializationInterface* a_serde);

    private:
        // Purges all historical and loaded dynamic forms from the player character
        static void PurgePlayerCustomSpells();

        static constexpr std::uint32_t kSerializationVersion = 3; // Version 3 includes runtime FormID tracking
        static constexpr std::uint32_t kSpellRecordType = 'SPEU';

        // Recipes loaded from co-save waiting for post-load engine synchronization
        static inline std::vector<TrackedSpellEntry> s_pendingSpells;

        // Current active recipes persisted across saves
        static inline std::vector<TrackedSpellEntry> s_trackedSpells;

        // Cumulative registry of all dynamic FormIDs ever forged in this session to prevent reload multiplication
        static inline std::vector<RE::FormID> s_allHistoricalFormIDs;
    };
}