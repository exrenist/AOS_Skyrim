#include "Serialization.h"
#include "SpellFactory.h"
#include <algorithm>

namespace Spellmaking
{
    void Serialization::Register()
    {
        const auto serialization = SKSE::GetSerializationInterface();
        if (!serialization) {
            SKSE::log::error("Serialization: Failed to acquire SKSE serialization interface.");
            return;
        }

        serialization->SetUniqueID('AOSM');
        serialization->SetSaveCallback(OnSave);
        serialization->SetLoadCallback(OnLoad);
        serialization->SetRevertCallback(OnRevert);

        SKSE::log::info("Serialization: Registered clean co-save serialization handlers.");
    }

    void Serialization::TrackCreatedSpell(const SpellCreationParams& a_params, RE::SpellItem* a_spell)
    {
        TrackedSpellEntry entry;
        entry.params = a_params;
        entry.spellFormID = a_spell ? a_spell->GetFormID() : 0;

        s_trackedSpells.push_back(entry);

        if (entry.spellFormID >= 0xFF000000) {
            s_allHistoricalFormIDs.push_back(entry.spellFormID);
        }
    }

    void Serialization::PurgePlayerCustomSpells()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        std::size_t removedCount = 0;

        // 1. Purge all historical dynamic FormIDs registered during the entire game execution
        for (const auto formID : s_allHistoricalFormIDs) {
            if (formID >= 0xFF000000) {
                if (auto* oldSpell = RE::TESForm::LookupByID<RE::SpellItem>(formID)) {
                    if (player->HasSpell(oldSpell)) {
                        player->RemoveSpell(oldSpell);
                        removedCount++;
                    }
                }
            }
        }

        // 2. Purge any FormIDs serialized into the current save file
        for (const auto& pending : s_pendingSpells) {
            if (pending.spellFormID >= 0xFF000000) {
                if (auto* oldSpell = RE::TESForm::LookupByID<RE::SpellItem>(pending.spellFormID)) {
                    if (player->HasSpell(oldSpell)) {
                        player->RemoveSpell(oldSpell);
                        removedCount++;
                    }
                }
            }
        }

        if (removedCount > 0) {
            SKSE::log::info("Serialization: Purged {} legacy custom spell instances from player before applying save.", removedCount);
        }
    }

    void Serialization::OnRevert(SKSE::SerializationInterface*)
    {
        // Notice: s_allHistoricalFormIDs is intentionally NOT cleared here,
        // ensuring we can recognize and strip ghosts after the .ess file loads into memory.
        s_trackedSpells.clear();
        s_pendingSpells.clear();
        SKSE::log::info("Serialization: Cleared in-memory active spell registry.");
    }

    void Serialization::OnSave(SKSE::SerializationInterface* a_serde)
    {
        if (!a_serde->OpenRecord(kSpellRecordType, kSerializationVersion)) {
            SKSE::log::error("Serialization: Failed to open record for saving.");
            return;
        }

        const auto count = static_cast<std::uint32_t>(s_trackedSpells.size());
        a_serde->WriteRecordData(&count, sizeof(count));

        for (const auto& entry : s_trackedSpells) {
            // Write runtime FormID (v3 addition)
            a_serde->WriteRecordData(&entry.spellFormID, sizeof(entry.spellFormID));

            // Write spell metadata
            const auto nameLen = static_cast<std::uint32_t>(entry.params.spellName.size());
            a_serde->WriteRecordData(&nameLen, sizeof(nameLen));
            a_serde->WriteRecordData(entry.params.spellName.data(), nameLen);

            const auto delivery = static_cast<std::uint32_t>(entry.params.deliveryType);
            const auto casting = static_cast<std::uint32_t>(entry.params.castingType);
            a_serde->WriteRecordData(&delivery, sizeof(delivery));
            a_serde->WriteRecordData(&casting, sizeof(casting));
            a_serde->WriteRecordData(&entry.params.chargeTime, sizeof(entry.params.chargeTime));

            // Write composite effects
            const auto fxCount = static_cast<std::uint32_t>(entry.params.effects.size());
            a_serde->WriteRecordData(&fxCount, sizeof(fxCount));

            for (const auto& fx : entry.params.effects) {
                const auto mgefID = fx.effectSetting ? fx.effectSetting->GetFormID() : 0;
                a_serde->WriteRecordData(&mgefID, sizeof(mgefID));
                a_serde->WriteRecordData(&fx.magnitude, sizeof(fx.magnitude));
                a_serde->WriteRecordData(&fx.duration, sizeof(fx.duration));
                a_serde->WriteRecordData(&fx.area, sizeof(fx.area));
            }
        }

        SKSE::log::info("Serialization: Successfully persisted {} custom composite spells to co-save (v{}).", count, kSerializationVersion);
    }

    void Serialization::OnLoad(SKSE::SerializationInterface* a_serde)
    {
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;

        s_pendingSpells.clear();

        while (a_serde->GetNextRecordInfo(type, version, length)) {
            if (type != kSpellRecordType) {
                continue;
            }

            std::uint32_t spellCount = 0;
            a_serde->ReadRecordData(&spellCount, sizeof(spellCount));

            for (std::uint32_t i = 0; i < spellCount; ++i) {
                TrackedSpellEntry entry;

                // Read runtime FormID if save was created with v3
                if (version >= 3) {
                    a_serde->ReadRecordData(&entry.spellFormID, sizeof(entry.spellFormID));
                } else {
                    entry.spellFormID = 0;
                }

                std::uint32_t nameLen = 0;
                a_serde->ReadRecordData(&nameLen, sizeof(nameLen));
                entry.params.spellName.resize(nameLen);
                a_serde->ReadRecordData(entry.params.spellName.data(), nameLen);

                std::uint32_t delivery = 0;
                std::uint32_t casting = 0;
                a_serde->ReadRecordData(&delivery, sizeof(delivery));
                a_serde->ReadRecordData(&casting, sizeof(casting));
                entry.params.deliveryType = static_cast<RE::MagicSystem::Delivery>(delivery);
                entry.params.castingType = static_cast<RE::MagicSystem::CastingType>(casting);
                a_serde->ReadRecordData(&entry.params.chargeTime, sizeof(entry.params.chargeTime));

                std::uint32_t fxCount = 0;
                a_serde->ReadRecordData(&fxCount, sizeof(fxCount));

                for (std::uint32_t j = 0; j < fxCount; ++j) {
                    RE::FormID rawFormID = 0;
                    float mag = 0.0f;
                    std::uint32_t dur = 0;
                    std::uint32_t area = 0;

                    a_serde->ReadRecordData(&rawFormID, sizeof(rawFormID));
                    a_serde->ReadRecordData(&mag, sizeof(mag));
                    a_serde->ReadRecordData(&dur, sizeof(dur));
                    a_serde->ReadRecordData(&area, sizeof(area));

                    RE::FormID resolvedID = 0;
                    if (a_serde->ResolveFormID(rawFormID, resolvedID)) {
                        if (auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(resolvedID)) {
                            entry.params.effects.push_back({ mgef, mag, area, dur });
                        }
                    }
                }

                if (!entry.params.effects.empty()) {
                    s_pendingSpells.push_back(entry);
                }
            }
        }

        SKSE::log::info("Serialization: Deserialized {} custom spell recipes (queued for PostLoadGame).", s_pendingSpells.size());
    }

    void Serialization::ApplyPendingSpells()
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::error("Serialization: Cannot apply pending spells - player reference is null.");
            return;
        }

        // 1. Completely strip legacy instances loaded by the engine's .ess save
        PurgePlayerCustomSpells();

        s_trackedSpells.clear();

        // 2. Re-instantiate the precise spell set for this save
        for (const auto& pending : s_pendingSpells) {
            auto* createdSpell = SpellFactory::CreateSpell(pending.params, player);
            if (createdSpell) {
                TrackedSpellEntry activeEntry;
                activeEntry.spellFormID = createdSpell->GetFormID();
                activeEntry.params = pending.params;

                s_trackedSpells.push_back(activeEntry);
                s_allHistoricalFormIDs.push_back(activeEntry.spellFormID);
            }
        }

        s_pendingSpells.clear();
        SKSE::log::info("Serialization: Successfully restored {} custom spells to player without duplication.", s_trackedSpells.size());
    }

    void Serialization::OnNewGame()
    {
        PurgePlayerCustomSpells();
        s_trackedSpells.clear();
        s_pendingSpells.clear();
        s_allHistoricalFormIDs.clear();
        SKSE::log::info("Serialization: Initialized clean state for new game.");
    }
}