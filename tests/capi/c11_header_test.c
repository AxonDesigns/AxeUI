/* Compiled as strict C11 (no extensions, warnings as errors in CI) to prove
 * that the generated include/ui/ui.h is valid C, then run to exercise the
 * conventions from C. */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ui/ui.h"

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#error "this test must be compiled as C11"
#endif

#ifdef __cplusplus
#error "this test must be compiled as C, not C++"
#endif

/* ABI rules, as seen from C. */
_Static_assert(sizeof(ui_handle) == 8, "ui_handle must be 64 bits");
_Static_assert(UI_NULL_HANDLE == 0, "the null handle must be zero");
_Static_assert(sizeof(ui_error_code) == 4, "enums cross the ABI as int32_t");
_Static_assert(sizeof(ui_log_level) == 4, "enums cross the ABI as int32_t");
_Static_assert(sizeof(ui_event_type) == 4, "enums cross the ABI as int32_t");
_Static_assert(offsetof(ui_context_desc, size) == 0, "structs start with their size");
_Static_assert(offsetof(ui_surface_desc, size) == 0, "structs start with their size");
_Static_assert(offsetof(ui_event, size) == 0, "structs start with their size");
_Static_assert(offsetof(ui_allocation_stats, size) == 0, "structs start with their size");
_Static_assert(sizeof(ui_surface_desc) == 20, "ui_surface_desc has no padding");
_Static_assert(sizeof(ui_allocation_stats) == 48, "ui_allocation_stats has no padding");

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
  ui_error_code last_code;
};

static void count_logs(void* user_data, ui_log_level level, ui_error_code code, const char* message,
                       uint32_t message_length) {
  struct log_counter* counter = (struct log_counter*)user_data;
  (void)level;
  (void)message;
  (void)message_length;
  counter->count++;
  counter->last_code = code;
}

int main(void) {
  ui_context* context = NULL;
  ui_context_desc desc;
  ui_surface_desc surface_desc;
  ui_handle surface = UI_NULL_HANDLE;
  ui_event event;
  uint32_t offset = 0;
  struct log_counter counter = {0, UI_OK};

  EXPECT(ui_get_version() == UI_VERSION);
  EXPECT(UI_MAKE_VERSION(1, 2, 3) == 0x010203u);
  EXPECT(strcmp(ui_error_code_name(UI_ERROR_STALE_HANDLE), "UI_ERROR_STALE_HANDLE") == 0);

  memset(&desc, 0, sizeof(desc));
  desc.size = sizeof(desc);
  desc.min_log_level = UI_LOG_INFO;
  EXPECT(ui_context_create(&desc, &context) == UI_OK);
  EXPECT(ui_context_set_log_callback(context, count_logs, &counter, UI_LOG_TRACE) == UI_OK);

  memset(&surface_desc, 0, sizeof(surface_desc));
  surface_desc.size = sizeof(surface_desc);
  surface_desc.width = 320;
  surface_desc.height = 200;
  surface_desc.scale = 1.0f;
  EXPECT(ui_surface_create(context, &surface_desc, &surface) == UI_OK);

  memset(&event, 0, sizeof(event));
  event.size = sizeof(event);
  EXPECT(ui_context_poll_event(context, &event) == UI_OK);
  EXPECT(event.type == UI_EVENT_FRAME_REQUESTED);
  EXPECT(event.surface == surface);
  EXPECT(ui_surface_frame(context, surface, 16666667) == UI_OK);

  /* An invalid handle is an error plus a diagnostic, never a crash. */
  EXPECT(ui_surface_destroy(context, surface + 1) != UI_OK);
  EXPECT(counter.count == 1);
  EXPECT(ui_surface_destroy(context, surface) == UI_OK);

  EXPECT(ui_utf8_to_utf16_offset("h\xC3\xA9llo", 6, 3, &offset) == UI_OK);
  EXPECT(offset == 2);

  ui_context_destroy(context);
  ui_context_destroy(NULL);

  if (failures != 0) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("C11 header test passed\n");
  return 0;
}
