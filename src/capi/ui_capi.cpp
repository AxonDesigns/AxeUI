// Implementation of include/ui/ui.h.

#include <memory>

#include "capi/capi_context.h"
#include "foundation/error_code.h"
#include "foundation/logging.h"

namespace {

using ui::foundation::ErrorCode;
using ui::foundation::LogLevel;

// The C enums mirror the C++ ones value for value.
#define UI_CHECK_ERROR(c_name, cpp_name) \
  static_assert(static_cast<int>(c_name) == static_cast<int>(ErrorCode::cpp_name))
UI_CHECK_ERROR(UI_OK, Ok);
UI_CHECK_ERROR(UI_ERROR_INVALID_ARGUMENT, InvalidArgument);
UI_CHECK_ERROR(UI_ERROR_OUT_OF_MEMORY, OutOfMemory);
UI_CHECK_ERROR(UI_ERROR_INVALID_HANDLE, InvalidHandle);
UI_CHECK_ERROR(UI_ERROR_STALE_HANDLE, StaleHandle);
UI_CHECK_ERROR(UI_ERROR_WRONG_HANDLE_TYPE, WrongHandleType);
UI_CHECK_ERROR(UI_ERROR_CAPACITY_EXCEEDED, CapacityExceeded);
UI_CHECK_ERROR(UI_ERROR_UNSUPPORTED, Unsupported);
UI_CHECK_ERROR(UI_ERROR_UNAVAILABLE, Unavailable);
UI_CHECK_ERROR(UI_ERROR_INVALID_STATE, InvalidState);
UI_CHECK_ERROR(UI_ERROR_INTERNAL, Internal);
#undef UI_CHECK_ERROR
static_assert(ui::foundation::kErrorCodeCount == 11, "update ui_error_code in ui.h");

static_assert(static_cast<int>(UI_LOG_TRACE) == static_cast<int>(LogLevel::Trace));
static_assert(static_cast<int>(UI_LOG_DEBUG) == static_cast<int>(LogLevel::Debug));
static_assert(static_cast<int>(UI_LOG_INFO) == static_cast<int>(LogLevel::Info));
static_assert(static_cast<int>(UI_LOG_WARNING) == static_cast<int>(LogLevel::Warning));
static_assert(static_cast<int>(UI_LOG_ERROR) == static_cast<int>(LogLevel::Error));

static_assert(sizeof(ui_handle) == sizeof(ui::foundation::Handle));

}  // namespace

extern "C" {

uint32_t ui_get_version(void) { return UI_VERSION; }

const char* ui_error_code_name(ui_error_code code) {
  switch (code) {
    case UI_OK:
      return "UI_OK";
    case UI_ERROR_INVALID_ARGUMENT:
      return "UI_ERROR_INVALID_ARGUMENT";
    case UI_ERROR_OUT_OF_MEMORY:
      return "UI_ERROR_OUT_OF_MEMORY";
    case UI_ERROR_INVALID_HANDLE:
      return "UI_ERROR_INVALID_HANDLE";
    case UI_ERROR_STALE_HANDLE:
      return "UI_ERROR_STALE_HANDLE";
    case UI_ERROR_WRONG_HANDLE_TYPE:
      return "UI_ERROR_WRONG_HANDLE_TYPE";
    case UI_ERROR_CAPACITY_EXCEEDED:
      return "UI_ERROR_CAPACITY_EXCEEDED";
    case UI_ERROR_UNSUPPORTED:
      return "UI_ERROR_UNSUPPORTED";
    case UI_ERROR_UNAVAILABLE:
      return "UI_ERROR_UNAVAILABLE";
    case UI_ERROR_INVALID_STATE:
      return "UI_ERROR_INVALID_STATE";
    case UI_ERROR_INTERNAL:
      return "UI_ERROR_INTERNAL";
    case UI_ERROR_FORCE_32BIT:
      break;
  }
  return "UI_ERROR_UNKNOWN";
}

ui_error_code ui_context_create(ui_context** out_context) {
  if (out_context == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  // The context holds its own heap resource; HeapResource is stateless, so a
  // temporary one can allocate the context itself.
  ui::foundation::HeapResource heap;
  void* memory = heap.allocate(sizeof(ui_context), alignof(ui_context));
  if (memory == nullptr) {
    return UI_ERROR_OUT_OF_MEMORY;
  }
  *out_context = std::construct_at(static_cast<ui_context*>(memory));
  return UI_OK;
}

void ui_context_destroy(ui_context* context) {
  if (context == nullptr) {
    return;
  }
  std::destroy_at(context);
  ui::foundation::HeapResource heap;
  heap.deallocate(context, sizeof(ui_context), alignof(ui_context));
}

ui_error_code ui_context_set_log_callback(ui_context* context, ui_log_callback callback,
                                          void* user_data, ui_log_level min_level) {
  if (context == nullptr || min_level < UI_LOG_TRACE || min_level > UI_LOG_ERROR) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  context->log_callback = callback;
  context->log_user_data = user_data;
  context->logger.set_min_level(static_cast<LogLevel>(min_level));
  return UI_OK;
}

ui_error_code ui_context_get_allocation_stats(const ui_context* context,
                                              ui_allocation_stats* out_stats) {
  if (context == nullptr || out_stats == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  const ui::foundation::AllocationStats& stats = context->memory.stats();
  out_stats->live_allocations = stats.live_allocations;
  out_stats->live_bytes = stats.live_bytes;
  out_stats->peak_allocations = stats.peak_allocations;
  out_stats->peak_bytes = stats.peak_bytes;
  out_stats->total_allocations = stats.total_allocations;
  return UI_OK;
}

}  // extern "C"
