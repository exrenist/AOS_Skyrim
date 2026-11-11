#include "Serialization.h"
#include "SpellFactory.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Spellmaking
{
    namespace
    {
        // Safely purges a spell pointer from Skyrim's MagicFavorites singleton (both spells and 1-8 hotkeys)
        void RemoveFromMagicFavorites(RE::TESForm* a_form)
        {
            if (!a_form) {
                return;
            }

            auto* magicFavs = RE::MagicFavorites::GetSingleton();
            if (!magicFavs) {
                return;
            }

            // 1. Remove from favorites list
            auto it = std::find(magicFavs->spells.begin(), magicFavs->spells.end(), a_form);
            if (it != magicFavs->spells.end()) {
                magicFavs->spells.erase(it);
            }

            // 2. Remove from 1-8 hotkeys array
            for (auto& hotkeyForm : magicFavs->hotkeys) {
                if (hotkeyForm == a_form) {
                    hotkeyForm = nullptr;
                }
            }
        }

        // Scrubs any dangling or dead dynamic FormIDs (0xFFxxxxxx) from MagicFavorites
        void ScrubDanglingFavorites(
            const std::vector<RE::FormID>& a_historicalIDs,
            const std::vector<CustomSpellRecord>& a_activeRecords,
            std::unordered_set<RE::FormID>& a_outFavoritedOldIDs,
            std::unordered_map<RE::FormID, std::size_t>& a_outHotkeyOldIDs)
        {
            auto* magicFavs = RE::MagicFavorites::GetSingleton();
            if (!magicFavs) {
                return;
            }

            auto isTrackedDynamicID = [&](RE::FormID a_id) -> bool {
                if (a_id < 0xFF000000) {
                    return false;
                }
                if (std::find(a_historicalIDs.begin(), a_historicalIDs.end(), a_id) != a_historicalIDs.end()) {
                    return true;
                }
                for (const auto& rec : a_activeRecords) {
                    if (rec.formID == a_id) {
                        return true;
                    }
                }
                return false;
            };

            // Check hotkeys (1-8) before scrubbing so we can restore them to the newly instantiated spell
            for (std::size_t i = 0; i < magicFavs->hotkeys.size(); ++i) {
                auto* form = magicFavs->hotkeys[i];
                if (form && isTrackedDynamicID(form->GetFormID())) {
                    a_outHotkeyOldIDs[form->GetFormID()] = i;
                    magicFavs->hotkeys[i] = nullptr;
                }
            }

            // Scrub favorites list and record which active spells were favorited
            for (auto it = magicFavs->spells.begin(); it != magicFavs->spells.end();) {
                auto* form = *it;
                if (!form) {
                    it = magicFavs->spells.erase(it);
                    continue;
                }

                const auto formID = form->GetFormID();
                if (isTrackedDynamicID(formID)) {
                    a_outFavoritedOldIDs.insert(formID);
                    it = magicFavs->spells.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    void Serialization::Register()
    {
        auto* serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            SKSE::log::error("[Serialization] Failed to acquire SKSE SerializationInterface!");
            return;
        }

        serialization->SetUniqueID(kSpellRecordType);
        serialization->SetSaveCallback(OnSave);
        serialization->SetLoadCallback(OnLoad);
        serialization->SetRevertCallback(OnRevert);

        SKSE::log::info("[Serialization] SKSE Co-Save v3 interface successfully registered.");
    }

    void Serialization::PurgePlayerCustomSpells()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        std::size_t removedCount = 0;

        // 1. Purge all historical dynamic FormIDs registered during the game session
        for (const auto formID : s_allHistoricalFormIDs) {
            if (formID >= 0xFF000000) {
                if (auto* oldSpell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
                    RemoveFromMagicFavorites(oldSpell);
                    if (player->HasSpell(oldSpell)) {
                        player->RemoveSpell(oldSpell);
                        removedCount++;
                    }
                }
            }
        }

        // 2. Also purge any FormIDs that were recorded in the active custom spells
        for (const auto& spell : s_activeCustomSpells) {
            if (spell.formID >= 0xFF000000) {
                if (auto* oldSpell = RE::TESForm::LookupByID<RE::SpellItem>(spell.formID)) {
                    RemoveFromMagicFavorites(oldSpell);
                    if (player->HasSpell(oldSpell)) {
                        player->RemoveSpell(oldSpell);
                        removedCount++;
                    }
                }
            }
        }

        // 3. Scan runtime inventory to ensure no lingering ghosts remain in player's spellbook
        const auto& actorSpells = player->GetActorRuntimeData().addedSpells;
        std::vector<RE::SpellItem*> spellsToStrip;
        for (auto* sp : actorSpells) {
            if (sp && sp->GetFormID() >= 0xFF000000) {
                bool isOurSpell = false;
                for (const auto histID : s_allHistoricalFormIDs) {
                    if (sp->GetFormID() == histID) {
                        isOurSpell = true;
                        break;
                    }
                }
                if (!isOurSpell) {
                    for (const auto& active : s_activeCustomSpells) {
                        if (sp->GetFormID() == active.formID) {
                            isOurSpell = true;
                            break;
                        }
                    }
                }
                if (isOurSpell) {
                    spellsToStrip.push_back(sp);
                }
            }
        }

        for (auto* sp : spellsToStrip) {
            RemoveFromMagicFavorites(sp);
            player->RemoveSpell(sp);
            removedCount++;
        }

        if (removedCount > 0) {
            SKSE::log::info("[Serialization] Purged {} legacy custom spell instances from player before applying save.", removedCount);
        }
    }

    void Serialization::ApplyPendingSpells()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::warn("[Serialization] PlayerCharacter singleton not available during ApplyPendingSpells.");
            return;
        }

        // Capture which old dynamic FormIDs were in MagicFavorites before purging them
        // This prevents FavoritesMenu from crashing on dangling pointers and seamlessly re-favorites the new spell!
        std::unordered_set<RE::FormID> favoritedOldIDs;
        std::unordered_map<RE::FormID, std::size_t> hotkeyOldIDs;
        ScrubDanglingFavorites(s_allHistoricalFormIDs, s_activeCustomSpells, favoritedOldIDs, hotkeyOldIDs);

        // Strip all ghost duplicates loaded by Skyrim's .ess save file
        PurgePlayerCustomSpells();

        SKSE::log::info("[Serialization] Re-instantiating {} custom spells from co-save...", s_activeCustomSpells.size());

        auto* magicFavs = RE::MagicFavorites::GetSingleton();

        for (auto& rec : s_activeCustomSpells) {
            const RE::FormID oldFormID = rec.formID;

            SpellCreationParams params;
            params.spellName = rec.spellName;
            params.castingType = (rec.castTypeStr == "Concentration") ? 
                RE::MagicSystem::CastingType::kConcentration : RE::MagicSystem::CastingType::kFireAndForget;
            params.deliveryType = (rec.targetTypeStr == "Self") ? 
                RE::MagicSystem::Delivery::kSelf : RE::MagicSystem::Delivery::kAimed;

            for (const auto& fx : rec.effects) {
                auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(fx.effectSettingFormID);
                if (mgef) {
                    params.effects.emplace_back(mgef, fx.magnitude, fx.areaFeet, fx.duration);
                }
            }

            if (!params.effects.empty()) {
                auto* newSpell = SpellFactory::CreateSpell(params, player);
                if (newSpell) {
                    rec.formID = newSpell->GetFormID();
                    if (std::find(s_allHistoricalFormIDs.begin(), s_allHistoricalFormIDs.end(), rec.formID) == s_allHistoricalFormIDs.end()) {
                        s_allHistoricalFormIDs.push_back(rec.formID);
                    }

                    // If the spell was favorited in the save, re-bind the newly allocated SpellItem to MagicFavorites
                    if (magicFavs && favoritedOldIDs.contains(oldFormID)) {
                        if (std::find(magicFavs->spells.begin(), magicFavs->spells.end(), newSpell) == magicFavs->spells.end()) {
                            magicFavs->spells.push_back(newSpell);
                        }
                        if (auto hkIt = hotkeyOldIDs.find(oldFormID); hkIt != hotkeyOldIDs.end()) {
                            if (hkIt->second < magicFavs->hotkeys.size()) {
                                magicFavs->hotkeys[hkIt->second] = newSpell;
                            }
                        }
                        SKSE::log::info("[Serialization] Restored favorite status for '{}' (Old: {:08X} -> New: {:08X}).",
                            rec.spellName, oldFormID, rec.formID);
                    }
                }
            }
        }

        SKSE::log::info("[Serialization] Finished applying pending spells to player spellbook without duplication.");
    }

    void Serialization::OnNewGame()
    {
        PurgePlayerCustomSpells();
        s_activeCustomSpells.clear();
        s_allHistoricalFormIDs.clear();
        SKSE::log::info("[Serialization] Cleared all custom spell states on new game.");
    }

    void Serialization::TrackCustomSpell(const CustomSpellRecord& a_record)
    {
        s_activeCustomSpells.push_back(a_record);
        if (std::find(s_allHistoricalFormIDs.begin(), s_allHistoricalFormIDs.end(), a_record.formID) == s_allHistoricalFormIDs.end()) {
            s_allHistoricalFormIDs.push_back(a_record.formID);
        }
        SKSE::log::info("[Serialization] Tracked new spell: {} [FormID: {:08X}]", a_record.spellName, a_record.formID);
    }

    void Serialization::TrackCreatedSpell(const SpellCreationParams& a_params, RE::SpellItem* a_spell)
    {
        CustomSpellRecord rec;
        rec.formID = a_spell ? a_spell->GetFormID() : 0;
        rec.spellName = a_params.spellName;
        rec.castTypeStr = (a_params.castingType == RE::MagicSystem::CastingType::kConcentration) ? "Concentration" : "FireAndForget";
        rec.targetTypeStr = (a_params.deliveryType == RE::MagicSystem::Delivery::kSelf) ? "Self" : "Aimed";

        for (const auto& fx : a_params.effects) {
            if (fx.effectSetting) {
                CustomSpellRecord::EffectRecord fxRec;
                fxRec.effectSettingFormID = fx.effectSetting->GetFormID();
                fxRec.effectName = fx.effectSetting->GetFullName();
                fxRec.magnitude = fx.magnitude;
                fxRec.duration = fx.duration;
                fxRec.areaFeet = fx.area;
                rec.effects.push_back(fxRec);

                if (rec.schoolName.empty() || rec.schoolName == "Destruction") {
                    switch (fx.effectSetting->data.primaryAV) {
                    case RE::ActorValue::kRestoration: rec.schoolName = "Restoration"; break;
                    case RE::ActorValue::kAlteration:  rec.schoolName = "Alteration"; break;
                    case RE::ActorValue::kConjuration: rec.schoolName = "Conjuration"; break;
                    case RE::ActorValue::kIllusion:    rec.schoolName = "Illusion"; break;
                    default:                           rec.schoolName = "Destruction"; break;
                    }
                }
            }
        }

        TrackCustomSpell(rec);
    }

    bool Serialization::DeleteCustomSpell(RE::FormID a_formID)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        auto it = std::find_if(s_activeCustomSpells.begin(), s_activeCustomSpells.end(),
            [a_formID](const CustomSpellRecord& rec) {
                return rec.formID == a_formID;
            });

        if (it == s_activeCustomSpells.end()) {
            SKSE::log::warn("[Serialization] Attempted to delete untracked spell FormID: {:08X}", a_formID);
            return false;
        }

        std::string spellName = it->spellName;

        auto* spellItem = RE::TESForm::LookupByID<RE::SpellItem>(a_formID);
        if (spellItem) {
            // Also strip from MagicFavorites so FavoritesMenu never holds a deleted spell pointer
            RemoveFromMagicFavorites(spellItem);
            player->RemoveSpell(spellItem);
            SKSE::log::info("[Serialization] Removed spell '{}' [{:08X}] from player inventory and favorites.", spellName, a_formID);
        }

        s_activeCustomSpells.erase(it);

        if (std::find(s_allHistoricalFormIDs.begin(), s_allHistoricalFormIDs.end(), a_formID) == s_allHistoricalFormIDs.end()) {
            s_allHistoricalFormIDs.push_back(a_formID);
        }

        SKSE::log::info("[Serialization] Spell '{}' [{:08X}] deconstructed successfully.", spellName, a_formID);
        return true;
    }

    std::string Serialization::GetActiveSpellsJson()
    {
        std::string json = "[";
        bool firstSpell = true;

        for (const auto& spell : s_activeCustomSpells) {
            if (!firstSpell) {
                json += ",";
            }
            firstSpell = false;

            json += "{";
            json += std::format(R"("formId":"0x{:08X}",)", spell.formID);
            json += std::format(R"("name":"{}",)", spell.spellName);
            json += std::format(R"("school":"{}",)", spell.schoolName);
            json += std::format(R"("castType":"{}",)", spell.castTypeStr);
            json += std::format(R"("targetType":"{}",)", spell.targetTypeStr);
            json += std::format(R"("magickaCost":{},)", spell.totalMagickaCost);
            json += R"("effects":[)";

            bool firstFx = true;
            for (const auto& fx : spell.effects) {
                if (!firstFx) {
                    json += ",";
                }
                firstFx = false;

                json += "{";
                json += std::format(R"("name":"{}",)", fx.effectName);
                json += std::format(R"("magnitude":{:.1f},)", fx.magnitude);
                json += std::format(R"("duration":{},)", fx.duration);
                json += std::format(R"("area":{})", fx.areaFeet);
                json += "}";
            }

            json += "]}";
        }

        json += "]";
        return json;
    }

    void Serialization::OnSave(SKSE::SerializationInterface* a_intfc)
    {
        if (!a_intfc->OpenRecord(kSpellRecordType, kCurrentSaveVersion)) {
            SKSE::log::error("[Serialization] Failed to open co-save record for writing!");
            return;
        }

        uint32_t activeCount = static_cast<uint32_t>(s_activeCustomSpells.size());
        a_intfc->WriteRecordData(&activeCount, sizeof(activeCount));

        for (const auto& spell : s_activeCustomSpells) {
            a_intfc->WriteRecordData(&spell.formID, sizeof(spell.formID));

            auto writeStr = [&](const std::string& str) {
                uint32_t len = static_cast<uint32_t>(str.size());
                a_intfc->WriteRecordData(&len, sizeof(len));
                if (len > 0) {
                    a_intfc->WriteRecordData(str.data(), len);
                }
            };

            writeStr(spell.spellName);
            writeStr(spell.schoolName);
            writeStr(spell.castTypeStr);
            writeStr(spell.targetTypeStr);

            a_intfc->WriteRecordData(&spell.totalMagickaCost, sizeof(spell.totalMagickaCost));
            a_intfc->WriteRecordData(&spell.totalGoldCost, sizeof(spell.totalGoldCost));

            uint32_t fxCount = static_cast<uint32_t>(spell.effects.size());
            a_intfc->WriteRecordData(&fxCount, sizeof(fxCount));

            for (const auto& fx : spell.effects) {
                a_intfc->WriteRecordData(&fx.effectSettingFormID, sizeof(fx.effectSettingFormID));
                writeStr(fx.effectName);
                a_intfc->WriteRecordData(&fx.magnitude, sizeof(fx.magnitude));
                a_intfc->WriteRecordData(&fx.duration, sizeof(fx.duration));
                a_intfc->WriteRecordData(&fx.areaFeet, sizeof(fx.areaFeet));
            }
        }

        uint32_t histCount = static_cast<uint32_t>(s_allHistoricalFormIDs.size());
        a_intfc->WriteRecordData(&histCount, sizeof(histCount));
        if (histCount > 0) {
            a_intfc->WriteRecordData(s_allHistoricalFormIDs.data(), histCount * sizeof(RE::FormID));
        }

        SKSE::log::info("[Serialization] Successfully saved {} active spells and {} historical FormIDs.", activeCount, histCount);
    }

    void Serialization::OnLoad(SKSE::SerializationInterface* a_intfc)
    {
        uint32_t type = 0;
        uint32_t version = 0;
        uint32_t length = 0;

        s_activeCustomSpells.clear();

        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type != kSpellRecordType) {
                continue;
            }

            uint32_t activeCount = 0;
            if (a_intfc->ReadRecordData(&activeCount, sizeof(activeCount)) != sizeof(activeCount)) {
                SKSE::log::error("[Serialization] Corrupted header on load!");
                return;
            }

            auto readStr = [&](std::string& outStr) {
                uint32_t len = 0;
                a_intfc->ReadRecordData(&len, sizeof(len));
                outStr.resize(len);
                if (len > 0) {
                    a_intfc->ReadRecordData(outStr.data(), len);
                }
            };

            for (uint32_t i = 0; i < activeCount; ++i) {
                CustomSpellRecord spell;
                a_intfc->ReadRecordData(&spell.formID, sizeof(spell.formID));

                readStr(spell.spellName);
                readStr(spell.schoolName);
                readStr(spell.castTypeStr);
                readStr(spell.targetTypeStr);

                a_intfc->ReadRecordData(&spell.totalMagickaCost, sizeof(spell.totalMagickaCost));
                a_intfc->ReadRecordData(&spell.totalGoldCost, sizeof(spell.totalGoldCost));

                uint32_t fxCount = 0;
                a_intfc->ReadRecordData(&fxCount, sizeof(fxCount));

                for (uint32_t j = 0; j < fxCount; ++j) {
                    CustomSpellRecord::EffectRecord fx;
                    a_intfc->ReadRecordData(&fx.effectSettingFormID, sizeof(fx.effectSettingFormID));

                    RE::FormID resolvedMgefID = 0;
                    if (a_intfc->ResolveFormID(fx.effectSettingFormID, resolvedMgefID)) {
                        fx.effectSettingFormID = resolvedMgefID;
                    }

                    readStr(fx.effectName);
                    a_intfc->ReadRecordData(&fx.magnitude, sizeof(fx.magnitude));
                    a_intfc->ReadRecordData(&fx.duration, sizeof(fx.duration));
                    a_intfc->ReadRecordData(&fx.areaFeet, sizeof(fx.areaFeet));
                    spell.effects.push_back(fx);
                }

                s_activeCustomSpells.push_back(spell);
            }

            uint32_t histCount = 0;
            if (a_intfc->ReadRecordData(&histCount, sizeof(histCount)) == sizeof(histCount) && histCount > 0) {
                std::vector<RE::FormID> loadedHist(histCount);
                a_intfc->ReadRecordData(loadedHist.data(), histCount * sizeof(RE::FormID));
                for (auto id : loadedHist) {
                    if (std::find(s_allHistoricalFormIDs.begin(), s_allHistoricalFormIDs.end(), id) == s_allHistoricalFormIDs.end()) {
                        s_allHistoricalFormIDs.push_back(id);
                    }
                }
            }

            SKSE::log::info("[Serialization] Loaded {} active custom spells from co-save.", s_activeCustomSpells.size());
        }
    }

    void Serialization::OnRevert(SKSE::SerializationInterface* /*a_intfc*/)
    {
        PurgePlayerCustomSpells();
        s_activeCustomSpells.clear();
        SKSE::log::info("[Serialization] Reverted custom spell registries on session reset (Historical FormIDs preserved).");
    }
}