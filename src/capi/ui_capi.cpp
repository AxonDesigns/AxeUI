// Implementation of include/ui/ui.h (generated from api/ui_api.json).

#include <algorithm>
#include <cstring>
#include <memory>
#include <string_view>

#include "capi/capi_context.h"
#include "foundation/error_code.h"
#include "foundation/logging.h"
#include "foundation/utf8.h"

using ui::capi::ApiScope;
using ui::capi::check_context;

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
UI_CHECK_ERROR(UI_ERROR_GPU, GpuError);
UI_CHECK_ERROR(UI_ERROR_DEVICE_LOST, DeviceLost);
UI_CHECK_ERROR(UI_ERROR_WRONG_THREAD, WrongThread);
#undef UI_CHECK_ERROR
static_assert(ui::foundation::kErrorCodeCount == 14, "update api/ui_api.json");

static_assert(static_cast<int>(UI_LOG_TRACE) == static_cast<int>(LogLevel::Trace));
static_assert(static_cast<int>(UI_LOG_ERROR) == static_cast<int>(LogLevel::Error));
static_assert(sizeof(ui_handle) == sizeof(ui::foundation::Handle));
static_assert(sizeof(ui_error_code) == 4 && sizeof(ui_log_level) == 4);

constexpr const char* kErrorNames[] = {
    "UI_OK",
    "UI_ERROR_INVALID_ARGUMENT",
    "UI_ERROR_OUT_OF_MEMORY",
    "UI_ERROR_INVALID_HANDLE",
    "UI_ERROR_STALE_HANDLE",
    "UI_ERROR_WRONG_HANDLE_TYPE",
    "UI_ERROR_CAPACITY_EXCEEDED",
    "UI_ERROR_UNSUPPORTED",
    "UI_ERROR_UNAVAILABLE",
    "UI_ERROR_INVALID_STATE",
    "UI_ERROR_INTERNAL",
    "UI_ERROR_GPU",
    "UI_ERROR_DEVICE_LOST",
    "UI_ERROR_WRONG_THREAD",
};
static_assert(std::size(kErrorNames) == ui::foundation::kErrorCodeCount);

ui_error_code to_c(const ui::foundation::Error& error) noexcept {
  return ui::capi::to_c(error.code);
}

bool valid_log_level(ui_log_level level) noexcept {
  return level >= UI_LOG_TRACE && level <= UI_LOG_ERROR;
}

// Reads an input struct whose first field is its size. Older callers pass a
// smaller struct (missing fields read as zero); newer callers a larger one
// (unknown trailing fields are ignored).
template <class T>
bool read_input(const T* input, T& out) noexcept {
  out = T{};
  if (input == nullptr || input->size < sizeof(std::uint32_t)) {
    return false;
  }
  std::memcpy(&out, input, std::min<std::size_t>(input->size, sizeof(T)));
  out.size = sizeof(T);
  return true;
}

// Writes an output struct, never past the size the caller declared.
template <class T>
bool write_output(T* output, const T& value) noexcept {
  if (output == nullptr || output->size < sizeof(std::uint32_t) * 2) {
    return false;
  }
  const std::uint32_t caller_size = output->size;
  std::memcpy(output, &value, std::min<std::size_t>(caller_size, sizeof(T)));
  output->size = caller_size;
  return true;
}

}  // namespace

extern "C" {

uint32_t ui_get_version(void) { return UI_VERSION; }

const char* ui_error_code_name(ui_error_code code) {
  if (code < 0 || static_cast<std::uint32_t>(code) >= std::size(kErrorNames)) {
    return "UI_ERROR_UNKNOWN";
  }
  return kErrorNames[code];
}

// --- Context -------------------------------------------------------------------

ui_error_code ui_context_create(const ui_context_desc* desc, ui_context** out_context) {
  if (out_context == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  ui_context_desc options{sizeof(ui_context_desc), UI_LOG_INFO};
  if (desc != nullptr && !read_input(desc, options)) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  if (!valid_log_level(options.min_log_level)) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  // HeapResource is stateless, so a temporary one can allocate the context.
  ui::foundation::HeapResource heap;
  void* memory = heap.allocate(sizeof(ui_context), alignof(ui_context));
  if (memory == nullptr) {
    return UI_ERROR_OUT_OF_MEMORY;
  }
  ui_context* context = std::construct_at(static_cast<ui_context*>(memory), options.min_log_level);
  context->engine.emplace(*context);
  *out_context = context;
  return UI_OK;
}

void ui_context_destroy(ui_context* context) {
  if (context == nullptr || check_context(context) != UI_OK) {
    return;
  }
  if (context->depth > 0) {
    context->logger.log(LogLevel::Error, ErrorCode::InvalidState,
                        "ui_context_destroy: called from inside a callback; ignored");
    return;
  }
  std::destroy_at(context);
  ui::foundation::HeapResource heap;
  heap.deallocate(context, sizeof(ui_context), alignof(ui_context));
}

ui_error_code ui_context_set_log_callback(ui_context* context, ui_log_callback callback,
                                          void* user_data, ui_log_level min_level) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  if (!valid_log_level(min_level)) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  context->log_callback = callback;
  context->log_user_data = user_data;
  context->logger.set_min_level(static_cast<LogLevel>(min_level));
  return UI_OK;
}

ui_error_code ui_context_set_event_callback(ui_context* context, ui_event_callback callback,
                                            void* user_data) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  context->event_callback = callback;
  context->event_user_data = user_data;
  return UI_OK;
}

ui_error_code ui_context_set_wake_callback(ui_context* context, ui_wake_callback callback,
                                           void* user_data) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  context->wake_user_data.store(user_data, std::memory_order_relaxed);
  context->wake_callback.store(callback, std::memory_order_release);
  return UI_OK;
}

ui_error_code ui_context_poll_event(ui_context* context, ui_event* out_event) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  ui_event event{};
  event.size = sizeof(ui_event);
  event.type = UI_EVENT_NONE;
  if (out_event == nullptr || out_event->size < sizeof(std::uint32_t) * 2) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  ui::capi::Engine& engine = *context->engine;
  if (context->wake_pending.exchange(false, std::memory_order_acq_rel)) {
    event.type = UI_EVENT_WAKE;
  } else if (engine.queue_head < engine.queue.size()) {
    const ui::capi::QueuedEvent& queued = engine.queue[engine.queue_head++];
    event.type = queued.type;
    event.code = queued.code;
    event.level = queued.level;
    event.surface = queued.surface;
    engine.polled_text.assign(engine.queue_text, queued.text_offset, queued.text_length);
    event.message = queued.text_length == 0 ? nullptr : engine.polled_text.data();
    event.message_length = queued.text_length;
    if (engine.queue_head == engine.queue.size()) {
      // Drained: reuse the storage (capacity is kept).
      engine.queue.clear();
      engine.queue_head = 0;
      engine.queue_text.clear();
    }
  }
  static_cast<void>(write_output(out_event, event));
  return UI_OK;
}

ui_error_code ui_context_wake(ui_context* context) {
  // Callable from any thread: only touches atomics.
  if (context == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  context->wake_pending.store(true, std::memory_order_release);
  if (const ui_wake_callback callback = context->wake_callback.load(std::memory_order_acquire)) {
    callback(context->wake_user_data.load(std::memory_order_relaxed));
  }
  return UI_OK;
}

ui_error_code ui_context_get_allocation_stats(const ui_context* context,
                                              ui_allocation_stats* out_stats) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  const ui::foundation::AllocationStats& stats = context->memory.stats();
  ui_allocation_stats value{};
  value.size = sizeof(ui_allocation_stats);
  value.live_allocations = stats.live_allocations;
  value.live_bytes = stats.live_bytes;
  value.peak_allocations = stats.peak_allocations;
  value.peak_bytes = stats.peak_bytes;
  value.total_allocations = stats.total_allocations;
  return write_output(out_stats, value) ? UI_OK : UI_ERROR_INVALID_ARGUMENT;
}

// --- Surfaces ------------------------------------------------------------------

ui_error_code ui_surface_create(ui_context* context, const ui_surface_desc* desc,
                                ui_handle* out_surface) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  ui_surface_desc options{};
  if (out_surface == nullptr || !read_input(desc, options) ||
      !ui::foundation::ScaleFactor::is_valid(options.scale)) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  const ApiScope scope(*context);
  ui::capi::Engine& engine = *context->engine;
  const ui::platform::SurfaceInfo info{
      {static_cast<float>(options.width), static_cast<float>(options.height)},
      ui::foundation::ScaleFactor{options.scale},
      options.refresh_rate_hz};
  auto entry =
      ui::foundation::allocate_unique<ui::capi::CapiSurface>(context->memory, *context, info);
  if (!entry) {
    return to_c(entry.error());
  }
  ui::capi::CapiSurface* surface = entry->get();
  engine.surfaces.push_back(std::move(entry).value());
  auto id = engine.core.add_surface(surface->surface);
  if (!id) {
    engine.surfaces.pop_back();
    return to_c(id.error());
  }
  *out_surface = id->bits();
  return UI_OK;
}

ui_error_code ui_surface_destroy(ui_context* context, ui_handle surface) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  const ApiScope scope(*context);
  auto found = ui::capi::resolve_surface(*context, surface, "ui_surface_destroy");
  if (!found) {
    return to_c(found.error());
  }
  // Deferred to the end of the outermost API call (ApiScope), so it is safe
  // from inside callbacks; the handle is stale from now on.
  (*found)->destroy_pending = true;
  context->engine->pending_destroy.push_back((*found)->surface.id());
  return UI_OK;
}

ui_error_code ui_surface_resize(ui_context* context, ui_handle surface, uint32_t width,
                                uint32_t height, float scale) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  const ApiScope scope(*context);
  auto found = ui::capi::resolve_surface(*context, surface, "ui_surface_resize");
  if (!found) {
    return to_c(found.error());
  }
  auto result = (*found)->surface.set_info({static_cast<float>(width), static_cast<float>(height)},
                                           scale, ui::foundation::Timestamp{},
                                           (*found)->surface.info().refresh_rate_hz);
  return result ? UI_OK : to_c(result.error());
}

ui_error_code ui_surface_frame(ui_context* context, ui_handle surface, int64_t timestamp_ns) {
  if (const ui_error_code status = check_context(context); status != UI_OK) {
    return status;
  }
  const ApiScope scope(*context);
  auto found = ui::capi::resolve_surface(*context, surface, "ui_surface_frame");
  if (!found) {
    return to_c(found.error());
  }
  context->engine->core.frame((*found)->surface.id(), ui::foundation::Timestamp{timestamp_ns});
  return UI_OK;
}

// --- Text ----------------------------------------------------------------------

ui_error_code ui_utf8_to_utf16_offset(const char* text, uint32_t text_length, uint32_t utf8_offset,
                                      uint32_t* out_utf16_offset) {
  if ((text == nullptr && text_length != 0) || out_utf16_offset == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  auto offset =
      ui::foundation::utf8_to_utf16_offset(std::string_view(text, text_length), utf8_offset);
  if (!offset) {
    return to_c(offset.error());
  }
  *out_utf16_offset = static_cast<uint32_t>(*offset);
  return UI_OK;
}

ui_error_code ui_utf16_to_utf8_offset(const char* text, uint32_t text_length, uint32_t utf16_offset,
                                      uint32_t* out_utf8_offset) {
  if ((text == nullptr && text_length != 0) || out_utf8_offset == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  auto offset =
      ui::foundation::utf16_to_utf8_offset(std::string_view(text, text_length), utf16_offset);
  if (!offset) {
    return to_c(offset.error());
  }
  *out_utf8_offset = static_cast<uint32_t>(*offset);
  return UI_OK;
}

}  // extern "C"
