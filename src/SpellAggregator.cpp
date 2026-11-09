#include "SpellAggregator.h"
#include <algorithm>
#include <sstream>

namespace Spellmaking
{
    namespace
    {
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

        [[nodiscard]] std::string ResolveFallbackDescription(const std::string& a_name, RE::ActorValue a_school = RE::ActorValue::kNone)
        {
            const auto lowerName = [&]() {
                std::string s = a_name;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            }();

            if (lowerName.find("fire") != std::string::npos || lowerName.find("flame") != std::string::npos) {
                return "Engulfs targets in searing flames, dealing fire damage and causing extra damage while burning.";
            }
            if (lowerName.find("frost") != std::string::npos || lowerName.find("ice") != std::string::npos || lowerName.find("cold") != std::string::npos) {
                return "Strikes targets with biting cold, dealing frost damage to Health and Stamina while slowing movement.";
            }
            if (lowerName.find("shock") != std::string::npos || lowerName.find("lightning") != std::string::npos) {
                return "Discharges electrical lightning, dealing shock damage to Health and half as much to Magicka.";
            }
            if (lowerName.find("heal") != std::string::npos || lowerName.find("restore") != std::string::npos) {
                return "Channels restorative divine light, healing the target's wounds and replenishing vitality.";
            }
            if (lowerName.find("fury") != std::string::npos || lowerName.find("frenzy") != std::string::npos) {
                return "Induces uncontrollable bloodlust, compelling creatures and people to attack anyone nearby.";
            }
            if (lowerName.find("fear") != std::string::npos || lowerName.find("rout") != std::string::npos) {
                return "Strikes primal terror into hearts, compelling targets to flee from combat.";
            }
            if (lowerName.find("calm") != std::string::npos || lowerName.find("pacify") != std::string::npos) {
                return "Soothes agitated targets with peaceful resonance, preventing them from fighting.";
            }
            if (lowerName.find("courage") != std::string::npos || lowerName.find("rally") != std::string::npos) {
                return "Inspires allies with arcane valor, bolstering combat morale.";
            }
            if (lowerName.find("paraly") != std::string::npos) {
                return "Renders the target completely rigid and unable to move or act.";
            }
            if (lowerName.find("invis") != std::string::npos) {
                return "Bends light around the caster, rendering them entirely unseen.";
            }

            if (a_school == RE::ActorValue::kDestruction) {
                return "Weaves destructive arcane forces to annihilate the target.";
            } else if (a_school == RE::ActorValue::kRestoration) {
                return "Channels benevolent restorative energies to heal, fortify, or ward.";
            } else if (a_school == RE::ActorValue::kIllusion) {
                return "Manipulates the target's perception, emotions, and psychic state.";
            } else if (a_school == RE::ActorValue::kConjuration) {
                return "Commands the planes of Oblivion to summon minions or trap souls.";
            } else {
                return "Bends the laws of nature to manipulate matter and physical reality.";
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

                // Strip internal engine helper effects
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

                std::string schoolStr = "Alteration";
                if (mgef->data.primaryAV == RE::ActorValue::kDestruction) {
                    schoolStr = "Destruction";
                } else if (mgef->data.primaryAV == RE::ActorValue::kRestoration) {
                    schoolStr = "Restoration";
                } else if (mgef->data.primaryAV == RE::ActorValue::kIllusion) {
                    schoolStr = "Illusion";
                } else if (mgef->data.primaryAV == RE::ActorValue::kConjuration) {
                    schoolStr = "Conjuration";
                }

                std::string castTypeStr = (mgef->data.castingType == RE::MagicSystem::CastingType::kConcentration)
                    ? "Concentration"
                    : "FireAndForget";

                std::vector<std::string> targetTypes;
                if (mgef->data.delivery == RE::MagicSystem::Delivery::kSelf) {
                    targetTypes.push_back("Self");
                } else {
                    targetTypes.push_back("Aimed");
                }

                std::string finalDesc = mgef->magicItemDescription.c_str();
                if (finalDesc.empty() || finalDesc.find("<") != std::string::npos) {
                    finalDesc = ResolveFallbackDescription(fullName, mgef->data.primaryAV);
                }

                const bool isSingleTargetBolt = (fullName.find("bolt") != std::string::npos) || 
                                                (fullName.find("Spike") != std::string::npos) ||
                                                (fullName.find("Bite") != std::string::npos);

                const bool inherentlyArea = (mgef->data.explosion != nullptr) || 
                                            (fullName.find("Ball") != std::string::npos) ||
                                            (fullName.find("Storm") != std::string::npos) ||
                                            (fullName.find("Rout") != std::string::npos) ||
                                            (fullName.find("Frenzy") != std::string::npos) ||
                                            (fullName.find("Mayhem") != std::string::npos) ||
                                            (fullName.find("Hysteria") != std::string::npos) ||
                                            (fullName.find("Harmony") != std::string::npos);

                const bool supportsArea = !isSingleTargetBolt && (inherentlyArea || effectItem->effectItem.area > 0);

                // Harvest original parameters to enforce as hard-caps
                const float origMag = effectItem->effectItem.magnitude;
                const std::uint32_t origDur = effectItem->effectItem.duration;
                const std::uint32_t origArea = effectItem->effectItem.area;

                DiscoveredEffect discovered;
                discovered.effectSetting = mgef;
                discovered.name = fullName;
                discovered.school = schoolStr;
                discovered.baseCost = mgef->data.baseCost;
                discovered.castType = castTypeStr;
                discovered.allowedTargetTypes = targetTypes;
                discovered.description = finalDesc;
                discovered.supportsArea = supportsArea;
                discovered.defaultArea = inherentlyArea ? (origArea > 0 ? origArea : 15) : 0;

                // Enforce hard-caps: If the original spell required duration for script lifecycle, clamp to at least original duration
                discovered.minMagnitude = origMag > 0.0f ? origMag : 1.0f;
                discovered.minDuration = origDur > 0 ? origDur : 0;
                discovered.minArea = origArea;

                result.push_back(discovered);
            }
        }

        SKSE::log::info("SpellAggregator: Discovered {} pure player-craftable effects with hardcap baselines.", result.size());
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
            // Hard-cap metadata sent to UI sliders
            ss << "\"minMagnitude\":" << fx.minMagnitude << ",";
            ss << "\"minDuration\":" << fx.minDuration << ",";
            ss << "\"minArea\":" << fx.minArea << ",";

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