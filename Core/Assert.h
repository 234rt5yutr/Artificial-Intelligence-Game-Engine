#pragma once

#include "Core/Log.h"

#ifdef _MSC_VER
    #define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    #define ENGINE_DEBUG_BREAK() __builtin_trap()
#else
    #define ENGINE_DEBUG_BREAK()
#endif

// Enable asserts always for now, but usually they are tied to a debug build configuration
#ifndef ENGINE_DISABLE_ASSERTS
    #define ENGINE_ENABLE_ASSERTS
#endif

#ifdef ENGINE_ENABLE_ASSERTS
    #define ENGINE_ASSERT(condition, ...) { if(!(condition)) { ENGINE_ERROR("Assertion Failed: " __VA_ARGS__); ENGINE_DEBUG_BREAK(); } }
    #define ENGINE_CORE_ASSERT(condition, ...) { if(!(condition)) { ENGINE_CORE_ERROR("Assertion Failed: " __VA_ARGS__); ENGINE_DEBUG_BREAK(); } }
#else
    #define ENGINE_ASSERT(condition, ...)
    #define ENGINE_CORE_ASSERT(condition, ...)
#endif
