#include "SpellAggregator.h"
#include <algorithm>
#include <sstream>

namespace Spellmaking
{
    namespace
    {
        // Escapes strings safely for JSON transmission
        std::string EscapeJsonString(const std::string& a_input)
        {
            std::ostringstream ss;
            for (char c : a_input) {
                switch (c) {
                case '"':  ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\b': ss << "\\b";  break;
                case '\f': ss << "\\f";  break;
                case '\n': ss << " ";    break;
                case '\r': break;
                case '\t': ss << " ";    break;
                default:   ss << c;       break;
                }
            }
            return ss.str();
        }

        // Resolves authentic Oblivion-style fallback descriptions
        [[nodiscard]] std::string ResolveFallbackDescription(const std::string& a_name, RE::ActorValue a_school = RE::ActorValue::kNone)
        {
            const auto lowerName = [&]() {
                std::string s = a_name;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            }();

            if (lowerName.find("fire") != std::string::npos || lowerName.find("flame") != std::string::npos || lowerName.find("burn") != std::string::npos) {
                return "Engulfs targets in searing flames, dealing fire damage and causing extra damage while burning.";
            }
            if (lowerName.find("frost") != std::string::npos || lowerName.find("ice") != std::string::npos || lowerName.find("blizzard") != std::string::npos || lowerName.find("freeze") != std::string::npos) {
                return "Strikes targets with biting cold, dealing frost damage to Health and Stamina while slowing movement.";
            }
            if (lowerName.find("shock") != std::string::npos || lowerName.find("lightning") != std::string::npos || lowerName.find("spark") != std::string::npos) {
                return "Discharges electrical lightning, dealing shock damage to Health and half as much to Magicka.";
            }
            if (lowerName.find("heal") != std::string::npos || lowerName.find("restore") != std::string::npos || lowerName.find("cure") != std::string::npos) {
                return "Channels restorative divine light, healing the target's wounds and replenishing vitality.";
            }
            if (lowerName.find("fury") != std::string::npos) {
                return "Induces blind rage, compelling creatures and people up to level 6 to attack anyone nearby.";
            }
            if (lowerName.find("frenzy") != std::string::npos || lowerName.find("mayhem") != std::string::npos) {
                return "Induces uncontrollable bloodlust, compelling creatures and people in the area to attack anyone nearby.";
            }
            if (lowerName.find("fear") != std::string::npos) {
                return "Strikes primal terror into the target's heart, compelling them to flee from combat.";
            }
            if (lowerName.find("rout") != std::string::npos || lowerName.find("hysteria") != std::string::npos) {
                return "Strikes mass panic into hearts, compelling creatures and people in the area to flee from combat.";
            }
            if (lowerName.find("calm") != std::string::npos || lowerName.find("pacify") != std::string::npos || lowerName.find("harmony") != std::string::npos) {
                return "Soothes agitated targets with peaceful resonance, preventing them from fighting.";
            }
            if (lowerName.find("courage") != std::string::npos || lowerName.find("rally") != std::string::npos) {
                return "Inspires allies with arcane valor, preventing them from fleeing and bolstering combat morale.";
            }
            if (lowerName.find("paraly") != std::string::npos) {
                return "Renders the target completely rigid and unable to move or act for the duration.";
            }
            if (lowerName.find("invis") != std::string::npos) {
                return "Bends light around the caster, rendering them entirely unseen until interaction.";
            }
            if (lowerName.find("trap") != std::string::npos) {
                return "Weaves an ethereal tether, capturing the target's soul in an empty gem if slain.";
            }
            if (lowerName.find("ward") != std::string::npos) {
                return "Manifests an arcane aegis that absorbs incoming spell damage and fortifies armor.";
            }

            if (a_school == RE::ActorValue::kDestruction) {
                return "Weaves destructive arcane forces to annihilate the target.";
            } else if (a_school == RE::ActorValue::kRestoration) {
                return "Channels benevolent restorative energies to heal, fortify, or ward.";
            } else if (a_school == RE::ActorValue::kIllusion) {
                return "Manipulates the target's perception, emotions, and psychic state.";
            } else if (a_school == RE::ActorValue::kConjuration) {
                return "Commands the planes of Oblivion to summon minions, bound weapons, or trap souls.";
            } else {
                return "Bends the laws of nature to manipulate matter, armor, and physical reality.";
            }
        }
    }

    std::vector<DiscoveredEffect> SpellAggregator::CollectAvailableEffects()
    {
        std::vector<DiscoveredEffect> result;
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        if (!player || !dataHandler) {
            return result;
        }

        std::vector<RE::EffectSetting*> processedMGEFs;
        const auto& allSpells = dataHandler->GetFormArray<RE::SpellItem>();

        for (auto* spell : allSpells) {
            if (!spell || spell->data.spellType != RE::MagicSystem::SpellType::kSpell) {
                continue;
            }

            if (!player->HasSpell(spell)) {
                continue;
            }

            for (auto* effectItem : spell->effects) {
                if (!effectItem || !effectItem->baseEffect) {
                    continue;
                }

                auto* mgef = effectItem->baseEffect;

                // Eliminate engine internal helper effects (kHideInUI)
                if (mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kHideInUI)) {
                    continue;
                }

                std::string fullName = mgef->GetFullName();
                if (fullName.empty()) {
                    continue;
                }

                if (fullName.find("Secondary") != std::string::npos ||
                    fullName.find("Dummy") != std::string::npos ||
                    fullName.find("Marker") != std::string::npos ||
                    fullName.find("Script") != std::string::npos ||
                    fullName.find("Trigger") != std::string::npos) {
                    continue;
                }

                if (std::find(processedMGEFs.begin(), processedMGEFs.end(), mgef) != processedMGEFs.end()) {
                    continue;
                }
                processedMGEFs.push_back(mgef);

                // Multi-Tier Magic School Resolution (Fix for Apocalypse & Modded Spells)
                const auto primaryAV = mgef->data.primaryAV;
                const auto spellSkill = spell->GetAssociatedSkill();

                std::string schoolStr = "Alteration";

                if (primaryAV == RE::ActorValue::kDestruction || spellSkill == RE::ActorValue::kDestruction) {
                    schoolStr = "Destruction";
                } else if (primaryAV == RE::ActorValue::kRestoration || spellSkill == RE::ActorValue::kRestoration) {
                    schoolStr = "Restoration";
                } else if (primaryAV == RE::ActorValue::kIllusion || spellSkill == RE::ActorValue::kIllusion) {
                    schoolStr = "Illusion";
                } else if (primaryAV == RE::ActorValue::kConjuration || spellSkill == RE::ActorValue::kConjuration) {
                    schoolStr = "Conjuration";
                } else if (primaryAV == RE::ActorValue::kAlteration || spellSkill == RE::ActorValue::kAlteration) {
                    schoolStr = "Alteration";
                } else {
                    // Deep Keyword Inspection for complex scripted archetype effects (Apocalypse, Mysticism, CC)
                    if (spell->HasKeywordString("MagicDestruction") || spell->HasKeywordString("MagicDamageFire") ||
                        spell->HasKeywordString("MagicDamageFrost") || spell->HasKeywordString("MagicDamageShock") ||
                        spell->HasKeywordString("WB_Destruction") || mgef->HasKeywordString("MagicDestruction") ||
                        mgef->HasKeywordString("MagicDamageFire") || mgef->HasKeywordString("MagicDamageFrost") ||
                        mgef->HasKeywordString("MagicDamageShock") || mgef->HasKeywordString("WB_Destruction")) {
                        schoolStr = "Destruction";
                    } else if (spell->HasKeywordString("MagicRestoration") || mgef->HasKeywordString("MagicRestoration") ||
                               spell->HasKeywordString("WB_Restoration") || mgef->HasKeywordString("WB_Restoration")) {
                        schoolStr = "Restoration";
                    } else if (spell->HasKeywordString("MagicConjuration") || mgef->HasKeywordString("MagicConjuration") ||
                               spell->HasKeywordString("WB_Conjuration") || mgef->HasKeywordString("WB_Conjuration")) {
                        schoolStr = "Conjuration";
                    } else if (spell->HasKeywordString("MagicIllusion") || mgef->HasKeywordString("MagicIllusion") ||
                               spell->HasKeywordString("WB_Illusion") || mgef->HasKeywordString("WB_Illusion")) {
                        schoolStr = "Illusion";
                    } else {
                        schoolStr = "Alteration";
                    }
                }

                std::string castTypeStr = (mgef->data.castingType == RE::MagicSystem::CastingType::kConcentration)
                    ? "Concentration"
                    : "FireAndForget";

                // Strict Delivery: Self or Aimed
                std::vector<std::string> targetTypes;
                if (mgef->data.delivery == RE::MagicSystem::Delivery::kSelf) {
                    targetTypes.push_back("Self");
                } else {
                    targetTypes.push_back("Aimed");
                }

                std::string finalDesc = mgef->magicItemDescription.c_str();
                if (finalDesc.empty() || finalDesc.find("<") != std::string::npos) {
                    finalDesc = ResolveFallbackDescription(fullName, primaryAV);
                }

                // Projectile and Area capabilities
                const bool isSingleTargetBolt = (fullName.find("bolt") != std::string::npos) || 
                                                (fullName.find("Spike") != std::string::npos) ||
                                                (fullName.find("Bite") != std::string::npos);

                const bool isFury = (fullName.find("Fury") != std::string::npos);

                const bool inherentlyArea = !isFury && ((mgef->data.explosion != nullptr) || 
                                            (fullName.find("Ball") != std::string::npos) ||
                                            (fullName.find("Storm") != std::string::npos) ||
                                            (fullName.find("Rout") != std::string::npos) ||
                                            (fullName.find("Frenzy") != std::string::npos) ||
                                            (fullName.find("Mayhem") != std::string::npos) ||
                                            (fullName.find("Hysteria") != std::string::npos) ||
                                            (fullName.find("Harmony") != std::string::npos));

                const bool supportsArea = !isSingleTargetBolt && !isFury && (inherentlyArea || effectItem->effectItem.area > 0);

                DiscoveredEffect discovered;
                discovered.effectSetting = mgef;
                discovered.name = fullName;
                discovered.school = schoolStr;
                discovered.baseCost = mgef->data.baseCost;
                discovered.castType = castTypeStr;
                discovered.allowedTargetTypes = targetTypes;
                discovered.description = finalDesc;
                discovered.supportsArea = supportsArea;
                discovered.defaultArea = inherentlyArea ? (effectItem->effectItem.area > 0 ? effectItem->effectItem.area : 15) : 0;

                result.push_back(discovered);
            }
        }

        SKSE::log::info("SpellAggregator: Discovered {} pure player-craftable effects with descriptions.", result.size());
        return result;
    }

    std::string SpellAggregator::BuildEffectsJsonPayload(const std::vector<DiscoveredEffect>& a_effects)
    {
        std::ostringstream ss;
        ss << "[";

        for (std::size_t i = 0; i < a_effects.size(); ++i) {
            const auto& fx = a_effects[i];
            ss << "{";
            ss << "\"id\":\"0x" << std::hex << std::uppercase << fx.effectSetting->GetFormID() << std::dec << "\",";
            ss << "\"name\":\"" << EscapeJsonString(fx.name) << "\",";
            ss << "\"school\":\"" << fx.school << "\",";
            ss << "\"baseCost\":" << fx.baseCost << ",";
            ss << "\"castType\":\"" << fx.castType << "\",";
            ss << "\"description\":\"" << EscapeJsonString(fx.description) << "\",";
            ss << "\"supportsArea\":" << (fx.supportsArea ? "true" : "false") << ",";
            ss << "\"defaultArea\":" << fx.defaultArea << ",";

            ss << "\"allowedTargetTypes\":[";
            for (std::size_t j = 0; j < fx.allowedTargetTypes.size(); ++j) {
                ss << "\"" << fx.allowedTargetTypes[j] << "\"";
                if (j + 1 < fx.allowedTargetTypes.size()) {
                    ss << ",";
                }
            }
            ss << "]";

            ss << "}";
            if (i + 1 < a_effects.size()) {
                ss << ",";
            }
        }

        ss << "]";
        return ss.str();
    }
}