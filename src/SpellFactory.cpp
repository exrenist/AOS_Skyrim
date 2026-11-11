#include "SpellFactory.h"
#include <cmath>
#include <algorithm>
#include <vector>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace Spellmaking
{
    namespace
    {
        [[nodiscard]] RE::TESBoundObject* ResolveMenuDisplayObject(RE::ActorValue a_school)
        {
            RE::FormID modelFormID = 0x0004944A; // Default: MAGInvRestoration

            switch (a_school) {
            case RE::ActorValue::kDestruction:
                modelFormID = 0x00049448;
                break;
            case RE::ActorValue::kAlteration:
                modelFormID = 0x00109AC2;
                break;
            case RE::ActorValue::kIllusion:
                modelFormID = 0x00049449;
                break;
            case RE::ActorValue::kConjuration:
                modelFormID = 0x00049447;
                break;
            case RE::ActorValue::kRestoration:
            default:
                modelFormID = 0x0004944A;
                break;
            }

            return RE::TESForm::LookupByID<RE::TESBoundObject>(modelFormID);
        }

        [[nodiscard]] bool HasExplosion(const RE::EffectSetting* a_mgef)
        {
            return a_mgef && (a_mgef->data.explosion != nullptr);
        }

        [[nodiscard]] bool HasValidProjectile(const RE::EffectSetting* a_mgef)
        {
            return a_mgef && (a_mgef->data.projectileBase != nullptr);
        }

        [[nodiscard]] RE::BGSProjectile* ResolveAuthenticCollidingProjectile(RE::ActorValue a_school, const std::string& a_name)
        {
            RE::FormID projFormID = 0x00012FD0; // MAG_DestructionBolt_Proj

            const std::string lower = [&]() {
                std::string s = a_name;
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return s;
            }();

            if (lower.find("lightning") != std::string::npos || lower.find("shock") != std::string::npos || lower.find("spark") != std::string::npos) {
                projFormID = 0x0003BEE9; // LightningBoltProjectile
            } else if (lower.find("fire") != std::string::npos || lower.find("flame") != std::string::npos) {
                projFormID = 0x00012F04; // FireboltProjectile
            } else if (lower.find("frost") != std::string::npos || lower.find("ice") != std::string::npos || lower.find("cold") != std::string::npos) {
                projFormID = 0x0003BEF0; // IceSpikeProjectile
            }

            return RE::TESForm::LookupByID<RE::BGSProjectile>(projFormID);
        }

        // Safely resolves the authentic EitherHand BGSEquipSlot from vanilla player spells
        [[nodiscard]] RE::BGSEquipSlot* ResolveEitherHandEquipSlot()
        {
            // Query vanilla Flames (0x00012FCD), Healing (0x00012FCC), or Firebolt (0x00012F03)
            static constexpr RE::FormID kVanillaHandSpells[] = { 0x00012FCD, 0x00012FCC, 0x00012F03 };
            for (const auto spellID : kVanillaHandSpells) {
                if (auto* vanillaSpell = RE::TESForm::LookupByID<RE::SpellItem>(spellID)) {
                    if (vanillaSpell->equipSlot) {
                        return vanillaSpell->equipSlot;
                    }
                }
            }

            // Fallback: Skyrim.esm EitherHand EquipSlot FormID is 0x00013F44
            return RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F44);
        }
    }

    void SpellFactory::CloneConditions(RE::TESCondition& a_dest, const RE::TESCondition& a_src)
    {
        auto* srcItem = a_src.head;
        RE::TESConditionItem** destTail = &a_dest.head;

        while (srcItem) {
            // Function ID: 53 = GetSpell, 89 = GetEquippedSpell
            const auto rawFunctionID = static_cast<std::uint32_t>(srcItem->data.functionData.function.get());
            const bool isSpellSelfLock = (rawFunctionID == 53 || rawFunctionID == 89);

            if (!isSpellSelfLock) {
                auto* newItem = new RE::TESConditionItem();
                newItem->data = srcItem->data;
                newItem->next = nullptr;

                *destTail = newItem;
                destTail = &newItem->next;
            }

            srcItem = srcItem->next;
        }
    }

    void SpellFactory::InheritKeywords(RE::SpellItem* a_spell, const std::vector<CustomEffectParam>& a_effects)
    {
        if (!a_spell) {
            return;
        }

        std::vector<RE::BGSKeyword*> collectedKeywords;

        for (const auto& fx : a_effects) {
            if (!fx.effectSetting) {
                continue;
            }

            const auto numKw = fx.effectSetting->GetNumKeywords();
            for (std::uint32_t i = 0; i < numKw; ++i) {
                if (const auto optKw = fx.effectSetting->GetKeywordAt(i); optKw.has_value()) {
                    auto* kw = optKw.value();
                    if (kw && std::find(collectedKeywords.begin(), collectedKeywords.end(), kw) == collectedKeywords.end()) {
                        collectedKeywords.push_back(kw);
                    }
                }
            }
        }

        if (!collectedKeywords.empty()) {
            auto** kwArray = RE::calloc<RE::BGSKeyword*>(collectedKeywords.size());
            for (std::size_t i = 0; i < collectedKeywords.size(); ++i) {
                kwArray[i] = collectedKeywords[i];
            }
            a_spell->keywords = kwArray;
            a_spell->numKeywords = static_cast<std::uint32_t>(collectedKeywords.size());
        }
    }

    std::uint32_t SpellFactory::CalculateCost(RE::EffectSetting* a_mgef, float a_magnitude, std::uint32_t a_duration, std::uint32_t a_area)
    {
        if (!a_mgef) {
            return 10;
        }

        const float baseCost = (std::max)(1.0f, a_mgef->data.baseCost);
        const float magFactor = (a_magnitude > 0.0f && !a_mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoMagnitude)) 
            ? std::pow(a_magnitude, 1.1f) : 1.0f;
        const float durFactor = (a_duration > 0 && !a_mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoDuration)) 
            ? std::pow(static_cast<float>(a_duration) / 10.0f, 1.1f) : 1.0f;
        const float areaFactor = (a_area > 0 && !a_mgef->data.flags.any(RE::EffectSetting::EffectSettingData::Flag::kNoArea)) 
            ? (static_cast<float>(a_area) * 0.1f) : 1.0f;

        constexpr float kMagicCostScale = 0.5f;
        const float calculated = baseCost * magFactor * durFactor * areaFactor * kMagicCostScale;

        return (std::max)(10u, static_cast<std::uint32_t>(std::round(calculated)));
    }

    RE::SpellItem* SpellFactory::CreateSpell(const SpellCreationParams& a_params, RE::PlayerCharacter* a_player)
    {
        if (a_params.effects.empty() || !a_player) {
            SKSE::log::error("SpellFactory: Cannot instantiate spell - invalid arguments.");
            return nullptr;
        }

        auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
        if (!factory) {
            SKSE::log::error("SpellFactory: Failed to query SpellItem IFormFactory.");
            return nullptr;
        }

        auto* spell = factory->Create();
        if (!spell) {
            SKSE::log::error("SpellFactory: Memory allocation failed for SpellItem.");
            return nullptr;
        }

        spell->formFlags |= RE::TESForm::RecordFlags::kInitialized;
        spell->fullName = a_params.spellName.c_str();
        spell->data.spellType = RE::MagicSystem::SpellType::kSpell;
        spell->data.delivery = a_params.deliveryType;
        spell->data.castingType = a_params.castingType;
        spell->data.chargeTime = a_params.chargeTime;
        spell->data.castingPerk = nullptr;

        // Assign genuine EitherHand equip slot so both Left ("L") and Right ("R") hands work in FavoritesMenu
        spell->equipSlot = ResolveEitherHandEquipSlot();

        auto sortedEffects = a_params.effects;
        std::stable_sort(sortedEffects.begin(), sortedEffects.end(), [](const CustomEffectParam& a, const CustomEffectParam& b) {
            const bool aExplosive = HasExplosion(a.effectSetting);
            const bool bExplosive = HasExplosion(b.effectSetting);
            if (aExplosive != bExplosive) {
                return aExplosive > bExplosive;
            }

            const bool aProj = HasValidProjectile(a.effectSetting);
            const bool bProj = HasValidProjectile(b.effectSetting);
            if (aProj != bProj) {
                return aProj > bProj;
            }

            return a.area > b.area;
        });

        auto* primaryMGEF = sortedEffects[0].effectSetting;
        const auto primarySchool = primaryMGEF ? primaryMGEF->data.primaryAV : RE::ActorValue::kDestruction;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();

        // 1. Inherit 3D inventory orb model
        RE::SpellItem* parentSpell = nullptr;
        if (dataHandler) {
            for (auto* s : dataHandler->GetFormArray<RE::SpellItem>()) {
                if (!s || s->effects.empty()) continue;
                for (auto* eff : s->effects) {
                    if (eff && eff->baseEffect == primaryMGEF) {
                        parentSpell = s;
                        break;
                    }
                }
                if (parentSpell) break;
            }
        }

        if (parentSpell && parentSpell->menuDispObject) {
            spell->menuDispObject = parentSpell->menuDispObject;
        } else {
            spell->menuDispObject = ResolveMenuDisplayObject(primarySchool);
        }

        // 2. Resolve Master Projectile
        RE::BGSProjectile* masterProjectile = nullptr;
        if (spell->data.delivery == RE::MagicSystem::Delivery::kAimed) {
            if (HasValidProjectile(primaryMGEF)) {
                masterProjectile = primaryMGEF->data.projectileBase;
            } else {
                masterProjectile = ResolveAuthenticCollidingProjectile(primarySchool, primaryMGEF ? primaryMGEF->GetFullName() : "");
            }

            if (primaryMGEF && masterProjectile) {
                primaryMGEF->data.projectileBase = masterProjectile;
            }
        }

        // 3. Keyword Harvesting & Explicit Injections
        std::vector<RE::BGSKeyword*> aggregatedKeywords;
        if (dataHandler) {
            for (const auto& fx : sortedEffects) {
                if (!fx.effectSetting) continue;

                auto* mgefKw = static_cast<RE::BGSKeywordForm*>(fx.effectSetting);
                if (mgefKw && mgefKw->keywords) {
                    for (std::uint32_t k = 0; k < mgefKw->numKeywords; ++k) {
                        if (mgefKw->keywords[k] && std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), mgefKw->keywords[k]) == aggregatedKeywords.end()) {
                            aggregatedKeywords.push_back(mgefKw->keywords[k]);
                        }
                    }
                }

                for (auto* s : dataHandler->GetFormArray<RE::SpellItem>()) {
                    if (!s || s->effects.empty()) continue;
                    for (auto* eff : s->effects) {
                        if (eff && eff->baseEffect == fx.effectSetting) {
                            auto* srcKw = static_cast<RE::BGSKeywordForm*>(s);
                            if (srcKw && srcKw->keywords) {
                                for (std::uint32_t k = 0; k < srcKw->numKeywords; ++k) {
                                    if (srcKw->keywords[k] && std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), srcKw->keywords[k]) == aggregatedKeywords.end()) {
                                        aggregatedKeywords.push_back(srcKw->keywords[k]);
                                    }
                                }
                            }
                            break;
                        }
                    }
                }
            }

            for (const auto& fx : sortedEffects) {
                if (!fx.effectSetting) continue;
                const std::string name = fx.effectSetting->GetFullName();

                if (name.find("Lightning") != std::string::npos || name.find("Shock") != std::string::npos || name.find("Spark") != std::string::npos) {
                    if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00046B9C)) {
                        if (std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), kw) == aggregatedKeywords.end()) aggregatedKeywords.push_back(kw);
                    }
                }
                if (name.find("Fire") != std::string::npos || name.find("Flame") != std::string::npos) {
                    if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00046B99)) {
                        if (std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), kw) == aggregatedKeywords.end()) aggregatedKeywords.push_back(kw);
                    }
                }
                if (name.find("Frost") != std::string::npos || name.find("Cold") != std::string::npos || name.find("Ice") != std::string::npos) {
                    if (auto* kw = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00046B9A)) {
                        if (std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), kw) == aggregatedKeywords.end()) aggregatedKeywords.push_back(kw);
                    }
                }
            }
        }

        if (!aggregatedKeywords.empty()) {
            auto* dstKw = static_cast<RE::BGSKeywordForm*>(static_cast<RE::MagicItem*>(spell));
            auto** kwBuffer = RE::calloc<RE::BGSKeyword*>(aggregatedKeywords.size());
            for (std::size_t i = 0; i < aggregatedKeywords.size(); ++i) {
                kwBuffer[i] = aggregatedKeywords[i];
            }
            dstKw->keywords = kwBuffer;
            dstKw->numKeywords = static_cast<std::uint32_t>(aggregatedKeywords.size());
        }

        // 4. Resolve Master Blast Area
        std::uint32_t masterBlastArea = 0;
        for (const auto& fx : sortedEffects) {
            if (HasExplosion(fx.effectSetting) && fx.area == 0) {
                masterBlastArea = (std::max)(masterBlastArea, 15u);
            } else if (fx.area > 0) {
                masterBlastArea = (std::max)(masterBlastArea, fx.area);
            }
        }

        std::uint32_t totalActualCost = 0;
        std::size_t effectIndex = 0;

        for (const auto& fx : sortedEffects) {
            if (!fx.effectSetting) continue;

            float minMag = 0.0f;
            std::uint32_t minDur = 0;
            std::uint32_t minArea = 0;

            if (dataHandler) {
                for (auto* s : dataHandler->GetFormArray<RE::SpellItem>()) {
                    if (!s || s->effects.empty()) continue;
                    for (auto* eff : s->effects) {
                        if (eff && eff->baseEffect == fx.effectSetting) {
                            minMag = eff->effectItem.magnitude;
                            minDur = eff->effectItem.duration;
                            minArea = eff->effectItem.area;
                            break;
                        }
                    }
                    if (minMag > 0.0f || minDur > 0) break;
                }
            }

            const float clampedMagnitude = (std::max)(fx.magnitude, minMag > 0.0f ? minMag : 1.0f);
            const std::uint32_t clampedDuration = (std::max)(fx.duration, minDur);
            const std::uint32_t clampedArea = (std::max)(fx.area, minArea);

            auto* effectItem = new RE::Effect();
            effectItem->baseEffect = fx.effectSetting;
            effectItem->effectItem.magnitude = clampedMagnitude;
            effectItem->effectItem.duration = clampedDuration;

            if (masterBlastArea > 0 && clampedArea == 0) {
                effectItem->effectItem.area = masterBlastArea;
            } else {
                effectItem->effectItem.area = clampedArea;
            }

            if (spell->data.delivery == RE::MagicSystem::Delivery::kAimed && masterProjectile && !fx.effectSetting->data.projectileBase) {
                fx.effectSetting->data.projectileBase = masterProjectile;
            }

            CloneConditions(effectItem->conditions, fx.effectSetting->conditions);

            const std::uint32_t singleCost = CalculateCost(fx.effectSetting, clampedMagnitude, clampedDuration, effectItem->effectItem.area);

            if (effectIndex == 0) {
                effectItem->cost = static_cast<float>(singleCost + 1);
                totalActualCost += singleCost;
            } else {
                effectItem->cost = static_cast<float>(singleCost);
                totalActualCost += static_cast<std::uint32_t>(std::round(static_cast<float>(singleCost) * (effectIndex == 1 ? 0.35f : 0.20f)));
            }

            spell->effects.push_back(effectItem);
            effectIndex++;
        }

        // 5. Universal Companion Effect Preserver (Apocalypse / Modded Spells)
        if (dataHandler) {
            for (const auto& fx : sortedEffects) {
                if (!fx.effectSetting) continue;

                for (auto* s : dataHandler->GetFormArray<RE::SpellItem>()) {
                    if (!s || s->effects.size() <= 1) continue;

                    bool belongsToThisSpell = false;
                    for (auto* eff : s->effects) {
                        if (eff && eff->baseEffect == fx.effectSetting) {
                            belongsToThisSpell = true;
                            break;
                        }
                    }

                    if (belongsToThisSpell) {
                        for (auto* companionEff : s->effects) {
                            if (!companionEff || !companionEff->baseEffect || companionEff->baseEffect == fx.effectSetting) {
                                continue;
                            }

                            bool alreadyAdded = false;
                            for (auto* existing : spell->effects) {
                                if (existing && existing->baseEffect == companionEff->baseEffect) {
                                    alreadyAdded = true;
                                    break;
                                }
                            }

                            if (!alreadyAdded) {
                                auto* clonedCompanion = new RE::Effect();
                                clonedCompanion->baseEffect = companionEff->baseEffect;
                                clonedCompanion->effectItem.magnitude = companionEff->effectItem.magnitude;
                                clonedCompanion->effectItem.duration = companionEff->effectItem.duration;
                                clonedCompanion->effectItem.area = companionEff->effectItem.area;
                                clonedCompanion->cost = 0.0f;

                                if (spell->data.delivery == RE::MagicSystem::Delivery::kAimed && masterProjectile && !companionEff->baseEffect->data.projectileBase) {
                                    companionEff->baseEffect->data.projectileBase = masterProjectile;
                                }

                                CloneConditions(clonedCompanion->conditions, companionEff->conditions);
                                spell->effects.push_back(clonedCompanion);

                                SKSE::log::info("SpellFactory: Attached companion script effect '{}' (0x{:08X}) from source spell '{}'.",
                                    companionEff->baseEffect->GetFullName(), companionEff->baseEffect->GetFormID(), s->GetFullName());
                            }
                        }
                        break;
                    }
                }
            }
        }

        const auto minSpellBaseCost = static_cast<std::uint32_t>((std::max)(10.0f, sortedEffects[0].effectSetting->data.baseCost));
        spell->data.costOverride = (std::max)(minSpellBaseCost, totalActualCost);

        a_player->AddSpell(spell);

        SKSE::log::info("SpellFactory: Successfully forged composite spell '{}' (FormID: 0x{:08X}) with {} effects, equipSlot: {:08X}, Balanced Cost: {}",
            a_params.spellName, static_cast<std::uint32_t>(spell->GetFormID()), spell->effects.size(),
            spell->equipSlot ? spell->equipSlot->GetFormID() : 0, spell->data.costOverride);

        return spell;
    }
}