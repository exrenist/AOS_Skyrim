#include "Serialization.h"
#include "SpellFactory.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Spellmaking
{
    namespace
    {
        // Resolves Skyrim.esm LeftHand (0x00013F43) and RightHand (0x00013F42) equip slots
        [[nodiscard]] RE::BGSEquipSlot* GetLeftHandEquipSlot()
        {
            return RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F43);
        }

        [[nodiscard]] RE::BGSEquipSlot* GetRightHandEquipSlot()
        {
            return RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F42);
        }

        // Checks if a TESForm pointer in MagicFavorites is a live, valid form registered in the engine
        // Prevents Access Violation on freed 0xFFxxxxxx memory!
        [[nodiscard]] bool IsLiveValidMagicForm(RE::TESForm* a_ptr, RE::FormID& a_outFormID)
        {
            a_outFormID = 0;
            if (!a_ptr) {
                return false;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                for (auto* sp : player->GetActorRuntimeData().addedSpells) {
                    if (sp == a_ptr) {
                        a_outFormID = sp->GetFormID();
                        return true;
                    }
                }
            }

            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                for (auto* sp : dataHandler->GetFormArray<RE::SpellItem>()) {
                    if (sp == a_ptr) {
                        a_outFormID = sp->GetFormID();
                        return true;
                    }
                }
                for (auto* shout : dataHandler->GetFormArray<RE::TESShout>()) {
                    if (shout == a_ptr) {
                        a_outFormID = shout->GetFormID();
                        return true;
                    }
                }
            }

            return false;
        }

        // Safely removes a specific pointer from MagicFavorites without dereferencing dead memory
        void RemoveFromMagicFavorites(RE::TESForm* a_form)
        {
            if (!a_form) {
                return;
            }

            auto* magicFavs = RE::MagicFavorites::GetSingleton();
            if (!magicFavs) {
                return;
            }

            auto it = std::find(magicFavs->spells.begin(), magicFavs->spells.end(), a_form);
            if (it != magicFavs->spells.end()) {
                magicFavs->spells.erase(it);
            }

            for (auto& hotkeyForm : magicFavs->hotkeys) {
                if (hotkeyForm == a_form) {
                    hotkeyForm = nullptr;
                }
            }
        }

        // Captures current live Favorite, Hotkey (1-8), and Left/Right Hand Equip states into s_activeCustomSpells
        void SnapshotPlayerSpellStates(std::vector<CustomSpellRecord>& a_records)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* magicFavs = RE::MagicFavorites::GetSingleton();
            if (!player) {
                return;
            }

            // In CommonLibSSE-NG: selectedSpells[0] = Left Hand, selectedSpells[1] = Right Hand
            auto* leftEquipped = player->GetActorRuntimeData().selectedSpells[0];
            auto* rightEquipped = player->GetActorRuntimeData().selectedSpells[1];

            for (auto& rec : a_records) {
                if (rec.formID == 0) continue;

                RE::SpellItem* liveSpell = nullptr;
                for (auto* sp : player->GetActorRuntimeData().addedSpells) {
                    if (sp && sp->GetFormID() == rec.formID) {
                        liveSpell = sp;
                        break;
                    }
                }

                if (!liveSpell) continue;

                rec.equippedLeft = (leftEquipped == liveSpell);
                rec.equippedRight = (rightEquipped == liveSpell);

                if (magicFavs) {
                    rec.isFavorited = (std::find(magicFavs->spells.begin(), magicFavs->spells.end(), liveSpell) != magicFavs->spells.end());
                    rec.hotkeySlot = -1;
                    for (std::size_t i = 0; i < magicFavs->hotkeys.size(); ++i) {
                        if (magicFavs->hotkeys[i] == liveSpell) {
                            rec.hotkeySlot = static_cast<int32_t>(i);
                            break;
                        }
                    }
                }
            }
        }

        // Scrubs any dangling/freed pointers or non-spell forms from MagicFavorites and captures in-session state
        void ScrubDanglingFavorites(std::vector<CustomSpellRecord>& a_activeRecords)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* magicFavs = RE::MagicFavorites::GetSingleton();

            // First, if any old custom spells are still live in memory (e.g. quickload), snapshot their current states
            if (player) {
                auto* leftEq = player->GetActorRuntimeData().selectedSpells[0];
                auto* rightEq = player->GetActorRuntimeData().selectedSpells[1];

                for (auto& rec : a_activeRecords) {
                    for (auto* sp : player->GetActorRuntimeData().addedSpells) {
                        if (sp && sp->GetFormID() == rec.formID) {
                            if (leftEq == sp) rec.equippedLeft = true;
                            if (rightEq == sp) rec.equippedRight = true;
                            break;
                        }
                    }
                }
            }

            if (!magicFavs) {
                return;
            }

            // 1. Sanitize 1-8 hotkeys safely
            for (std::size_t i = 0; i < magicFavs->hotkeys.size(); ++i) {
                auto* formPtr = magicFavs->hotkeys[i];
                if (!formPtr) continue;

                RE::FormID liveID = 0;
                const bool isLive = IsLiveValidMagicForm(formPtr, liveID);

                if (!isLive || liveID >= 0xFF000000) {
                    if (isLive) {
                        for (auto& rec : a_activeRecords) {
                            if (rec.formID == liveID) {
                                rec.hotkeySlot = static_cast<int32_t>(i);
                                rec.isFavorited = true;
                            }
                        }
                    }
                    magicFavs->hotkeys[i] = nullptr;
                }
            }

            // 2. Sanitize MagicFavorites spells list safely
            for (auto it = magicFavs->spells.begin(); it != magicFavs->spells.end();) {
                auto* formPtr = *it;
                if (!formPtr) {
                    it = magicFavs->spells.erase(it);
                    continue;
                }

                RE::FormID liveID = 0;
                const bool isLive = IsLiveValidMagicForm(formPtr, liveID);

                if (!isLive || liveID >= 0xFF000000) {
                    if (isLive) {
                        for (auto& rec : a_activeRecords) {
                            if (rec.formID == liveID) {
                                rec.isFavorited = true;
                            }
                        }
                    }
                    SKSE::log::info("[Serialization] Safely scrubbed invalid/dynamic pointer from MagicFavorites.");
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

        SKSE::log::info("[Serialization] SKSE Co-Save v4 interface successfully registered.");
    }

    void Serialization::PurgePlayerCustomSpells()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        // Clear any dangling dynamic spell pointers in player's Left/Right hand slots before removing
        for (std::size_t slot = 0; slot < 2; ++slot) {
            auto* eqSpell = player->GetActorRuntimeData().selectedSpells[slot];
            if (eqSpell) {
                for (auto* sp : player->GetActorRuntimeData().addedSpells) {
                    if (sp == eqSpell && sp->GetFormID() >= 0xFF000000) {
                        player->GetActorRuntimeData().selectedSpells[slot] = nullptr;
                        break;
                    }
                }
            }
        }

        std::size_t removedCount = 0;

        const auto& actorSpells = player->GetActorRuntimeData().addedSpells;
        std::vector<RE::SpellItem*> spellsToStrip;
        for (auto* sp : actorSpells) {
            if (sp && sp->GetFormID() >= 0xFF000000) {
                spellsToStrip.push_back(sp);
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

        ScrubDanglingFavorites(s_activeCustomSpells);
        PurgePlayerCustomSpells();

        SKSE::log::info("[Serialization] Re-instantiating {} custom spells from co-save...", s_activeCustomSpells.size());

        auto* magicFavs = RE::MagicFavorites::GetSingleton();
        auto* equipMgr = RE::ActorEquipManager::GetSingleton();
        auto* leftSlot = GetLeftHandEquipSlot();
        auto* rightSlot = GetRightHandEquipSlot();

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

                    // 1. Restore Favorite & Hotkey (1-8) status
                    if (magicFavs && rec.isFavorited) {
                        if (std::find(magicFavs->spells.begin(), magicFavs->spells.end(), newSpell) == magicFavs->spells.end()) {
                            magicFavs->spells.push_back(newSpell);
                        }
                        if (rec.hotkeySlot >= 0 && static_cast<std::size_t>(rec.hotkeySlot) < magicFavs->hotkeys.size()) {
                            magicFavs->hotkeys[static_cast<std::size_t>(rec.hotkeySlot)] = newSpell;
                        }
                        SKSE::log::info("[Serialization] Restored favorite status for '{}' (Old: {:08X} -> New: {:08X}, Hotkey: {}).",
                            rec.spellName, oldFormID, rec.formID, rec.hotkeySlot);
                    }

                    // 2. Restore Left / Right Hand Equipped state via ActorEquipManager
                    // Fixes the empty sword-draw animation when pressing "R" after loading a save!
                    if (equipMgr) {
                        if (rec.equippedLeft && leftSlot) {
                            equipMgr->EquipSpell(player, newSpell, leftSlot);
                            SKSE::log::info("[Serialization] Re-equipped '{}' to Left Hand.", rec.spellName);
                        }
                        if (rec.equippedRight && rightSlot) {
                            equipMgr->EquipSpell(player, newSpell, rightSlot);
                            SKSE::log::info("[Serialization] Re-equipped '{}' to Right Hand.", rec.spellName);
                        }
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

        for (auto* sp : player->GetActorRuntimeData().addedSpells) {
            if (sp && sp->GetFormID() == a_formID) {
                RemoveFromMagicFavorites(sp);
                player->RemoveSpell(sp);
                SKSE::log::info("[Serialization] Removed spell '{}' [{:08X}] from player inventory and favorites.", spellName, a_formID);
                break;
            }
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
        // Snapshot live Favorite, Hotkey, and Left/Right Hand equip states right before saving
        SnapshotPlayerSpellStates(s_activeCustomSpells);

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

            // v4 additions: Favorite, Hotkey, Left/Right Hand equip states
            uint8_t favByte = spell.isFavorited ? 1 : 0;
            uint8_t leftByte = spell.equippedLeft ? 1 : 0;
            uint8_t rightByte = spell.equippedRight ? 1 : 0;
            a_intfc->WriteRecordData(&favByte, sizeof(favByte));
            a_intfc->WriteRecordData(&spell.hotkeySlot, sizeof(spell.hotkeySlot));
            a_intfc->WriteRecordData(&leftByte, sizeof(leftByte));
            a_intfc->WriteRecordData(&rightByte, sizeof(rightByte));
        }

        uint32_t histCount = static_cast<uint32_t>(s_allHistoricalFormIDs.size());
        a_intfc->WriteRecordData(&histCount, sizeof(histCount));
        if (histCount > 0) {
            a_intfc->WriteRecordData(s_allHistoricalFormIDs.data(), histCount * sizeof(RE::FormID));
        }

        SKSE::log::info("[Serialization] Successfully saved {} active spells (with equip/favorite states) and {} historical FormIDs.",
            activeCount, histCount);
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

                // Read v4 persistent Favorite, Hotkey, and Left/Right Hand equip states
                if (version >= 4) {
                    uint8_t favByte = 0;
                    uint8_t leftByte = 0;
                    uint8_t rightByte = 0;
                    a_intfc->ReadRecordData(&favByte, sizeof(favByte));
                    a_intfc->ReadRecordData(&spell.hotkeySlot, sizeof(spell.hotkeySlot));
                    a_intfc->ReadRecordData(&leftByte, sizeof(leftByte));
                    a_intfc->ReadRecordData(&rightByte, sizeof(rightByte));

                    spell.isFavorited = (favByte != 0);
                    spell.equippedLeft = (leftByte != 0);
                    spell.equippedRight = (rightByte != 0);
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

            SKSE::log::info("[Serialization] Loaded {} active custom spells from co-save (v{}).", s_activeCustomSpells.size(), version);
        }
    }

    void Serialization::OnRevert(SKSE::SerializationInterface* /*a_intfc*/)
    {
        // Snapshot states before revert in case of an in-game quickload
        SnapshotPlayerSpellStates(s_activeCustomSpells);
        PurgePlayerCustomSpells();
        s_activeCustomSpells.clear();
        SKSE::log::info("[Serialization] Reverted custom spell registries on session reset (Historical FormIDs preserved).");
    }
}