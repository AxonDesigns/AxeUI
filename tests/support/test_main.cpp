#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "support/test_support.h"

int main(int argc, char** argv) {
  ui::test::configure_process_for_tests();
  doctest::Context context(argc, argv);
  return context.run();
}
