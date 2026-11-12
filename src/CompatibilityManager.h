#pragma once

#include "pch.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace Spellmaking
{
    // Rule configuration for a specific modded spell or effect
    struct SpellCompatibilityRule
    {
        RE::FormID sourceSpellID{ 0 };
        std::string modName;
        std::vector<RE::BGSKeyword*> forcedKeywords;
        float minDuration{ 0.0f };
        bool forceCollidingProjectile{ false };
        bool aliasSourceSpell{ false };
    };

    class CompatibilityManager
    {
    public:
        [[nodiscard]] static CompatibilityManager* GetSingleton()
        {
            static CompatibilityManager singleton;
            return &singleton;
        }

        // Orchestrates automatic heuristic scanning and custom disk INI overrides
        void LoadRules();

        // Queries compatibility rules for a specific primary magic effect or base spell
        [[nodiscard]] const SpellCompatibilityRule* FindRuleForEffect(const RE::EffectSetting* a_mgef) const;
        [[nodiscard]] const SpellCompatibilityRule* FindRuleForSpell(RE::FormID a_formID) const;

        // Resolves the exact authentic parent SpellItem for a player-selected primary effect
        [[nodiscard]] RE::SpellItem* ResolveExactParentSpell(const RE::EffectSetting* a_mgef) const;

        // Explicitly binds a crafted dynamic spell (0xFFxxxxxx) to the exact parent spells of the effects the player chose
        void RegisterCraftedSpellSources(RE::FormID a_dynamicSpellID, const std::vector<RE::FormID>& a_authenticSourceSpells);
        [[nodiscard]] std::vector<RE::FormID> GetCraftedSpellSources(RE::FormID a_dynamicSpellID) const;

        [[nodiscard]] std::size_t GetLoadedRuleCount() const noexcept { return _rules.size(); }

    private:
        CompatibilityManager() = default;
        ~CompatibilityManager() = default;

        CompatibilityManager(const CompatibilityManager&) = delete;
        CompatibilityManager& operator=(const CompatibilityManager&) = delete;

        void BuildAutomaticHeuristicRules();
        void ParseIniFile(const std::filesystem::path& a_path);

        std::unordered_map<RE::FormID, SpellCompatibilityRule> _rules;
        std::unordered_map<RE::FormID, RE::FormID> _effectToParentSpellMap;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> _craftedSpellToAuthenticSources;
    };
}