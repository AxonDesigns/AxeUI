#include "foundation/assertions.h"

#include "support/test_support.h"

TEST_CASE("assertions pass silently when the condition holds") {
  int evaluations = 0;
  UI_ASSERT(++evaluations == 1, "always evaluated");
  CHECK(evaluations == 1);
}

TEST_CASE("UI_DEBUG_ASSERT evaluates its condition only in debug builds") {
  int evaluations = 0;
  UI_DEBUG_ASSERT(++evaluations > 0, "debug only");
  CHECK(evaluations == (UI_DEBUG ? 1 : 0));
}

// The failing path (report file, line and message, then abort) is covered by
// the ui_assert_death_test CTest entry.
