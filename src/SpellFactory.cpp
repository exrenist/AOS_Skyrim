#include "SpellFactory.h"
#include "CompatibilityManager.h"
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

        [[nodiscard]] bool HasAuthenticCollidingProjectile(const RE::EffectSetting* a_mgef)
        {
            if (!a_mgef || !a_mgef->data.projectileBase) {
                return false;
            }
            if (a_mgef->data.archetype == RE::EffectArchetypes::ArchetypeID::kScript) {
                return false;
            }
            return true;
        }

        [[nodiscard]] RE::BGSEquipSlot* ResolveEitherHandEquipSlot()
        {
            static constexpr RE::FormID kVanillaHandSpells[] = { 0x00012FCD, 0x00012FCC, 0x00012F03 };
            for (const auto spellID : kVanillaHandSpells) {
                if (auto* vanillaSpell = RE::TESForm::LookupByID<RE::SpellItem>(spellID)) {
                    if (vanillaSpell->equipSlot) {
                        return vanillaSpell->equipSlot;
                    }
                }
            }

            return RE::TESForm::LookupByID<RE::BGSEquipSlot>(0x00013F44);
        }
    }

    void SpellFactory::CloneConditions(RE::TESCondition& a_dest, const RE::TESCondition& a_src)
    {
        auto* srcItem = a_src.head;
        RE::TESConditionItem** destTail = &a_dest.head;

        while (srcItem) {
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
        spell->data.castingType = a_params.castingType;
        spell->data.chargeTime = a_params.chargeTime;
        spell->data.castingPerk = nullptr;
        spell->equipSlot = ResolveEitherHandEquipSlot();

        auto sortedEffects = a_params.effects;
        std::stable_sort(sortedEffects.begin(), sortedEffects.end(), [](const CustomEffectParam& a, const CustomEffectParam& b) {
            const bool aExplosive = HasExplosion(a.effectSetting);
            const bool bExplosive = HasExplosion(b.effectSetting);
            if (aExplosive != bExplosive) {
                return aExplosive > bExplosive;
            }

            const bool aCollidingProj = HasAuthenticCollidingProjectile(a.effectSetting);
            const bool bCollidingProj = HasAuthenticCollidingProjectile(b.effectSetting);
            if (aCollidingProj != bCollidingProj) {
                return aCollidingProj > bCollidingProj;
            }

            return a.area > b.area;
        });

        auto* primaryMGEF = sortedEffects[0].effectSetting;
        if (a_params.deliveryType == RE::MagicSystem::Delivery::kSelf) {
            spell->data.delivery = RE::MagicSystem::Delivery::kSelf;
        } else if (primaryMGEF && primaryMGEF->data.delivery != RE::MagicSystem::Delivery::kSelf) {
            spell->data.delivery = primaryMGEF->data.delivery;
        } else {
            spell->data.delivery = a_params.deliveryType;
        }

        const auto primarySchool = primaryMGEF ? primaryMGEF->data.primaryAV : RE::ActorValue::kDestruction;
        auto* compatMgr = CompatibilityManager::GetSingleton();

        // 1. Resolve exact parent spells for the player-chosen effects ONLY (Disambiguated!)
        std::vector<RE::FormID> authenticParentSpellIDs;
        RE::SpellItem* primaryParentSpell = nullptr;

        if (compatMgr) {
            for (const auto& fx : sortedEffects) {
                if (!fx.effectSetting) continue;
                if (auto* exactParent = compatMgr->ResolveExactParentSpell(fx.effectSetting)) {
                    if (!primaryParentSpell) {
                        primaryParentSpell = exactParent;
                    }
                    if (std::find(authenticParentSpellIDs.begin(), authenticParentSpellIDs.end(), exactParent->GetFormID()) == authenticParentSpellIDs.end()) {
                        authenticParentSpellIDs.push_back(exactParent->GetFormID());
                        SKSE::log::info("SpellFactory: Mapped user-selected effect '{}' -> Exact Parent Spell '{}' (0x{:08X}).",
                            fx.effectSetting->GetFullName(), exactParent->GetFullName(), exactParent->GetFormID());
                    }
                }
            }
            compatMgr->RegisterCraftedSpellSources(spell->GetFormID(), authenticParentSpellIDs);
        }

        if (primaryParentSpell && primaryParentSpell->menuDispObject) {
            spell->menuDispObject = primaryParentSpell->menuDispObject;
        } else {
            spell->menuDispObject = ResolveMenuDisplayObject(primarySchool);
        }

        // 2. Keyword Harvesting from verified parents only
        std::vector<RE::BGSKeyword*> aggregatedKeywords;
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
        }

        for (const auto parentID : authenticParentSpellIDs) {
            if (auto* parentSpell = RE::TESForm::LookupByID<RE::SpellItem>(parentID)) {
                auto* srcKw = static_cast<RE::BGSKeywordForm*>(static_cast<RE::MagicItem*>(parentSpell));
                if (srcKw && srcKw->keywords) {
                    for (std::uint32_t k = 0; k < srcKw->numKeywords; ++k) {
                        if (srcKw->keywords[k] && std::find(aggregatedKeywords.begin(), aggregatedKeywords.end(), srcKw->keywords[k]) == aggregatedKeywords.end()) {
                            aggregatedKeywords.push_back(srcKw->keywords[k]);
                        }
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

        // 3. Resolve Master Blast Area
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

            if (compatMgr) {
                if (auto* exactParent = compatMgr->ResolveExactParentSpell(fx.effectSetting)) {
                    for (auto* eff : exactParent->effects) {
                        if (eff && eff->baseEffect == fx.effectSetting) {
                            minMag = eff->effectItem.magnitude;
                            minDur = eff->effectItem.duration;
                            minArea = eff->effectItem.area;
                            break;
                        }
                    }
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

            CloneConditions(effectItem->conditions, fx.effectSetting->conditions);

            const std::uint32_t singleCost = CalculateCost(fx.effectSetting, clampedMagnitude, clampedDuration, effectItem->effectItem.area);

            if (effectIndex == 0) {
                effectItem->cost = static_cast<float>(singleCost + 500);
                totalActualCost += singleCost;
            } else {
                effectItem->cost = static_cast<float>(singleCost);
                totalActualCost += static_cast<std::uint32_t>(std::round(static_cast<float>(singleCost) * (effectIndex == 1 ? 0.35f : 0.20f)));
            }

            spell->effects.push_back(effectItem);
            effectIndex++;
        }

        // 4. Disambiguated Companion Effect Preserver:
        // Only attaches companions from the exact verified parent spells of the user's selected effects!
        for (const auto parentID : authenticParentSpellIDs) {
            auto* exactParent = RE::TESForm::LookupByID<RE::SpellItem>(parentID);
            if (!exactParent || exactParent->effects.size() <= 1) continue;

            for (auto* companionEff : exactParent->effects) {
                if (!companionEff || !companionEff->baseEffect) continue;

                // Skip if already present
                bool alreadyAdded = false;
                for (auto* existing : spell->effects) {
                    if (existing && existing->baseEffect == companionEff->baseEffect) {
                        alreadyAdded = true;
                        break;
                    }
                }
                if (alreadyAdded) continue;

                // Do not attach companions with conflicting delivery types into the SpellItem (handled by Cast-Proc)
                if (companionEff->baseEffect->data.delivery != spell->data.delivery) {
                    SKSE::log::info("SpellFactory: Delegating conflicting delivery companion '{}' (from '{}') to Cast-Proc Engine.",
                        companionEff->baseEffect->GetFullName(), exactParent->GetFullName());
                    continue;
                }

                auto* clonedCompanion = new RE::Effect();
                clonedCompanion->baseEffect = companionEff->baseEffect;
                clonedCompanion->effectItem.magnitude = companionEff->effectItem.magnitude;
                clonedCompanion->effectItem.duration = companionEff->effectItem.duration;
                clonedCompanion->effectItem.area = companionEff->effectItem.area;
                clonedCompanion->cost = 0.0f;

                CloneConditions(clonedCompanion->conditions, companionEff->conditions);
                spell->effects.push_back(clonedCompanion);

                SKSE::log::info("SpellFactory: Attached verified companion '{}' (0x{:08X}) from exact parent '{}'.",
                    companionEff->baseEffect->GetFullName(), companionEff->baseEffect->GetFormID(), exactParent->GetFullName());
            }
        }

        const auto minSpellBaseCost = static_cast<std::uint32_t>((std::max)(10.0f, sortedEffects[0].effectSetting->data.baseCost));
        spell->data.costOverride = (std::max)(minSpellBaseCost, totalActualCost);

        a_player->AddSpell(spell);

        SKSE::log::info("SpellFactory: Successfully forged composite spell '{}' (FormID: 0x{:08X}) with {} effects, Delivery: {}, Balanced Cost: {}",
            a_params.spellName, static_cast<std::uint32_t>(spell->GetFormID()), spell->effects.size(),
            static_cast<int>(spell->data.delivery), spell->data.costOverride);

        return spell;
    }
}