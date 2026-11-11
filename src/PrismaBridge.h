#pragma once

#include "pch.h"
#include "PrismaUI_API.h"
#include <string>
#include <cstdint>

namespace Spellmaking
{
    // Alias PrismaView from the global scope to guarantee zero namespace resolution drift
    using PrismaView = ::PrismaView;

    class PrismaBridge
    {
    public:
        // Unique relative path to the HTML entrypoint
        static constexpr const char* kViewPath = "AltarOfSpellmaking/index.html";

        // Initializes API pointer and pre-allocates view handle during kDataLoaded
        static bool InitializeAPI();

        // Dual endpoints guaranteeing seamless dispatch whether called from Papyrus or internal C++
        static bool OpenMenu(std::int32_t a_currentGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka);
        static bool OpenAltarMenu(std::int32_t a_currentGold, std::int32_t a_maxMagicka, std::int32_t a_currentMagicka);

        // Closes the interface, releases focus, and restores game controls
        static void CloseAltarMenu();
        static void CloseMenu();

        // Native engine audio playback using Skyrim's BSAudioManager
        static void PlayUIAudio(const std::string& a_soundName);

        // WebKit DOM lifecycle callback enforcing initial double-lock hidden state
        static void OnDomReady(PrismaView a_view);

        // Callback handlers invoked by WebKit JS event dispatches
        static void HandleCraftSpellPayload(const std::string& a_jsonPayload);
        static void HandleDeconstructSpellPayload(const std::string& a_jsonPayload);

    private:
        // Hardware-accelerated PrismaUI view handle
        static inline PrismaView s_altarView{ PRISMA_UI_API::kInvalidView };

        // Native interface pointer to PrismaUI runtime engine
        static inline PRISMA_UI_API::IVPrismaUI1* s_prismaUI{ nullptr };

        // Active display state guard
        static inline bool s_isMenuOpen{ false };

        // Helper to display native Skyrim HUD notification without binary drift
        static void ShowHUDMessage(const char* a_message);
    };
}