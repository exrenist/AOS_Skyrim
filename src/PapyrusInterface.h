#pragma once

#include "pch.h"

namespace Spellmaking
{
    class PapyrusInterface
    {
    public:
        // Registers custom native C++ functions to the Papyrus virtual machine
        static bool RegisterFunctions(RE::BSScript::IVirtualMachine* a_vm);

        // Native Papyrus API endpoint receiving safe engine-evaluated stats
        static bool OpenAltarMenu(
            RE::StaticFunctionTag*,
            std::int32_t a_gold,
            std::int32_t a_maxMagicka,
            std::int32_t a_currentMagicka);

        // Legacy test function
        static bool QuickTestCraftSpell(RE::StaticFunctionTag*);
    };
}