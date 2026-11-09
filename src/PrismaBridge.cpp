#include "PrismaBridge.h"
#include "SpellAggregator.h"
#include "SpellFactory.h"
#include "Serialization.h"
#include "RE/B/BSAudioManager.h"
#include <regex>
#include <sstream>
#include <iomanip>

namespace Spellmaking
{
    namespace
    {
        RE::BGSSoundDescriptorForm* s_soundHover{ nullptr };
        RE::BGSSoundDescriptorForm* s_soundClick{ nullptr };
        RE::BGSSoundDescriptorForm* s_soundCancel{ nullptr };
        RE::BGSSoundDescriptorForm* s_soundGold{ nullptr };
        bool s_soundsInitialized{ false };

        void EnsureSoundsInitialized()
        {
            if (s_soundsInitialized) {
                return;
            }

            if (auto* goldItem = RE::TESForm::LookupByID<RE::TESObjectMISC>(0x0000000F)) {
                s_soundGold = goldItem->putdownSound;
            }
            if (!s_soundGold) {
                s_soundGold = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x0003E955);
            }

            s_soundHover  = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x0003C759);
            s_soundClick  = s_soundHover;
            s_soundCancel = RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(0x0003C75A);

            s_soundsInitialized = true;
            SKSE::log::info("PrismaBridge: Audio Engine Bound -> Hover: 0x{:08X}, Click: 0x{:08X}, Cancel: 0x{:08X}, Gold: 0x{:08X}",
                s_soundHover  ? s_soundHover->GetFormID()  : 0,
                s_soundClick  ? s_soundClick->GetFormID()  : 0,
                s_soundCancel ? s_soundCancel->GetFormID() : 0,
                s_soundGold   ? s_soundGold->GetFormID()   : 0);
        }

        void PlayDescriptor(RE::BGSSoundDescriptorForm* a_descriptor)
        {
            if (!a_descriptor) {
                return;
            }

            auto* audioManager = RE::BSAudioManager::GetSingleton();
            if (audioManager) {
                audioManager->Play(a_descriptor->GetFormID());
            }
        }

        std::int32_t GetTruePlayerGold(RE::PlayerCharacter* a_player)
        {
            if (!a_player) return 0;
            auto* goldForm = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
            if (!goldForm) return 0;

            const auto inv = a_player->GetInventory();
            const auto it = inv.find(goldForm);
            if (it != inv.end()) {
                return it->second.first;
            }

            const auto rawCount = a_player->GetItemCount(goldForm);
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(rawCount) & 0x7FFFFFFF);
        }

        void DeductPlayerGold(RE::PlayerCharacter* a_player, std::int32_t a_amount)
        {
            if (!a_player || a_amount <= 0) {
                return;
            }

            auto* goldItem = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
            if (!goldItem) {
                return;
            }

            a_player->RemoveItem(goldItem, a_amount, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);

            if (auto* invChanges = a_player->GetInventoryChanges()) {
                invChanges->SendContainerChangedEvent(nullptr, a_player, goldItem, a_amount);
            }

            EnsureSoundsInitialized();
            PlayDescriptor(s_soundGold);
        }
    }

    void PrismaBridge::InitializeAPI()
    {
        if (s_prismaUI) {
            return;
        }

        s_prismaUI = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();

        if (s_prismaUI) {
            SKSE::log::info("PrismaBridge: Successfully bound official PrismaUI v1 interface.");
        } else {
            SKSE::log::error("PrismaBridge: PrismaUI runtime API not available. Ensure PrismaUI.dll is active.");
        }
    }

    void PrismaBridge::Register()
    {
        InitializeAPI();
    }

    void PrismaBridge::ShowHUDNotification(const char* a_message)
    {
        using func_t = void(*)(const char*, const char*, bool);
        static REL::Relocation<func_t> func{ RELOCATION_ID(52058, 52933) };
        if (func.address()) {
            func(a_message, nullptr, true);
        }
    }

    void PrismaBridge::RefundGold(RE::PlayerCharacter* a_player, std::int32_t a_amount)
    {
        if (!a_player || a_amount <= 0) {
            return;
        }

        auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F);
        if (gold) {
            a_player->AddObjectToContainer(gold, nullptr, a_amount, nullptr);
            SKSE::log::info("PrismaBridge: Refunded {} gold to player.", a_amount);
        }
    }

    void PrismaBridge::PlaySound(const std::string& a_soundName)
    {
        EnsureSoundsInitialized();
        if (a_soundName == "hover") {
            PlayDescriptor(s_soundHover);
        } else if (a_soundName == "click") {
            PlayDescriptor(s_soundClick);
        } else if (a_soundName == "cancel") {
            PlayDescriptor(s_soundCancel);
        } else if (a_soundName == "gold") {
            PlayDescriptor(s_soundGold);
        } else if (a_soundName == "slider") {
            PlayDescriptor(s_soundHover);
        }
    }

    std::string PrismaBridge::BuildPlayerDataJson(std::int32_t a_gold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka)
    {
        return std::format(
            "{{\"currentGold\":{},\"maxMagicka\":{},\"currentMagicka\":{}}}",
            a_gold, a_maxMagicka, a_currentMagicka);
    }

    bool PrismaBridge::OpenMenu(std::int32_t a_playerGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka)
    {
        if (!s_prismaUI) {
            InitializeAPI();
            if (!s_prismaUI) {
                SKSE::log::error("PrismaBridge: Cannot open menu - PrismaUI API is unavailable.");
                return false;
            }
        }

        EnsureSoundsInitialized();
        SKSE::log::info("PrismaBridge: Preparing arcane matrix for PrismaUI frontend...");

        const auto discoveredEffects = SpellAggregator::CollectAvailableEffects();
        s_cachedEffectsJson = SpellAggregator::BuildEffectsJsonPayload(discoveredEffects);
        s_cachedPlayerDataJson = BuildPlayerDataJson(a_playerGold, a_maxMagicka, a_currentMagicka);

        if (s_view != PRISMA_UI_API::kInvalidView && s_prismaUI->IsValid(s_view)) {
            SKSE::log::info("PrismaBridge: Reusing active view [{:X}]. Resetting cart, updating and focusing...", s_view);
            s_prismaUI->Invoke(s_view, "if(window.AltarAPI && window.AltarAPI.resetCart) window.AltarAPI.resetCart();", nullptr);
            s_prismaUI->Invoke(s_view, std::format("window.AltarAPI.setPlayerData({});", s_cachedPlayerDataJson).c_str(), nullptr);
            s_prismaUI->Invoke(s_view, std::format("window.AltarAPI.setKnownEffects({});", s_cachedEffectsJson).c_str(), nullptr);
            s_prismaUI->Show(s_view);
            s_prismaUI->Focus(s_view, true, false);
            return true;
        }

        SKSE::log::info("PrismaBridge: Allocating PrismaView for '{}'...", kViewPath);
        s_view = s_prismaUI->CreateView(kViewPath, OnDomReady);

        if (s_view == PRISMA_UI_API::kInvalidView) {
            SKSE::log::error("PrismaBridge: Failed to allocate PrismaView.");
            return false;
        }

        s_prismaUI->RegisterJSListener(s_view, "onPlayerCraftSpell", OnCraftSpellDispatched);
        s_prismaUI->RegisterJSListener(s_view, "onMenuCloseRequested", OnCloseMenuRequested);
        s_prismaUI->RegisterJSListener(s_view, "onPlayUISound", OnPlayUISoundDispatched);

        SKSE::log::info("PrismaBridge: PrismaView [{:X}] created. Awaiting DOM ready...", s_view);
        return true;
    }

    bool PrismaBridge::OpenAltarMenu(std::int32_t a_playerGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka)
    {
        return OpenMenu(a_playerGold, a_maxMagicka, a_currentMagicka);
    }

    void PrismaBridge::OnDomReady(PrismaView a_view)
    {
        if (!s_prismaUI || a_view != s_view) {
            return;
        }

        SKSE::log::info("PrismaBridge: View [{:X}] DOM ready. Registering listeners and acquiring focus...", a_view);

        s_prismaUI->RegisterJSListener(a_view, "onPlayerCraftSpell", OnCraftSpellDispatched);
        s_prismaUI->RegisterJSListener(a_view, "onMenuCloseRequested", OnCloseMenuRequested);
        s_prismaUI->RegisterJSListener(a_view, "onPlayUISound", OnPlayUISoundDispatched);

        s_prismaUI->Invoke(a_view, "if(window.AltarAPI && window.AltarAPI.resetCart) window.AltarAPI.resetCart();", nullptr);
        s_prismaUI->Invoke(a_view, std::format("window.AltarAPI.setPlayerData({});", s_cachedPlayerDataJson).c_str(), nullptr);
        s_prismaUI->Invoke(a_view, std::format("window.AltarAPI.setKnownEffects({});", s_cachedEffectsJson).c_str(), nullptr);

        s_prismaUI->Show(a_view);
        s_prismaUI->Focus(a_view, true, false);

        SKSE::log::info("PrismaBridge: Altar of Spellmaking interface is now live and interactive.");
    }

    void PrismaBridge::CloseMenu()
    {
        if (!s_prismaUI || s_view == PRISMA_UI_API::kInvalidView) {
            return;
        }

        SKSE::log::info("PrismaBridge: Closing interface, resetting cart state and releasing focus...");
        s_prismaUI->Invoke(s_view, "if(window.AltarAPI && window.AltarAPI.resetCart) window.AltarAPI.resetCart();", nullptr);
        s_prismaUI->Unfocus(s_view);
        s_prismaUI->Hide(s_view);
    }

    void PrismaBridge::OnCloseMenuRequested(const char*)
    {
        CloseMenu();
    }

    void PrismaBridge::OnPlayUISoundDispatched(const char* a_soundName)
    {
        if (!a_soundName) return;
        PlaySound(std::string(a_soundName));
    }

    void PrismaBridge::OnCraftSpellDispatched(const char* a_payload)
    {
        if (!a_payload) {
            return;
        }

        SKSE::log::info("PrismaBridge: Received craft payload from frontend.");
        if (ParseAndExecuteCrafting(a_payload)) {
            SKSE::log::info("PrismaBridge: Spell crafted successfully. Closing menu...");
            ShowHUDNotification("Incantation Successfully Forged!");
            CloseMenu();
        } else {
            SKSE::log::warn("PrismaBridge: Crafting could not be completed.");
        }
    }

    bool PrismaBridge::ParseAndExecuteCrafting(const std::string& a_jsonPayload)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        std::regex nameRegex("\"spellName\"\\s*:\\s*\"([^\"]+)\"");
        std::regex castTypeRegex("\"castType\"\\s*:\\s*\"([^\"]+)\"");
        std::regex targetTypeRegex("\"targetType\"\\s*:\\s*\"([^\"]+)\"");
        std::regex goldCostRegex("\"totalGoldCost\"\\s*:\\s*([0-9]+)");

        std::smatch match;
        std::string spellName = "Custom Incantation";
        std::string castType = "FireAndForget";
        std::string targetType = "Aimed";
        std::int32_t totalGold = 0;

        if (std::regex_search(a_jsonPayload, match, nameRegex)) {
            spellName = match[1].str();
        }
        if (std::regex_search(a_jsonPayload, match, castTypeRegex)) {
            castType = match[1].str();
        }
        if (std::regex_search(a_jsonPayload, match, targetTypeRegex)) {
            targetType = match[1].str();
        }
        if (std::regex_search(a_jsonPayload, match, goldCostRegex)) {
            totalGold = std::stoi(match[1].str());
        }

        const std::int32_t currentGold = GetTruePlayerGold(player);

        if (currentGold < totalGold) {
            SKSE::log::warn("PrismaBridge: Crafting rejected - insufficient gold (Requires {}, Has {}).",
                totalGold, currentGold);
            ShowHUDNotification("You do not possess enough gold to forge this incantation!");
            PlaySound("cancel");
            return false;
        }

        if (totalGold > 0) {
            DeductPlayerGold(player, totalGold);
        }

        RE::MagicSystem::Delivery delivery = (targetType == "Self") 
            ? RE::MagicSystem::Delivery::kSelf 
            : RE::MagicSystem::Delivery::kAimed;

        RE::MagicSystem::CastingType casting = (castType == "Concentration")
            ? RE::MagicSystem::CastingType::kConcentration
            : RE::MagicSystem::CastingType::kFireAndForget;

        SpellCreationParams params;
        params.spellName = spellName;
        params.deliveryType = delivery;
        params.castingType = casting;
        params.chargeTime = (casting == RE::MagicSystem::CastingType::kConcentration) ? 0.0f : 0.5f;

        const auto effectsPos = a_jsonPayload.find("\"effects\"");
        if (effectsPos == std::string::npos) {
            SKSE::log::error("PrismaBridge: Invalid payload - missing 'effects' array. Refunding gold.");
            RefundGold(player, totalGold);
            ShowHUDNotification("Incantation Unstable! Arcane matrix collapsed. Gold refunded.");
            PlaySound("cancel");
            return false;
        }

        const std::string effectsSub = a_jsonPayload.substr(effectsPos);

        std::regex singleBlockRegex("\\{([^\\}]+)\\}");
        std::regex idRegex("\"id\"\\s*:\\s*\"(0x[0-9A-Fa-f]+)\"");
        std::regex magRegex("\"magnitude\"\\s*:\\s*([0-9.]+)");
        std::regex durRegex("\"duration\"\\s*:\\s*([0-9]+)");
        std::regex areaRegex("\"areaFeet\"\\s*:\\s*([0-9]+)");

        auto blocksBegin = std::sregex_iterator(effectsSub.begin(), effectsSub.end(), singleBlockRegex);
        auto blocksEnd = std::sregex_iterator();
        std::string failedEffectID = "";

        for (auto it = blocksBegin; it != blocksEnd; ++it) {
            const std::string blockStr = it->str();

            std::smatch idMatch, magMatch, durMatch, areaMatch;
            if (std::regex_search(blockStr, idMatch, idRegex)) {
                const auto formId = static_cast<RE::FormID>(std::stoul(idMatch[1].str(), nullptr, 16));
                
                float mag = 10.0f;
                std::uint32_t dur = 0;
                std::uint32_t area = 0;

                if (std::regex_search(blockStr, magMatch, magRegex)) {
                    mag = std::stof(magMatch[1].str());
                }
                if (std::regex_search(blockStr, durMatch, durRegex)) {
                    dur = static_cast<std::uint32_t>(std::stoul(durMatch[1].str()));
                }
                if (std::regex_search(blockStr, areaMatch, areaRegex)) {
                    area = static_cast<std::uint32_t>(std::stoul(areaMatch[1].str()));
                }

                if (auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(formId)) {
                    params.effects.push_back({ mgef, mag, area, dur });
                    SKSE::log::info("PrismaBridge: Resolved composite effect '{}' (FormID: 0x{:08X}) Mag: {:.1f}, Dur: {}s, Area: {}ft",
                        mgef->GetFullName(), formId, mag, dur, area);
                } else {
                    failedEffectID = idMatch[1].str();
                }
            }
        }

        if (params.effects.empty()) {
            SKSE::log::error("PrismaBridge: Crafting aborted. No valid Magic Effects resolved. Refunding.");
            RefundGold(player, totalGold);
            ShowHUDNotification("Incantation Unstable! Arcane matrix collapsed. Gold refunded.");
            PlaySound("cancel");

            if (!failedEffectID.empty() && s_view != PRISMA_UI_API::kInvalidView && s_prismaUI) {
                std::string jsCall = "if (window.AltarAPI && window.AltarAPI.lockEffect) { window.AltarAPI.lockEffect('" + failedEffectID + "'); }";
                s_prismaUI->Invoke(s_view, jsCall.c_str(), nullptr);
            }
            return false;
        }

        auto* createdSpell = SpellFactory::CreateSpell(params, player);
        if (createdSpell) {
            // Track recipe and live pointer to prevent duplicate stacking on reload
            Serialization::TrackCreatedSpell(params, createdSpell);
            SKSE::log::info("PrismaBridge: Successfully forged spell '{}' (FormID: 0x{:08X}) with {} unique effects.", 
                params.spellName, static_cast<std::uint32_t>(createdSpell->GetFormID()), params.effects.size());
            return true;
        }

        SKSE::log::error("PrismaBridge: Engine failed to allocate SpellItem. Refunding.");
        RefundGold(player, totalGold);
        ShowHUDNotification("Incantation Failed! Form allocation error. Gold refunded.");
        PlaySound("cancel");
        return false;
    }
}