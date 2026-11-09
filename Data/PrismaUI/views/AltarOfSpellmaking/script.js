/**
 * ============================================================================
 * Altar of Spellmaking - Frontend Controller & SKSE Bridge
 * Duplicate Prevention, Fail-Safe Effect Locking & Formula Synchronization
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

function resolveSchoolAsset(school, spellName) {
    let s = (school || "").toLowerCase();
    const name = (spellName || "").toLowerCase();

    if (!s || s === "alteration" || s === "unknown") {
        if (name.includes("fire") || name.includes("flame") || name.includes("frost") || name.includes("ice") || name.includes("shock") || name.includes("lightning") || name.includes("spark")) {
            s = "destruction";
        } else if (name.includes("heal") || name.includes("restore") || name.includes("cure") || name.includes("ward") || name.includes("turn")) {
            s = "restoration";
        } else if (name.includes("fury") || name.includes("fear") || name.includes("calm") || name.includes("frenzy") || name.includes("rout") || name.includes("courage") || name.includes("rally")) {
            s = "illusion";
        } else if (name.includes("conjure") || name.includes("summon") || name.includes("bound") || name.includes("raise") || name.includes("reanimate") || name.includes("trap")) {
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
    player: {
        gold: 0,
        maxMagicka: 100,
        currentMagicka: 100
    },
    knownEffects: [],
    addedEffects: [],
    lockedEffects: new Set(), // Set of disabled/blacklisted effect IDs
    currentConfig: null,
    lockedCastType: null,
    lockedTargetType: null,
    isMetric: false
};

const DOM = {
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
    closeMenuBtn: document.getElementById("closeMenuBtn")
};

function calculateSingleEffectCost(effect, mag, dur, areaRawFeet) {
    const rawBase = effect.baseCost || 10.0;
    const base = Math.max(1.0, rawBase);

    const magFactor = (mag > 0) ? Math.pow(mag, 1.1) : 1.0;
    const durFactor = (dur > 0) ? Math.pow(dur / 10.0, 1.1) : 1.0;
    const areaFactor = (areaRawFeet > 0) ? (areaRawFeet * 0.1) : 1.0;

    constexpr_Scale = 0.5;
    const computedMagicka = Math.round(base * magFactor * durFactor * areaFactor * constexpr_Scale);
    const finalMagicka = Math.max(10, computedMagicka);
    const finalGold = Math.max(25, Math.round(finalMagicka * 2.5));

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

// Checks compatibility, duplicate state, and locked status
function getEffectState(effect) {
    if (AppState.lockedEffects.has(effect.id)) {
        return { selectable: false, reason: "locked" };
    }

    const isAlreadyAdded = AppState.addedEffects.some(item => item.effect.id === effect.id);
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
        li.className = `effect-item ${!state.selectable ? (state.reason === "duplicate" ? "disabled-duplicate" : (state.reason === "locked" ? "disabled-locked" : "incompatible")) : ""}`;
        li.setAttribute("role", "option");

        if (state.reason === "duplicate") {
            li.title = "Already added to the current incantation (Duplicates not permitted)";
        } else if (state.reason === "locked") {
            li.title = "Effect currently locked due to matrix instability";
        }

        li.innerHTML = `
            <span class="effect-icon-wrap">
                <img src="${asset.path}" class="school-svg-img ${asset.colorClass}" alt="${effect.school}" />
            </span>
            <span class="effect-name">${effect.name}</span>
        `;

        if (state.selectable) {
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
        const matchesCurrentLock = !AppState.lockedTargetType || AppState.lockedTargetType === btnType;

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

    const btnText = (AppState.currentConfig.editIndex !== null)
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

        if (AppState.currentConfig.editIndex !== null && AppState.addedEffects[AppState.currentConfig.editIndex]) {
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

// SKSE / PrismaUI Bridge Interface
window.AltarAPI = {
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

renderKnownEffects();
renderAddedEffectsCart();