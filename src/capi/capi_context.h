#pragma once

// Internal: the C API's context object and helpers shared by every ui_*
// function. Not part of the public interface.

#include <cinttypes>

#include "foundation/allocator.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "foundation/slot_map.h"
#include "foundation/tracking_resource.h"
#include "ui/ui.h"

struct ui_context {
  ui_context() noexcept;
  ~ui_context();

  ui_context(const ui_context&) = delete;
  ui_context& operator=(const ui_context&) = delete;

  ui::foundation::HeapResource heap;
  // Every allocation the context makes goes through `memory`, so
  // ui_context_get_allocation_stats() covers all of it.
  ui::foundation::TrackingResource memory{heap};
  ui::foundation::Logger logger;

  ui_log_callback log_callback = nullptr;
  void* log_user_data = nullptr;
};

namespace ui::capi {

[[nodiscard]] constexpr ui_error_code to_c(foundation::ErrorCode code) noexcept {
  return static_cast<ui_error_code>(code);
}

[[nodiscard]] constexpr ui_log_level to_c(foundation::LogLevel level) noexcept {
  return static_cast<ui_log_level>(level);
}

// Reports a handle that failed to resolve in `function` through the
// context's log callback.
void report_bad_handle(const ui_context& context, const foundation::Error& error, ui_handle handle,
                       const char* function);

// Resolves a handle received through the C API. Every ui_* function that
// takes a handle uses this, so stale, mistyped and null handles are always
// reported and turned into an error code instead of undefined behavior.
template <class T>
[[nodiscard]] foundation::Result<T*> resolve(const ui_context& context,
                                             foundation::SlotMap<T>& objects, ui_handle handle,
                                             const char* function) {
  auto object = objects.get(foundation::Handle::from_bits(handle));
  if (!object) {
    report_bad_handle(context, object.error(), handle, function);
  }
  return object;
}

}  // namespace ui::capi
