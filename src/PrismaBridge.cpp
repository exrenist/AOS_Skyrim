#include "PrismaBridge.h"
#include "SpellAggregator.h"
#include "SpellFactory.h"
#include "Serialization.h"
#include "RE/B/BSAudioManager.h"
#include <format>
#include <sstream>
#include <algorithm>

namespace Spellmaking
{
    void PrismaBridge::ShowHUDMessage(const char* a_message)
    {
        // Address Library ID 52058 (1.5.97) / 52933 (1.6.x+) for native HUD notification
        using ShowNotification_t = void(*)(const char*, const char*, bool);
        static REL::Relocation<ShowNotification_t> ShowNotification{ RELOCATION_ID(52058, 52933) };
        ShowNotification(a_message, nullptr, true);
    }

    void PrismaBridge::PlayUIAudio(const std::string& a_soundName)
    {
        auto* audioManager = RE::BSAudioManager::GetSingleton();
        if (!audioManager) {
            return;
        }

        RE::FormID soundID = 0;
        if (a_soundName == "UIMenuFocus" || a_soundName == "hover") {
            soundID = 0x0003C759; // UIMenuFocus
        } else if (a_soundName == "UIMenuOK" || a_soundName == "click") {
            soundID = 0x0003C759; // UIMenuOK
        } else if (a_soundName == "UIMenuPrevNext") {
            soundID = 0x0003C759; // UIMenuPrevNext
        } else if (a_soundName == "UIMenuCancel" || a_soundName == "cancel") {
            soundID = 0x0003C75A; // UIMenuCancel
        } else if (a_soundName == "ITMGoldDown" || a_soundName == "gold") {
            soundID = 0x0003E955; // ITMGoldDown
        } else if (a_soundName == "MAGFail" || a_soundName == "fail") {
            soundID = 0x00010E40; // MAGFail
        }

        if (soundID != 0) {
            audioManager->Play(soundID);
        }
    }

    void PrismaBridge::OnDomReady(PrismaView a_view)
    {
        if (!s_prismaUI || a_view != s_altarView) {
            return;
        }

        // Enforce initial double-lock: guarantee the view is completely invisible upon startup
        s_prismaUI->Hide(a_view);
        s_prismaUI->Unfocus(a_view);
        s_prismaUI->Invoke(a_view, "if (window.AltarAPI && window.AltarAPI.closeMenu) window.AltarAPI.closeMenu();", nullptr);
        SKSE::log::info("[PrismaBridge] View DOM ready confirmed. Initial hidden state successfully enforced.");
    }

    bool PrismaBridge::InitializeAPI()
    {
        if (s_prismaUI) {
            return true;
        }

        s_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
        if (!s_prismaUI) {
            SKSE::log::error("[PrismaBridge] PrismaUI.dll API interface not found! Ensure PrismaUI is installed.");
            return false;
        }

        // Create the persistent Altar View and bind OnDomReady to lock out game launch popups
        s_altarView = s_prismaUI->CreateView(kViewPath, OnDomReady);

        if (s_altarView == PRISMA_UI_API::kInvalidView) {
            SKSE::log::error("[PrismaBridge] Failed to create PrismaView for {}", kViewPath);
            return false;
        }

        // Initial hide immediately after creation
        s_prismaUI->Hide(s_altarView);
        s_prismaUI->Unfocus(s_altarView);

        // Register JS -> C++ event listeners
        s_prismaUI->RegisterJSListener(s_altarView, "onPlayerCraftSpell", [](const char* a_json) {
            if (a_json) {
                HandleCraftSpellPayload(a_json);
            }
        });

        s_prismaUI->RegisterJSListener(s_altarView, "onPlayerDeconstructSpell", [](const char* a_json) {
            if (a_json) {
                HandleDeconstructSpellPayload(a_json);
            }
        });

        s_prismaUI->RegisterJSListener(s_altarView, "onPlayUISound", [](const char* a_soundName) {
            if (a_soundName) {
                PlayUIAudio(a_soundName);
            }
        });

        s_prismaUI->RegisterJSListener(s_altarView, "onMenuCloseRequested", []([[maybe_unused]] const char* a_param) {
            CloseAltarMenu();
        });

        SKSE::log::info("[PrismaBridge] Successfully initialized view and registered JS listeners.");
        return true;
    }

    bool PrismaBridge::OpenMenu(std::int32_t a_currentGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka)
    {
        return OpenAltarMenu(a_currentGold, a_maxMagicka, a_currentMagicka);
    }

    bool PrismaBridge::OpenAltarMenu(std::int32_t a_currentGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka)
    {
        if (!InitializeAPI() || s_altarView == PRISMA_UI_API::kInvalidView) {
            ShowHUDMessage("Failed to invoke Altar interface.");
            return false;
        }

        // 1. Send Player Economy and Stats
        std::string playerJson = std::format(
            "{{\"currentGold\":{},\"maxMagicka\":{},\"currentMagicka\":{}}}",
            a_currentGold, a_maxMagicka, a_currentMagicka);
        s_prismaUI->Invoke(s_altarView, std::format("window.AltarAPI.setPlayerData({});", playerJson).c_str(), nullptr);

        // 2. Aggregate and send Known Magic Effects
        const auto availableEffects = SpellAggregator::CollectAvailableEffects();
        std::string effectsJson = SpellAggregator::BuildEffectsJsonPayload(availableEffects);
        s_prismaUI->Invoke(s_altarView, std::format("window.AltarAPI.setKnownEffects({});", effectsJson).c_str(), nullptr);

        // 3. Serialize and send Bound Spells for Deconstruct mode
        std::string createdSpellsJson = Serialization::GetActiveSpellsJson();
        s_prismaUI->Invoke(s_altarView, std::format("window.AltarAPI.setCreatedSpells({});", createdSpellsJson).c_str(), nullptr);

        // 4. Reset Cart and open JS window
        s_prismaUI->Invoke(s_altarView, "if (window.AltarAPI && window.AltarAPI.resetCart) window.AltarAPI.resetCart();", nullptr);
        s_prismaUI->Invoke(s_altarView, "if (window.AltarAPI && window.AltarAPI.openMenu) window.AltarAPI.openMenu();", nullptr);

        // 5. Reveal View and capture keyboard/mouse focus
        s_prismaUI->Show(s_altarView);
        s_prismaUI->Focus(s_altarView, true, false);
        s_isMenuOpen = true;

        PlayUIAudio("UIMenuOK");
        SKSE::log::info("[PrismaBridge] Altar menu opened with dual-mode datasets.");
        return true;
    }

    void PrismaBridge::CloseAltarMenu()
    {
        if (s_prismaUI && s_altarView != PRISMA_UI_API::kInvalidView && s_isMenuOpen) {
            s_prismaUI->Invoke(s_altarView, "if (window.AltarAPI && window.AltarAPI.closeMenu) window.AltarAPI.closeMenu();", nullptr);
            s_prismaUI->Invoke(s_altarView, "if (window.AltarAPI && window.AltarAPI.resetCart) window.AltarAPI.resetCart();", nullptr);
            s_prismaUI->Unfocus(s_altarView);
            s_prismaUI->Hide(s_altarView);
            s_isMenuOpen = false;
            PlayUIAudio("UIMenuCancel");
            SKSE::log::info("[PrismaBridge] Altar menu closed and focus released.");
        }
    }

    void PrismaBridge::CloseMenu()
    {
        CloseAltarMenu();
    }

    void PrismaBridge::HandleCraftSpellPayload(const std::string& a_jsonPayload)
    {
        SKSE::log::info("[PrismaBridge] Processing craft payload...");

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return;
        }

        auto extractString = [&](const std::string& key) -> std::string {
            auto pos = a_jsonPayload.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            auto colon = a_jsonPayload.find(':', pos);
            if (colon == std::string::npos) return "";
            auto quote1 = a_jsonPayload.find('"', colon);
            if (quote1 == std::string::npos) return "";
            auto quote2 = a_jsonPayload.find('"', quote1 + 1);
            if (quote2 == std::string::npos) return "";
            return a_jsonPayload.substr(quote1 + 1, quote2 - quote1 - 1);
        };

        auto extractUint = [&](const std::string& key) -> std::uint32_t {
            auto pos = a_jsonPayload.find("\"" + key + "\"");
            if (pos == std::string::npos) return 0;
            auto colon = a_jsonPayload.find(':', pos);
            if (colon == std::string::npos) return 0;
            auto valStart = a_jsonPayload.find_first_of("0123456789", colon);
            if (valStart == std::string::npos) return 0;
            auto valEnd = a_jsonPayload.find_first_not_of("0123456789", valStart);
            std::string numStr = a_jsonPayload.substr(valStart, valEnd - valStart);
            try { return static_cast<std::uint32_t>(std::stoul(numStr)); } catch (...) { return 0; }
        };

        std::string spellName = extractString("spellName");
        if (spellName.empty()) spellName = "Custom Incantation";

        std::string castTypeStr = extractString("castType");
        std::string targetTypeStr = extractString("targetType");
        std::uint32_t totalMagicka = extractUint("totalMagickaCost");
        std::uint32_t totalGold = extractUint("totalGoldCost");

        SpellCreationParams params;
        params.spellName = spellName;
        params.castingType = (castTypeStr == "Concentration") ? 
            RE::MagicSystem::CastingType::kConcentration : RE::MagicSystem::CastingType::kFireAndForget;
        params.deliveryType = (targetTypeStr == "Self") ? 
            RE::MagicSystem::Delivery::kSelf : RE::MagicSystem::Delivery::kAimed;
        params.chargeTime = (params.castingType == RE::MagicSystem::CastingType::kConcentration) ? 0.0f : 0.5f;

        CustomSpellRecord coSaveRecord;
        coSaveRecord.spellName = spellName;
        coSaveRecord.castTypeStr = castTypeStr.empty() ? "FireAndForget" : castTypeStr;
        coSaveRecord.targetTypeStr = targetTypeStr.empty() ? "Aimed" : targetTypeStr;
        coSaveRecord.totalMagickaCost = totalMagicka;
        coSaveRecord.totalGoldCost = totalGold;

        auto effectsPos = a_jsonPayload.find("\"effects\"");
        if (effectsPos != std::string::npos) {
            size_t cursor = effectsPos;
            while ((cursor = a_jsonPayload.find('{', cursor)) != std::string::npos) {
                size_t endBlock = a_jsonPayload.find('}', cursor);
                if (endBlock == std::string::npos) break;

                std::string block = a_jsonPayload.substr(cursor, endBlock - cursor + 1);

                auto idPos = block.find("\"id\":\"");
                if (idPos != std::string::npos) {
                    auto idEnd = block.find('"', idPos + 6);
                    std::string hexId = block.substr(idPos + 6, idEnd - (idPos + 6));
                    RE::FormID mgefId = 0;
                    try { mgefId = static_cast<RE::FormID>(std::stoul(hexId, nullptr, 16)); } catch (...) {}

                    auto getBlockNum = [&](const std::string& tag) -> float {
                        auto p = block.find("\"" + tag + "\":");
                        if (p == std::string::npos) return 0.0f;
                        auto numStart = block.find_first_of("0123456789.", p);
                        auto numEnd = block.find_first_not_of("0123456789.", numStart);
                        try { return std::stof(block.substr(numStart, numEnd - numStart)); } catch (...) { return 0.0f; }
                    };

                    float mag = getBlockNum("magnitude");
                    auto dur = static_cast<std::uint32_t>(getBlockNum("duration"));
                    auto area = static_cast<std::uint32_t>(getBlockNum("areaFeet"));

                    if (auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(mgefId)) {
                        params.effects.emplace_back(mgef, mag, area, dur);

                        CustomSpellRecord::EffectRecord fxRec;
                        fxRec.effectSettingFormID = mgefId;
                        fxRec.effectName = mgef->GetFullName();
                        fxRec.magnitude = mag;
                        fxRec.duration = dur;
                        fxRec.areaFeet = area;
                        coSaveRecord.effects.push_back(fxRec);

                        if (coSaveRecord.schoolName.empty() || coSaveRecord.schoolName == "Destruction") {
                            switch (mgef->data.primaryAV) {
                            case RE::ActorValue::kRestoration: coSaveRecord.schoolName = "Restoration"; break;
                            case RE::ActorValue::kAlteration:  coSaveRecord.schoolName = "Alteration"; break;
                            case RE::ActorValue::kConjuration: coSaveRecord.schoolName = "Conjuration"; break;
                            case RE::ActorValue::kIllusion:    coSaveRecord.schoolName = "Illusion"; break;
                            default:                           coSaveRecord.schoolName = "Destruction"; break;
                            }
                        }
                    }
                }
                cursor = endBlock + 1;
            }
        }

        if (params.effects.empty()) {
            SKSE::log::error("[PrismaBridge] No valid magic effects resolved in payload.");
            return;
        }

        if (totalGold > 0) {
            auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
            if (gold) {
                player->RemoveItem(gold, static_cast<std::int32_t>(totalGold), RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
            }
        }

        PlayUIAudio("ITMGoldDown");

        auto* createdSpell = SpellFactory::CreateSpell(params, player);
        if (createdSpell) {
            coSaveRecord.formID = createdSpell->GetFormID();
            Serialization::TrackCustomSpell(coSaveRecord);

            std::string refreshedList = Serialization::GetActiveSpellsJson();
            s_prismaUI->Invoke(s_altarView, std::format("window.AltarAPI.setCreatedSpells({});", refreshedList).c_str(), nullptr);

            ShowHUDMessage(std::format("'{}' successfully forged and bound to your spirit.", spellName).c_str());
        }
    }

    void PrismaBridge::HandleDeconstructSpellPayload(const std::string& a_jsonPayload)
    {
        SKSE::log::info("[PrismaBridge] Processing deconstruct payload: {}", a_jsonPayload);

        auto idPos = a_jsonPayload.find("\"formId\":\"");
        if (idPos == std::string::npos) {
            idPos = a_jsonPayload.find("\"formId\": \"");
        }

        if (idPos == std::string::npos) {
            SKSE::log::error("[PrismaBridge] Missing formId in deconstruct payload!");
            return;
        }

        size_t startPos = a_jsonPayload.find('"', idPos + 9) != std::string::npos ? (idPos + 9) : (idPos + 10);
        if (a_jsonPayload[startPos] == '"') startPos++;

        size_t endPos = a_jsonPayload.find('"', startPos);
        std::string hexStr = a_jsonPayload.substr(startPos, endPos - startPos);

        RE::FormID targetFormID = 0;
        try {
            targetFormID = static_cast<RE::FormID>(std::stoul(hexStr, nullptr, 16));
        } catch (...) {
            SKSE::log::error("[PrismaBridge] Failed to parse hex FormID from string: {}", hexStr);
            return;
        }

        if (Serialization::DeleteCustomSpell(targetFormID)) {
            PlayUIAudio("MAGFail");

            std::string refreshedList = Serialization::GetActiveSpellsJson();
            s_prismaUI->Invoke(s_altarView, std::format("window.AltarAPI.setCreatedSpells({});", refreshedList).c_str(), nullptr);

            ShowHUDMessage("Incantation unbound and purged from your spellbook.");
        }
    }
}