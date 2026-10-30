// Triggers UI_ASSERT on purpose. CTest checks that the process prints the
// file, line and message before aborting (see tests/CMakeLists.txt).

#include "foundation/assertions.h"
#include "support/test_process.h"

int main(int argc, char** /*argv*/) {
  ui::test::configure_process_for_tests();
  // Depend on argc so the compiler cannot prove the branch is dead.
  UI_ASSERT(argc < 0, "deliberate assertion failure");
  return 0;
}
