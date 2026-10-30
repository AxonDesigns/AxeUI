// Verifies at compile time that this test target was built with exceptions
// and RTTI disabled. foundation/build_config.h enforces the same rule for
// every engine translation unit; this checks it without relying on that
// header.

#include "foundation/build_config.h"
#include "support/test_support.h"

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error "exceptions are enabled"
#endif

#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
#error "RTTI is enabled"
#endif

TEST_CASE("build flags: exceptions and RTTI are disabled") {
  // The checks above are compile-time; reaching this point means they held.
  CHECK(true);
}

TEST_CASE("build flags: sanitizer detection matches the compiler") {
#if defined(__SANITIZE_ADDRESS__)
  CHECK(ui::foundation::kAddressSanitizer);
#endif
#if defined(NDEBUG)
  CHECK_FALSE(ui::foundation::kDebugBuild);
#else
  CHECK(ui::foundation::kDebugBuild);
#endif
}
