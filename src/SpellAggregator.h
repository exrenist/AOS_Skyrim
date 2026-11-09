#pragma once

#include "pch.h"
#include <string>
#include <vector>

namespace Spellmaking
{
    // Holds craftable effect metadata along with engine-level minimum hardcaps
    struct DiscoveredEffect
    {
        RE::EffectSetting* effectSetting{ nullptr };
        std::string name;
        std::string school;
        float baseCost{ 0.0f };
        std::string castType;
        std::vector<std::string> allowedTargetTypes;
        std::string description;
        bool supportsArea{ false };
        std::uint32_t defaultArea{ 0 };

        // Mandatory baseline hard-caps harvested from the original parent spell
        float minMagnitude{ 0.0f };
        std::uint32_t minDuration{ 0 };
        std::uint32_t minArea{ 0 };
    };

    class SpellAggregator
    {
    public:
        // Scans player spellbook and extracts effects with their baseline constraints
        [[nodiscard]] static std::vector<DiscoveredEffect> CollectAvailableEffects();

        // Serializes discovered effects into a JSON payload for the PrismaUI frontend
        [[nodiscard]] static std::string BuildEffectsJsonPayload(const std::vector<DiscoveredEffect>& a_effects);
    };
}