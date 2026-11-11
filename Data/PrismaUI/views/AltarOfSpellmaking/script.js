/**
 * ============================================================================
 * Altar of Spellmaking - Frontend Controller & SKSE Bridge
 * Deep Apocalypse Magic School Compatibility & BaseCost Floor Formula
 * ============================================================================
 */

function triggerEngineAudio(soundName) {
    if (typeof window.onPlayUISound === "function") {
        window.onPlayUISound(soundName);
    } else if (typeof window.dispatch === "function") {
        window.dispatch("onPlayUISound", soundName);
    }
}

let lastSliderSoundTime = 0;
function triggerSliderAudio() {
    const now = Date.now();
    if (now - lastSliderSoundTime > 65) {
        lastSliderSoundTime = now;
        triggerEngineAudio("slider");
    }
}

// Deep School Vector Asset Resolver (Expanded with Apocalypse Magic Archetypes)
function resolveSchoolAsset(school, spellName) {
    let s = (school || "").toLowerCase();
    const name = (spellName || "").toLowerCase();

    if (!s || s === "alteration" || s === "unknown") {
        // Comprehensive Destruction Archetypes (Vanilla + Apocalypse + Mysticism)
        if (name.includes("fire") || name.includes("flame") || name.includes("frost") || 
            name.includes("ice") || name.includes("shock") || name.includes("lightning") || 
            name.includes("spark") || name.includes("bolide") || name.includes("rift") || 
            name.includes("blast") || name.includes("bombard") || name.includes("burst") || 
            name.includes("barrage") || name.includes("cinder") || name.includes("cremate") || 
            name.includes("scorch") || name.includes("meteor") || name.includes("hailstone") || 
            name.includes("thundercrack") || name.includes("combust") || name.includes("howler") || 
            name.includes("volcano") || name.includes("cyclone") || name.includes("plasma")) {
            s = "destruction";
        } else if (name.includes("heal") || name.includes("restore") || name.includes("cure") || 
                   name.includes("ward") || name.includes("turn") || name.includes("repel") || 
                   name.includes("wildfire") || name.includes("resplendent")) {
            s = "restoration";
        } else if (name.includes("fury") || name.includes("fear") || name.includes("calm") || 
                   name.includes("frenzy") || name.includes("rout") || name.includes("courage") || 
                   name.includes("rally") || name.includes("mayhem") || name.includes("hysteria") || 
                   name.includes("mind") || name.includes("compel") || name.includes("evil twin")) {
            s = "illusion";
        } else if (name.includes("conjure") || name.includes("summon") || name.includes("bound") || 
                   name.includes("raise") || name.includes("reanimate") || name.includes("trap") || 
                   name.includes("banish") || name.includes("command") || name.includes("familiar") || 
                   name.includes("atronach") || name.includes("dremora") || name.includes("herne") || 
                   name.includes("lich")) {
            s = "conjuration";
        }
    }

    if (s.includes("destruct")) return { path: "./assets/Destruction.svg", colorClass: "color-destruction" };
    if (s.includes("restor"))   return { path: "./assets/Restoration.svg", colorClass: "color-restoration" };
    if (s.includes("conjur"))   return { path: "./assets/Conjuration.svg", colorClass: "color-conjuration" };
    if (s.includes("illus"))    return { path: "./assets/Illusion.svg", colorClass: "color-illusion" };
    return { path: "./assets/Alteration.svg", colorClass: "color-alteration" };
}

const AppState = {
    activeTab: "forge",
    player: {
        gold: 0,
        maxMagicka: 100,
        currentMagicka: 100
    },
    knownEffects: [],
    addedEffects: [],
    editingCartIndex: -1,
    createdSpells: [],
    selectedCreatedSpell: null,
    lockedEffects: new Set(),
    currentConfig: null,
    lockedCastType: null,
    lockedTargetType: null,
    isMetric: false
};

const DOM = {
    altarViewport: document.getElementById("altarViewport"),
    forgeModeContainer: document.getElementById("forgeModeContainer"),
    deconstructModeContainer: document.getElementById("deconstructModeContainer"),
    tabForgeBtn: document.getElementById("tabForgeBtn"),
    tabDeconstructBtn: document.getElementById("tabDeconstructBtn"),

    effectList: document.getElementById("effectList"),
    playerGoldDisplay: document.getElementById("playerGoldDisplay"),
    spellNameInput: document.getElementById("spellNameInput"),
    viewCart: document.getElementById("viewCart"),
    viewConfig: document.getElementById("viewConfig"),
    cartSummaryBar: document.querySelector(".cart-summary-bar"),
    emptyCartMsg: document.getElementById("emptyCartMsg"),
    addedEffectsList: document.getElementById("addedEffectsList"),
    cfgBadge: document.getElementById("cfgBadge"),
    cfgEffectName: document.getElementById("cfgEffectName"),
    cfgEffectDesc: document.getElementById("cfgEffectDesc"),
    rangeBtns: document.querySelectorAll(".range-btn"),
    cfgMagSlider: document.getElementById("cfgMagSlider"),
    cfgDurSlider: document.getElementById("cfgDurSlider"),
    cfgAreaSlider: document.getElementById("cfgAreaSlider"),
    cfgMagVal: document.getElementById("cfgMagVal"),
    cfgDurVal: document.getElementById("cfgDurVal"),
    cfgAreaVal: document.getElementById("cfgAreaVal"),
    cfgAreaToggle: document.getElementById("cfgAreaToggle"),
    unitBadge: document.getElementById("unitBadge"),
    cfgConfirmBtn: document.getElementById("cfgConfirmBtn"),
    cfgCancelBtn: document.getElementById("cfgCancelBtn"),
    totalMagickaDisplay: document.getElementById("totalMagickaDisplay"),
    totalDurationDisplay: document.getElementById("totalDurationDisplay"),
    totalGoldDisplay: document.getElementById("totalGoldDisplay"),
    createSpellBtn: document.getElementById("createSpellBtn"),
    closeMenuBtn: document.getElementById("closeMenuBtn"),

    createdSpellList: document.getElementById("createdSpellList"),
    createdSpellCount: document.getElementById("createdSpellCount"),
    deconstructPlaceholder: document.getElementById("deconstructPlaceholder"),
    deconstructContent: document.getElementById("deconstructContent"),
    deconSchoolBadge: document.getElementById("deconSchoolBadge"),
    deconSpellName: document.getElementById("deconSpellName"),
    executeDeconstructBtn: document.getElementById("executeDeconstructBtn"),
    deconCastingType: document.getElementById("deconCastingType"),
    deconDeliveryType: document.getElementById("deconDeliveryType"),
    deconFormId: document.getElementById("deconFormId"),
    deconEffectsList: document.getElementById("deconEffectsList"),
    closeDeconMenuBtn: document.getElementById("closeDeconMenuBtn")
};

function switchMode(newMode) {
    if (AppState.activeTab === newMode) return;
    AppState.activeTab = newMode;
    triggerEngineAudio("UIMenuPrevNext");

    if (newMode === "forge") {
        DOM.tabForgeBtn.classList.add("active");
        DOM.tabDeconstructBtn.classList.remove("active");
        DOM.forgeModeContainer.classList.remove("hidden");
        DOM.deconstructModeContainer.classList.add("hidden");
    } else {
        DOM.tabDeconstructBtn.classList.add("active");
        DOM.tabForgeBtn.classList.remove("active");
        DOM.deconstructModeContainer.classList.remove("hidden");
        DOM.forgeModeContainer.classList.add("hidden");
        renderCreatedSpells();
    }
}

if (DOM.tabForgeBtn) DOM.tabForgeBtn.addEventListener("click", () => switchMode("forge"));
if (DOM.tabDeconstructBtn) DOM.tabDeconstructBtn.addEventListener("click", () => switchMode("deconstruct"));

// BaseCost Floor Formula
function calculateSingleEffectCost(effect, mag, dur, areaRawFeet) {
    const rawBase = effect.baseCost || 10.0;
    const base = Math.max(1.0, rawBase);

    const magFactor = (mag > 0) ? Math.pow(mag, 1.1) : 1.0;
    const durFactor = (dur > 0) ? Math.pow(dur / 10.0, 1.1) : 1.0;
    const areaFactor = (areaRawFeet > 0) ? (areaRawFeet * 0.1) : 1.0;

    const constexpr_Scale = 0.5;
    const computedMagicka = Math.round(base * magFactor * durFactor * areaFactor * constexpr_Scale);

    const finalMagicka = Math.max(Math.round(rawBase), Math.max(10, computedMagicka));
    const finalGold = Math.max(Math.round(rawBase * 2.5), Math.round(finalMagicka * 2.5));

    return { magicka: finalMagicka, gold: finalGold };
}

function recalculateTotalCart() {
    let totalMagicka = 0;
    let totalGold = 0;
    let maxDuration = 0;

    AppState.addedEffects.forEach((item, index) => {
        let weight = 1.0;
        if (index === 1) weight = 0.35;
        else if (index >= 2) weight = 0.20;

        totalMagicka += Math.round(item.computedMagicka * weight);
        totalGold += Math.round(item.computedGold * weight);

        if (item.duration > maxDuration) {
            maxDuration = item.duration;
        }
    });

    DOM.totalMagickaDisplay.textContent = `${totalMagicka} / ${AppState.player.maxMagicka}`;
    DOM.totalDurationDisplay.textContent = `${maxDuration} s`;
    DOM.totalGoldDisplay.textContent = totalGold.toLocaleString();

    const hasEnoughGold = AppState.player.gold >= totalGold;
    const hasEffects = AppState.addedEffects.length > 0;

    if (!hasEnoughGold && hasEffects) {
        DOM.totalGoldDisplay.parentElement.classList.add("insufficient");
        DOM.createSpellBtn.disabled = true;
    } else {
        DOM.totalGoldDisplay.parentElement.classList.remove("insufficient");
        DOM.createSpellBtn.disabled = !hasEffects;
    }
}

function getEffectState(effect) {
    if (AppState.lockedEffects.has(effect.id)) {
        return { selectable: false, reason: "locked" };
    }

    const isAlreadyAdded = AppState.addedEffects.some(item => 
        (item.effect.id && effect.id && item.effect.id.toLowerCase() === effect.id.toLowerCase()) ||
        (item.effect.name && effect.name && item.effect.name.trim().toLowerCase() === effect.name.trim().toLowerCase())
    );

    if (isAlreadyAdded) {
        return { selectable: false, reason: "duplicate" };
    }

    if (AppState.addedEffects.length > 0) {
        if (AppState.lockedCastType && effect.castType !== AppState.lockedCastType) {
            return { selectable: false, reason: "incompatible" };
        }
        if (AppState.lockedTargetType && !effect.allowedTargetTypes.includes(AppState.lockedTargetType)) {
            return { selectable: false, reason: "incompatible" };
        }
    }

    return { selectable: true, reason: "" };
}

function renderKnownEffects() {
    DOM.effectList.innerHTML = "";

    AppState.knownEffects.forEach((effect) => {
        const asset = resolveSchoolAsset(effect.school, effect.name);
        const state = getEffectState(effect);

        const li = document.createElement("li");
        let itemClass = "effect-item";
        let statusBadge = "";

        if (!state.selectable) {
            if (state.reason === "duplicate") {
                itemClass += " disabled-duplicate already-added";
                statusBadge = `<span class="cart-lock-tag font-futura">IN CART</span>`;
            } else if (state.reason === "locked") {
                itemClass += " disabled-locked";
            } else {
                itemClass += " incompatible";
            }
        }

        li.className = itemClass;
        li.setAttribute("role", "option");

        li.innerHTML = `
            <span class="effect-icon-wrap">
                <img src="${asset.path}" class="school-svg-img ${asset.colorClass}" alt="${effect.school}" />
            </span>
            <span class="effect-name">${effect.name}</span>
            ${statusBadge}
        `;

        if (!state.selectable) {
            li.style.pointerEvents = "none";
            li.setAttribute("aria-disabled", "true");
        } else {
            li.addEventListener("mouseenter", () => triggerEngineAudio("hover"));
            li.addEventListener("click", () => {
                triggerEngineAudio("click");
                openEffectConfiguration(effect, asset, null);
            });
        }

        DOM.effectList.appendChild(li);
    });
}

function openEffectConfiguration(effect, asset, editIndex = null) {
    if (editIndex === null && AppState.addedEffects.some(item => 
        (item.effect.id && effect.id && item.effect.id.toLowerCase() === effect.id.toLowerCase()) ||
        (item.effect.name && effect.name && item.effect.name.trim().toLowerCase() === effect.name.trim().toLowerCase())
    )) {
        console.warn("Blocked duplicate selection attempt for:", effect.name);
        return;
    }

    let defaultTarget = AppState.lockedTargetType;
    const minMag = effect.minMagnitude !== undefined ? effect.minMagnitude : 1;
    const minDur = effect.minDuration !== undefined ? effect.minDuration : 0;
    const minArea = effect.minArea !== undefined ? effect.minArea : 0;

    let initialMag = Math.max(25, minMag);
    let initialDur = Math.max(5, minDur);
    let initialArea = effect.supportsArea ? Math.max(effect.defaultArea || 0, minArea) : 0;

    if (editIndex !== null && AppState.addedEffects[editIndex]) {
        const existing = AppState.addedEffects[editIndex];
        defaultTarget = existing.targetType;
        initialMag = existing.magnitude;
        initialDur = existing.duration;
        initialArea = existing.areaRawFeet;
    } else {
        if (!defaultTarget && Array.isArray(effect.allowedTargetTypes) && effect.allowedTargetTypes.length > 0) {
            defaultTarget = effect.allowedTargetTypes[0];
        } else if (!defaultTarget) {
            defaultTarget = "Aimed";
        }
    }

    const supportsArea = effect.supportsArea === true;

    AppState.currentConfig = {
        effect: effect,
        asset: asset,
        editIndex: editIndex,
        targetType: defaultTarget,
        magnitude: initialMag,
        duration: initialDur,
        areaRawFeet: supportsArea ? initialArea : 0
    };

    DOM.cfgBadge.className = "effect-icon-wrap";
    DOM.cfgBadge.innerHTML = `<img src="${asset.path}" class="school-svg-img ${asset.colorClass}" alt="${effect.school}" />`;
    DOM.cfgEffectName.textContent = effect.name;
    if (DOM.cfgEffectDesc) {
        DOM.cfgEffectDesc.textContent = effect.description || "Channels arcane matrix into the woven incantation.";
    }

    DOM.cfgMagSlider.min = minMag;
    DOM.cfgMagSlider.value = initialMag;
    DOM.cfgDurSlider.min = minDur;
    DOM.cfgDurSlider.value = initialDur;
    DOM.cfgAreaSlider.min = minArea;
    DOM.cfgAreaSlider.value = supportsArea ? initialArea : 0;

    const areaRow = DOM.cfgAreaSlider.closest(".config-row");
    if (areaRow) areaRow.style.display = supportsArea ? "block" : "none";

    DOM.rangeBtns.forEach(btn => {
        const btnType = btn.dataset.range;
        const isAllowedByEffect = Array.isArray(effect.allowedTargetTypes) && effect.allowedTargetTypes.includes(btnType);
        const matchesCurrentLock = (editIndex === 0) || !AppState.lockedTargetType || AppState.lockedTargetType === btnType;

        if (isAllowedByEffect && matchesCurrentLock) {
            btn.classList.remove("disabled-locked");
            btn.classList.toggle("active", btnType === defaultTarget);
        } else {
            btn.classList.add("disabled-locked");
            btn.classList.remove("active");
        }
    });

    updateConfigLivePreview();

    DOM.viewCart.classList.add("hidden");
    if (DOM.cartSummaryBar) DOM.cartSummaryBar.classList.add("hidden");
    DOM.viewConfig.classList.remove("hidden");
}

function closeEffectConfiguration() {
    AppState.currentConfig = null;
    AppState.editingCartIndex = -1;
    DOM.viewConfig.classList.add("hidden");
    DOM.viewCart.classList.remove("hidden");
    if (DOM.cartSummaryBar) DOM.cartSummaryBar.classList.remove("hidden");
}

function updateConfigLivePreview() {
    if (!AppState.currentConfig) return;

    DOM.cfgMagVal.textContent = AppState.currentConfig.magnitude;
    DOM.cfgDurVal.textContent = `${AppState.currentConfig.duration} s`;

    if (AppState.isMetric) {
        DOM.cfgAreaVal.textContent = Math.round(AppState.currentConfig.areaRawFeet * 0.3048);
        DOM.unitBadge.textContent = "m";
    } else {
        DOM.cfgAreaVal.textContent = AppState.currentConfig.areaRawFeet;
        DOM.unitBadge.textContent = "ft";
    }

    const liveCost = calculateSingleEffectCost(
        AppState.currentConfig.effect,
        AppState.currentConfig.magnitude,
        AppState.currentConfig.duration,
        AppState.currentConfig.areaRawFeet
    );

    const btnText = (AppState.currentConfig.editIndex !== null && AppState.currentConfig.editIndex >= 0)
        ? `Update (${liveCost.magicka} Magicka / ${liveCost.gold} Gold)`
        : `Confirm (${liveCost.magicka} Magicka / ${liveCost.gold} Gold)`;

    DOM.cfgConfirmBtn.textContent = btnText;
}

function renderAddedEffectsCart() {
    DOM.addedEffectsList.innerHTML = "";

    if (AppState.addedEffects.length === 0) {
        DOM.emptyCartMsg.style.display = "block";
        AppState.lockedCastType = null;
        AppState.lockedTargetType = null;
    } else {
        DOM.emptyCartMsg.style.display = "none";
        AppState.lockedCastType = AppState.addedEffects[0].effect.castType;
        AppState.lockedTargetType = AppState.addedEffects[0].targetType;

        AppState.addedEffects.forEach((item, index) => {
            const li = document.createElement("li");
            li.className = "cart-item";
            li.title = "Click to re-edit this effect's configuration";

            li.innerHTML = `
                <div class="cart-item-info">
                    <span class="effect-icon-wrap">
                        <img src="${item.asset.path}" class="school-svg-img ${item.asset.colorClass}" alt="${item.effect.school}" />
                    </span>
                    <div>
                        <div class="cart-item-title">${item.effect.name} on ${item.targetType}</div>
                        <div class="cart-item-meta">${item.magnitude} pts | ${item.duration}s | ${item.computedMagicka} Magicka (${item.computedGold} Gold)</div>
                    </div>
                </div>
                <button type="button" class="cart-item-remove" data-index="${index}" title="Remove Effect">&times;</button>
            `;

            li.addEventListener("mouseenter", () => triggerEngineAudio("hover"));
            li.addEventListener("click", (e) => {
                if (e.target.closest(".cart-item-remove")) return;
                triggerEngineAudio("click");
                openEffectConfiguration(item.effect, item.asset, index);
            });

            li.querySelector(".cart-item-remove").addEventListener("click", (e) => {
                e.stopPropagation();
                triggerEngineAudio("cancel");
                AppState.addedEffects.splice(index, 1);
                renderAddedEffectsCart();
            });

            DOM.addedEffectsList.appendChild(li);
        });
    }

    renderKnownEffects();
    recalculateTotalCart();
}

function resetSpellmakingCart() {
    AppState.addedEffects = [];
    AppState.currentConfig = null;
    AppState.editingCartIndex = -1;
    AppState.lockedCastType = null;
    AppState.lockedTargetType = null;
    DOM.spellNameInput.value = "";
    closeEffectConfiguration();
    renderAddedEffectsCart();
}

DOM.rangeBtns.forEach(btn => {
    btn.addEventListener("click", () => {
        if (btn.classList.contains("disabled-locked")) return;
        triggerEngineAudio("click");
        DOM.rangeBtns.forEach(b => b.classList.remove("active"));
        btn.classList.add("active");
        if (AppState.currentConfig) {
            AppState.currentConfig.targetType = btn.dataset.range;
            updateConfigLivePreview();
        }
    });
});

DOM.cfgMagSlider.addEventListener("input", (e) => {
    triggerSliderAudio();
    if (AppState.currentConfig) {
        AppState.currentConfig.magnitude = parseFloat(e.target.value);
        updateConfigLivePreview();
    }
});

DOM.cfgDurSlider.addEventListener("input", (e) => {
    triggerSliderAudio();
    if (AppState.currentConfig) {
        AppState.currentConfig.duration = parseInt(e.target.value, 10);
        updateConfigLivePreview();
    }
});

DOM.cfgAreaSlider.addEventListener("input", (e) => {
    triggerSliderAudio();
    if (AppState.currentConfig) {
        AppState.currentConfig.areaRawFeet = parseInt(e.target.value, 10);
        updateConfigLivePreview();
    }
});

DOM.cfgAreaToggle.addEventListener("click", () => {
    triggerEngineAudio("click");
    AppState.isMetric = !AppState.isMetric;
    updateConfigLivePreview();
});

DOM.cfgConfirmBtn.addEventListener("click", () => {
    triggerEngineAudio("click");
    if (AppState.currentConfig) {
        const cost = calculateSingleEffectCost(
            AppState.currentConfig.effect,
            AppState.currentConfig.magnitude,
            AppState.currentConfig.duration,
            AppState.currentConfig.areaRawFeet
        );

        const newEntry = {
            effect: AppState.currentConfig.effect,
            asset: AppState.currentConfig.asset,
            targetType: AppState.currentConfig.targetType,
            magnitude: AppState.currentConfig.magnitude,
            duration: AppState.currentConfig.duration,
            areaRawFeet: AppState.currentConfig.areaRawFeet,
            computedMagicka: cost.magicka,
            computedGold: cost.gold
        };

        if (AppState.currentConfig.editIndex !== null && AppState.currentConfig.editIndex >= 0 && AppState.addedEffects[AppState.currentConfig.editIndex]) {
            AppState.addedEffects[AppState.currentConfig.editIndex] = newEntry;
        } else {
            AppState.addedEffects.push(newEntry);
        }

        closeEffectConfiguration();
        renderAddedEffectsCart();
    }
});

DOM.cfgCancelBtn.addEventListener("click", () => {
    triggerEngineAudio("cancel");
    closeEffectConfiguration();
});

DOM.createSpellBtn.addEventListener("click", () => {
    if (AppState.addedEffects.length === 0) return;

    let totalGold = 0;
    let totalMagicka = 0;

    AppState.addedEffects.forEach((item, index) => {
        let weight = 1.0;
        if (index === 1) weight = 0.35;
        else if (index >= 2) weight = 0.20;

        totalMagicka += Math.round(item.computedMagicka * weight);
        totalGold += Math.round(item.computedGold * weight);
    });

    if (AppState.player.gold < totalGold) {
        triggerEngineAudio("cancel");
        alert("You do not possess enough gold to craft this incantation!");
        return;
    }

    const spellName = DOM.spellNameInput.value.trim() || "Custom Incantation";

    const payload = {
        spellName: spellName,
        castType: AppState.lockedCastType,
        targetType: AppState.lockedTargetType,
        effects: AppState.addedEffects.map(item => ({
            id: item.effect.id,
            targetType: item.targetType,
            magnitude: item.magnitude,
            duration: item.duration,
            areaFeet: item.areaRawFeet
        })),
        totalMagickaCost: totalMagicka,
        totalGoldCost: totalGold
    };

    if (typeof window.onPlayerCraftSpell === "function") {
        window.onPlayerCraftSpell(JSON.stringify(payload));
    }

    resetSpellmakingCart();
});

DOM.closeMenuBtn.addEventListener("click", () => {
    triggerEngineAudio("cancel");
    resetSpellmakingCart();
    if (typeof window.onMenuCloseRequested === "function") {
        window.onMenuCloseRequested();
    }
});

if (DOM.closeDeconMenuBtn) {
    DOM.closeDeconMenuBtn.addEventListener("click", () => {
        triggerEngineAudio("cancel");
        if (typeof window.onMenuCloseRequested === "function") {
            window.onMenuCloseRequested();
        }
    });
}

function renderCreatedSpells() {
    if (!DOM.createdSpellList) return;
    DOM.createdSpellList.innerHTML = "";
    if (DOM.createdSpellCount) DOM.createdSpellCount.textContent = AppState.createdSpells.length;

    if (AppState.createdSpells.length === 0) {
        if (DOM.deconstructPlaceholder) DOM.deconstructPlaceholder.classList.remove("hidden");
        if (DOM.deconstructContent) DOM.deconstructContent.classList.add("hidden");
        return;
    }

    AppState.createdSpells.forEach((spell) => {
        const asset = resolveSchoolAsset(spell.school || "Destruction", spell.name);
        const li = document.createElement("li");
        li.className = "effect-item";
        if (AppState.selectedCreatedSpell && AppState.selectedCreatedSpell.formId === spell.formId) {
            li.classList.add("active");
        }

        li.innerHTML = `
            <span class="effect-icon-wrap">
                <img src="${asset.path}" class="school-svg-img ${asset.colorClass}" alt="${spell.school || 'Magic'}" />
            </span>
            <span>${spell.name}</span>
        `;

        li.addEventListener("mouseenter", () => triggerEngineAudio("hover"));
        li.addEventListener("click", () => {
            triggerEngineAudio("click");
            selectSpellToDeconstruct(spell, asset);
            document.querySelectorAll("#createdSpellList .effect-item").forEach(item => item.classList.remove("active"));
            li.classList.add("active");
        });

        DOM.createdSpellList.appendChild(li);
    });
}

function selectSpellToDeconstruct(spell, asset) {
    AppState.selectedCreatedSpell = spell;

    if (DOM.deconstructPlaceholder) DOM.deconstructPlaceholder.classList.add("hidden");
    if (DOM.deconstructContent) DOM.deconstructContent.classList.remove("hidden");

    if (DOM.deconSchoolBadge) DOM.deconSchoolBadge.innerHTML = `<img src="${asset.path}" class="school-svg-img ${asset.colorClass}" alt="${spell.school || 'Magic'}" />`;
    if (DOM.deconSpellName) DOM.deconSpellName.textContent = spell.name;
    if (DOM.deconCastingType) DOM.deconCastingType.textContent = spell.castType || "Fire and Forget";
    if (DOM.deconDeliveryType) DOM.deconDeliveryType.textContent = spell.targetType || "Aimed";
    if (DOM.deconFormId) DOM.deconFormId.textContent = spell.formId || "Custom Form";

    if (DOM.deconEffectsList) {
        DOM.deconEffectsList.innerHTML = "";
        if (Array.isArray(spell.effects)) {
            spell.effects.forEach(fx => {
                const li = document.createElement("li");
                li.className = "deconstruct-fx-card";
                li.innerHTML = `
                    <span class="fx-card-title">${fx.name || 'Arcane Effect'}</span>
                    <span class="fx-card-stats font-futura">Mag: ${fx.magnitude} | Dur: ${fx.duration}s | Area: ${fx.area || 0}ft</span>
                `;
                DOM.deconEffectsList.appendChild(li);
            });
        }
    }
}

if (DOM.executeDeconstructBtn) {
    DOM.executeDeconstructBtn.addEventListener("click", () => {
        if (!AppState.selectedCreatedSpell) return;

        triggerEngineAudio("fail");
        const spell = AppState.selectedCreatedSpell;
        const payload = {
            formId: spell.formId,
            spellName: spell.name
        };

        if (typeof window.onPlayerDeconstructSpell === "function") {
            window.onPlayerDeconstructSpell(JSON.stringify(payload));
        }

        AppState.createdSpells = AppState.createdSpells.filter(s => s.formId !== spell.formId);
        AppState.selectedCreatedSpell = null;
        if (DOM.deconstructContent) DOM.deconstructContent.classList.add("hidden");
        if (DOM.deconstructPlaceholder) DOM.deconstructPlaceholder.classList.remove("hidden");
        renderCreatedSpells();
    });
}

// SKSE / PrismaUI Bridge Interface
window.AltarAPI = {
    openMenu: function() {
        if (DOM.altarViewport) {
            DOM.altarViewport.classList.remove("hidden-viewport");
        }
    },

    closeMenu: function() {
        if (DOM.altarViewport) {
            DOM.altarViewport.classList.add("hidden-viewport");
        }
    },

    setPlayerData: function(data) {
        if (!data) return;
        if (typeof data.currentGold === "number") {
            AppState.player.gold = data.currentGold;
            DOM.playerGoldDisplay.textContent = data.currentGold.toLocaleString();
        }
        if (typeof data.maxMagicka === "number") {
            AppState.player.maxMagicka = data.maxMagicka;
        }
        if (typeof data.currentMagicka === "number") {
            AppState.player.currentMagicka = data.currentMagicka;
        }
        recalculateTotalCart();
    },

    setKnownEffects: function(effectsList) {
        if (Array.isArray(effectsList) && effectsList.length > 0) {
            AppState.knownEffects = effectsList;
            renderKnownEffects();
        }
    },

    setCreatedSpells: function(createdList) {
        if (Array.isArray(createdList)) {
            AppState.createdSpells = createdList;
            renderCreatedSpells();
        }
    },

    lockEffect: function(effectId) {
        if (!effectId) return;
        AppState.lockedEffects.add(effectId);
        renderKnownEffects();
    },

    unlockEffect: function(effectId) {
        if (!effectId) return;
        AppState.lockedEffects.delete(effectId);
        renderKnownEffects();
    },

    resetCart: function() {
        resetSpellmakingCart();
    }
};

(function initBrowserTestEnvironment() {
    const isStandaloneWeb = !navigator.userAgent.includes("Ultralight") && typeof window.onPlayerCraftSpell === "undefined";
    
    if (isStandaloneWeb) {
        console.info("[Altar of Spellmaking] Running in Standalone Browser Test Mode.");
        const mockEffects = [
            { id: "0x0001CEA1", name: "Fireball", school: "Destruction", baseCost: 15, castType: "FireAndForget", allowedTargetTypes: ["Aimed"], supportsArea: true, defaultArea: 15, description: "Engulfs targets in searing flames, dealing fire damage and causing an explosion." },
            { id: "0x080FADFD", name: "Lightning Strike", school: "Destruction", baseCost: 20, castType: "FireAndForget", allowedTargetTypes: ["Aimed"], supportsArea: true, defaultArea: 15, description: "Calls down a bolt of lightning, dealing heavy shock damage to targets in the area." },
            { id: "0x00012F03", name: "Firebolt", school: "Destruction", baseCost: 8, castType: "FireAndForget", allowedTargetTypes: ["Aimed"], supportsArea: false, defaultArea: 0, description: "Fires a bolt of fire, dealing 25 points of fire damage to a single target." },
            { id: "0x00012FCD", name: "Healing", school: "Restoration", baseCost: 12, castType: "Concentration", allowedTargetTypes: ["Self"], supportsArea: false, defaultArea: 0, description: "Heals the caster for 10 points per second." },
            { id: "0x0001EA77", name: "Fear", school: "Illusion", baseCost: 18, castType: "FireAndForget", allowedTargetTypes: ["Aimed"], supportsArea: false, defaultArea: 0, description: "Creatures and people up to level 9 flee from combat for 30 seconds." },
            { id: "0x0004DEE6", name: "Fury", school: "Illusion", baseCost: 14, castType: "FireAndForget", allowedTargetTypes: ["Aimed"], supportsArea: true, defaultArea: 15, description: "Creatures and people in the area attack anything nearby for 30 seconds." },
            { id: "0x000211EB", name: "Bound Sword", school: "Conjuration", baseCost: 22, castType: "FireAndForget", allowedTargetTypes: ["Self"], supportsArea: false, defaultArea: 0, description: "Creates a magic sword for 120 seconds. Sheathe it to dispel." },
            { id: "0x0005AD5C", name: "Oakflesh", school: "Alteration", baseCost: 16, castType: "FireAndForget", allowedTargetTypes: ["Self"], supportsArea: false, defaultArea: 0, description: "Improves the caster's armor rating by 40 points for 60 seconds." }
        ];

        window.AltarAPI.openMenu();
        window.AltarAPI.setPlayerData({ currentGold: 18500, maxMagicka: 280, currentMagicka: 280 });
        window.AltarAPI.setKnownEffects(mockEffects);
        window.AltarAPI.setCreatedSpells([
            {
                formId: "0xFF001001",
                name: "Blazing Wrath",
                school: "Destruction",
                castType: "FireAndForget",
                targetType: "Aimed",
                effects: [
                    { name: "Fireball", magnitude: 45, duration: 0, area: 15 },
                    { name: "Firebolt", magnitude: 20, duration: 3, area: 0 }
                ]
            }
        ]);
    }
})();

renderKnownEffects();
renderAddedEffectsCart();