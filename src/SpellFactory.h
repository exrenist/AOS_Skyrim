#pragma once

#include "pch.h"
#include <string>
#include <vector>

namespace Spellmaking
{
    // Configuration parameters for an individual effect in a composite custom spell
    struct CustomEffectParam
    {
        union {
            RE::EffectSetting* effectSetting{ nullptr };
            RE::EffectSetting* setting;
        };

        float magnitude{ 0.0f };
        std::uint32_t area{ 0 };
        std::uint32_t duration{ 0 };

        CustomEffectParam() :
            effectSetting(nullptr), magnitude(0.0f), area(0), duration(0) {}

        CustomEffectParam(RE::EffectSetting* a_mgef, float a_mag, std::uint32_t a_area, std::uint32_t a_dur) :
            effectSetting(a_mgef), magnitude(a_mag), area(a_area), duration(a_dur) {}
    };

    // Blueprint specifications for dynamic spell assembly
    struct SpellCreationParams
    {
        std::string spellName{ "Custom Incantation" };
        RE::MagicSystem::Delivery deliveryType{ RE::MagicSystem::Delivery::kAimed };
        RE::MagicSystem::CastingType castingType{ RE::MagicSystem::CastingType::kFireAndForget };
        float chargeTime{ 0.5f };
        std::vector<CustomEffectParam> effects;
    };

    class SpellFactory
    {
    public:
        // Dynamically instantiates a composite SpellItem in memory and binds it to the player
        [[nodiscard]] static RE::SpellItem* CreateSpell(const SpellCreationParams& a_params, RE::PlayerCharacter* a_player);

        // Calculates vanilla-balanced magicka casting cost matching engine scale
        [[nodiscard]] static std::uint32_t CalculateCost(RE::EffectSetting* a_mgef, float a_magnitude, std::uint32_t a_duration, std::uint32_t a_area);

    private:
        // Performs a deep-copy of condition items from source effect to target effect
        static void CloneConditions(RE::TESCondition& a_dest, const RE::TESCondition& a_src);

        // Inherits keywords from base magic effects so perk mechanics (e.g. Apocalypse) trigger properly
        static void InheritKeywords(RE::SpellItem* a_spell, const std::vector<CustomEffectParam>& a_effects);
    };
}