/* Compiled as strict C11 (no extensions, warnings as errors in CI) to prove
 * that include/ui/ui.h is valid C, then run to exercise the conventions. */

#include <stdio.h>
#include <string.h>

#include "ui/ui.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "this test must be compiled as C11"
#endif

#ifdef __cplusplus
#error "this test must be compiled as C, not C++"
#endif

/* The handle type is a plain 64-bit integer in C. */
_Static_assert(sizeof(ui_handle) == 8, "ui_handle must be 64 bits");
_Static_assert(UI_NULL_HANDLE == 0, "the null handle must be zero");
_Static_assert(sizeof(ui_error_code) == 4, "ui_error_code must be 32 bits");

static int failures = 0;

#define EXPECT(condition)                                                            \
  do {                                                                               \
    if (!(condition)) {                                                              \
      fprintf(stderr, "%s:%d: EXPECT(%s) failed\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                                    \
    }                                                                                \
  } while (0)

struct log_counter {
  int count;
};

static void count_logs(void* user_data, ui_log_level level, ui_error_code code, const char* message,
                       size_t message_length) {
  struct log_counter* counter = (struct log_counter*)user_data;
  (void)level;
  (void)code;
  (void)message;
  (void)message_length;
  counter->count++;
}

int main(void) {
  ui_context* context = NULL;
  ui_allocation_stats stats;
  struct log_counter counter = {0};

  EXPECT(ui_get_version() == UI_VERSION);
  EXPECT(UI_MAKE_VERSION(1, 2, 3) == 0x010203u);
  EXPECT(strcmp(ui_error_code_name(UI_ERROR_STALE_HANDLE), "UI_ERROR_STALE_HANDLE") == 0);
  EXPECT(strcmp(ui_error_code_name((ui_error_code)12345), "UI_ERROR_UNKNOWN") == 0);

  EXPECT(ui_context_create(NULL) == UI_ERROR_INVALID_ARGUMENT);
  EXPECT(ui_context_create(&context) == UI_OK);
  EXPECT(context != NULL);
  EXPECT(ui_context_set_log_callback(context, count_logs, &counter, UI_LOG_TRACE) == UI_OK);
  EXPECT(ui_context_set_log_callback(context, count_logs, &counter, (ui_log_level)99) ==
         UI_ERROR_INVALID_ARGUMENT);
  EXPECT(ui_context_get_allocation_stats(context, &stats) == UI_OK);
  EXPECT(stats.live_allocations == 0);
  EXPECT(ui_context_get_allocation_stats(context, NULL) == UI_ERROR_INVALID_ARGUMENT);
  ui_context_destroy(context);
  ui_context_destroy(NULL);
  EXPECT(counter.count == 0);

  if (failures != 0) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("C11 header test passed\n");
  return 0;
}
