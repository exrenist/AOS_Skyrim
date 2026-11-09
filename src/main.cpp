#include "pch.h"
#include "Serialization.h"
#include "PapyrusInterface.h"
#include "PrismaBridge.h"
#include "MagicDiagnostics.h"

// Lifecycle listener handling SKSE engine messages
void OnMessage(SKSE::MessagingInterface::Message* a_message)
{
    switch (a_message->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        // Bind PrismaUI runtime API once game data is fully loaded
        Spellmaking::PrismaBridge::InitializeAPI();
        break;

    case SKSE::MessagingInterface::kPostLoadGame:
        // Safely instantiate custom spells after .ess file finishes loading
        Spellmaking::Serialization::ApplyPendingSpells();
        break;

    case SKSE::MessagingInterface::kNewGame:
        // Clear registries on new playthrough
        Spellmaking::Serialization::OnNewGame();
        break;

    default:
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    if (!a_skse) {
        return false;
    }

    SKSE::Init(a_skse);

    SKSE::log::info("AOS_Skyrim v1-0-0-0 initializing...");

    // Register clean cosave serialization handlers
    Spellmaking::Serialization::Register();

    // Register messaging interface listener
    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("Altar of Spellmaking: Failed to register messaging interface.");
        return false;
    }

    // Register native Papyrus functions bridge
    const auto papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus || !papyrus->Register(Spellmaking::PapyrusInterface::RegisterFunctions)) {
        SKSE::log::error("Altar of Spellmaking: Failed to register Papyrus interface.");
        return false;
    }

    // Register diagnostics
    Spellmaking::MagicDiagnostics::Register();

    SKSE::log::info("Altar of Spellmaking: SKSE plugin successfully initialized.");
    return true;
}