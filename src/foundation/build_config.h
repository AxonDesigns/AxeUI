#pragma once

// Build-configuration detection shared by every engine header.
//
// This header also enforces the "no exceptions, no RTTI" rule at compile time:
// a translation unit that includes any engine header with either feature
// enabled fails to build, independent of the CMake flags.

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error "AxeUI must be compiled with exceptions disabled (-fno-exceptions or /EHs-c-)."
#endif

#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "AxeUI must be compiled with RTTI disabled (-fno-rtti or /GR-)."
#endif

// UI_DEBUG: 1 in debug builds (no NDEBUG), 0 otherwise. Can be forced with
// -DUI_DEBUG=0/1. Class layouts must never depend on it.
#if !defined(UI_DEBUG)
#if defined(NDEBUG)
#define UI_DEBUG 0
#else
#define UI_DEBUG 1
#endif
#endif

// UI_HAS_ASAN: 1 when compiled with AddressSanitizer (GCC, Clang or MSVC).
#if !defined(UI_HAS_ASAN)
#if defined(__SANITIZE_ADDRESS__)
#define UI_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define UI_HAS_ASAN 1
#endif
#endif
#endif
#if !defined(UI_HAS_ASAN)
#define UI_HAS_ASAN 0
#endif

#if defined(__GNUC__) || defined(__clang__)
// Lets the compiler check printf-style format strings. Indices are 1-based;
// for non-static member functions, `this` is argument 1.
#define UI_PRINTF_FORMAT(format_index, args_index) \
  __attribute__((format(printf, format_index, args_index)))
#else
#define UI_PRINTF_FORMAT(format_index, args_index)
#endif

namespace ui::foundation {

inline constexpr bool kDebugBuild = UI_DEBUG != 0;
inline constexpr bool kAddressSanitizer = UI_HAS_ASAN != 0;

}  // namespace ui::foundation
