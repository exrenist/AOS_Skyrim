#pragma once

// Enforce modern C++ std::min and std::max over legacy Windows preprocessor macros
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>

// Undefine lingering macros before including CLibUtil and SimpleIni
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

// ClibUtil Core Modules
#include <CLIBUtil/editorID.hpp>
#include <CLIBUtil/simpleINI.hpp>
#include <CLIBUtil/string.hpp>

// CommonLib's generated plugin declaration uses string-view literals.
using namespace std::literals;