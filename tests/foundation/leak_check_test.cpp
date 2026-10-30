// Self-test of the leak-checking harness (tests/support/test_support.h).

#include "foundation/allocator.h"
#include "support/test_support.h"

using namespace ui::foundation;

UI_TEST_CASE("leak checker passes when every allocation is released") {
  void* p = memory().allocate(64, 16);
  CHECK(tracker().stats().live_allocations == 1);
  memory().deallocate(p, 64, 16);
}

// This test leaks on purpose. The fixture's leak check must fail it, and
// should_fail() turns that expected failure into a pass. If the checker
// stopped catching leaks, this test would fail the suite.
UI_TEST_CASE("leak checker catches a deliberate leak" * doctest::should_fail()) {
  void* leaked = memory().allocate(64, 16);
  CHECK(leaked != nullptr);
  // Freed only after the check, so LeakSanitizer sees no real leak.
  free_after_leak_check(leaked, 64, 16);
}
