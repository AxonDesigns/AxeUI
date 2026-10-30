#pragma once

#include "foundation/build_config.h"

namespace ui::foundation::detail {

// Writes "file:line: assertion failed: message (expression)" to stderr and
// aborts. Never returns.
[[noreturn]] void assertion_failed(const char* expression, const char* message, const char* file,
                                   int line) noexcept;

}  // namespace ui::foundation::detail

// UI_ASSERT: checked in every build. Use for invariants whose violation would
// corrupt memory or state if execution continued.
#define UI_ASSERT(condition, message) \
  (static_cast<bool>(condition)       \
       ? static_cast<void>(0)         \
       : ::ui::foundation::detail::assertion_failed(#condition, message, __FILE__, __LINE__))

// UI_DEBUG_ASSERT: checked only when UI_DEBUG is 1. In release builds the
// condition is not evaluated, but it is still type-checked and its operands
// count as used.
#if UI_DEBUG
#define UI_DEBUG_ASSERT(condition, message) UI_ASSERT(condition, message)
#else
#define UI_DEBUG_ASSERT(condition, message) static_cast<void>(sizeof(static_cast<bool>(condition)))
#endif
