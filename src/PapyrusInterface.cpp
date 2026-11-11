#include "PapyrusInterface.h"
#include "PrismaBridge.h"
#include "SpellAggregator.h"
#include "SpellFactory.h"
#include "Serialization.h"

namespace Spellmaking
{
    bool PapyrusInterface::OpenAltarMenu(
        RE::StaticFunctionTag*,
        std::int32_t a_gold,
        std::int32_t a_maxMagicka,
        std::int32_t a_currentMagicka)
    {
        return PrismaBridge::OpenMenu(a_gold, a_maxMagicka, a_currentMagicka);
    }

    bool PapyrusInterface::QuickTestCraftSpell(RE::StaticFunctionTag*)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            return false;
        }

        const auto availableEffects = SpellAggregator::CollectAvailableEffects();
        if (availableEffects.empty()) {
            return false;
        }

        SpellCreationParams testParams;
        testParams.spellName = "Altar Test Spell";
        testParams.deliveryType = RE::MagicSystem::Delivery::kAimed;
        testParams.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        testParams.chargeTime = 0.5f;
        testParams.effects.emplace_back(availableEffects[0].effectSetting, 25.0f, 0u, 5u);

        auto* spell = SpellFactory::CreateSpell(testParams, player);
        if (spell) {
            Serialization::TrackCreatedSpell(testParams, spell);
            return true;
        }

        return false;
    }

    bool PapyrusInterface::RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm)
    {
        if (!a_vm) {
            return false;
        }

        constexpr auto scriptName = "AltarOfSpellmaking"sv;

        a_vm->RegisterFunction("OpenAltarMenu"sv, scriptName, OpenAltarMenu);
        a_vm->RegisterFunction("QuickTestCraftSpell"sv, scriptName, QuickTestCraftSpell);

        SKSE::log::info("[PapyrusInterface] Successfully bound native functions for '{}'.", scriptName);
        return true;
    }
}