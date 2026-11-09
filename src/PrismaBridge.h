#pragma once

#include "pch.h"
#include "PrismaUI_API.h"
#include "SpellFactory.h"
#include <string>
#include <cstdint>

namespace Spellmaking
{
    class PrismaBridge
    {
    public:
        // Lifecycle methods
        static void InitializeAPI();
        static void Register();

        // Native Papyrus endpoint - Returns bool indicating successful interface instantiation
        static bool OpenMenu(std::int32_t a_playerGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka);
        static bool OpenAltarMenu(std::int32_t a_playerGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka);
        static void CloseMenu();

        // PrismaUI event hooks
        static void OnDomReady(PrismaView a_view);
        static void OnCraftSpellDispatched(const char* a_payload);
        static void OnCloseMenuRequested(const char* a_payload);
        static void OnPlayUISoundDispatched(const char* a_soundName);

        // Fail-safe notification and sound triggers
        static void PlaySound(const std::string& a_soundName);
        static void ShowHUDNotification(const char* a_message);

    private:
        // Crafting payload parser and execution logic
        [[nodiscard]] static bool ParseAndExecuteCrafting(const std::string& a_jsonPayload);
        static std::string BuildPlayerDataJson(std::int32_t a_gold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka);
        static void RefundGold(RE::PlayerCharacter* a_player, std::int32_t a_amount);

        // Internal static state
        inline static PRISMA_UI_API::IVPrismaUI1* s_prismaUI{ nullptr };
        inline static PrismaView s_view{ PRISMA_UI_API::kInvalidView };
        inline static constexpr const char* kViewPath{ "AltarOfSpellmaking/index.html" };

        inline static std::string s_cachedPlayerDataJson;
        inline static std::string s_cachedEffectsJson;
    };
}