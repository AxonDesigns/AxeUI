#include "foundation/assertions.h"

#include <cstdio>
#include <cstdlib>

namespace ui::foundation::detail {

void assertion_failed(const char* expression, const char* message, const char* file,
                      int line) noexcept {
  // Nothing useful can be done if reporting fails; abort regardless.
  static_cast<void>(
      std::fprintf(stderr, "%s:%d: assertion failed: %s (%s)\n", file, line, message, expression));
  static_cast<void>(std::fflush(stderr));
  std::abort();
}

}  // namespace ui::foundation::detail
