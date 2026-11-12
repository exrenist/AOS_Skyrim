#pragma once

#include "pch.h"
#include "SpellFactory.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Spellmaking
{
    // Holds the runtime blueprint and persistent equip/favorite state of a crafted custom spell
    struct CustomSpellRecord
    {
        RE::FormID formID{ 0 };
        std::string spellName;
        std::string schoolName{ "Destruction" };
        std::string castTypeStr{ "FireAndForget" };
        std::string targetTypeStr{ "Aimed" };
        uint32_t totalMagickaCost{ 0 };
        uint32_t totalGoldCost{ 0 };

        // v4 Persistent UI & Hand Equip states across save/load cycles
        bool isFavorited{ false };
        int32_t hotkeySlot{ -1 };
        bool equippedLeft{ false };
        bool equippedRight{ false };

        struct EffectRecord
        {
            RE::FormID effectSettingFormID{ 0 };
            std::string effectName;
            float magnitude{ 0.0f };
            uint32_t duration{ 0 };
            uint32_t areaFeet{ 0 };
        };

        std::vector<EffectRecord> effects;
    };

    class Serialization
    {
    public:
        // Unique 4-character SKSE Co-Save record type identifier
        static constexpr uint32_t kSpellRecordType = 'SPMK';
        static constexpr uint32_t kCurrentSaveVersion = 4;

        // Registers SKSE serialization interface listeners
        static void Register();

        // Lifecycle hook called on kPostLoadGame to reconstruct custom spells in memory without duplicates
        static void ApplyPendingSpells();

        // Lifecycle hook called on kNewGame to reset all custom spell trackers
        static void OnNewGame();

        // Adds a newly forged spell to memory tracking
        static void TrackCustomSpell(const CustomSpellRecord& a_record);

        // Overload bridging SpellCreationParams for PapyrusInterface legacy calls
        static void TrackCreatedSpell(const SpellCreationParams& a_params, RE::SpellItem* a_spell = nullptr);

        // Deconstructs and purges a custom spell from runtime memory and co-save
        static bool DeleteCustomSpell(RE::FormID a_formID);

        // Serializes all currently active player-crafted spells into a JSON array for PrismaUI
        static std::string GetActiveSpellsJson();

        // SKSE Co-Save serialization lifecycle callbacks
        static void OnSave(SKSE::SerializationInterface* a_intfc);
        static void OnLoad(SKSE::SerializationInterface* a_intfc);
        static void OnRevert(SKSE::SerializationInterface* a_intfc);

    private:
        // Strips any ghost spells loaded into the player by the engine's .ess save file before instantiating
        static void PurgePlayerCustomSpells();

        // Active custom spells bound to the current playthrough session
        static inline std::vector<CustomSpellRecord> s_activeCustomSpells;

        // Historical FormID graveyard to prevent form duplication or ghosting across reload cycles
        static inline std::vector<RE::FormID> s_allHistoricalFormIDs;
    };
}