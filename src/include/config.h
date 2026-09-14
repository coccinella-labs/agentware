#pragma once

// Define DLL export/import macros for Windows
#if defined(_WIN32) || defined(_WIN64)
#ifndef AGENTWARE_API
#ifdef AGENTWARE_LIBRARY
#define AGENTWARE_API __declspec(dllexport)
#else
#define AGENTWARE_API __declspec(dllimport)
#endif
#endif

// Disable warning about STL types in the interface
#ifndef _HAS_CXX17
#define _HAS_CXX17 1
#endif

#ifndef _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
#define _SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING
#endif

#pragma warning(disable                                                        \
                : 4251) // Disable warning about STL types in the interface
#pragma warning(                                                               \
    disable                                                                    \
    : 4275) // Disable warning about non dll-interface class used as base

// Enable secure CRT functions
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// Disable min/max macros from windows.h
#ifndef NOMINMAX
#define NOMINMAX
#endif
#else
#ifndef AGENTWARE_API
#define AGENTWARE_API __attribute__((visibility("default")))
#endif

// On non-Windows, we can use visibility attributes for better control
#ifdef __GNUC__
#ifndef AGENTWARE_LOCAL
#define AGENTWARE_LOCAL __attribute__((visibility("hidden")))
#endif
#else
#ifndef AGENTWARE_LOCAL
#define AGENTWARE_LOCAL
#endif
#endif
#endif

// Disable exporting of STL templates
#ifdef _MSC_VER
#define AGENTWARE_NO_EXPORT_TEMPLATE template class AGENTWARE_API
#else
#define AGENTWARE_NO_EXPORT_TEMPLATE extern template class AGENTWARE_API
#endif

#include <string>

namespace Utils::Config {
AGENTWARE_API void load_environment(const std::string &filename = ".env");
AGENTWARE_API std::string get_env_var(const std::string &key,
                                      const std::string &default_value = "");
AGENTWARE_API bool has_env_var(const std::string &key);
} // namespace Utils::Config
