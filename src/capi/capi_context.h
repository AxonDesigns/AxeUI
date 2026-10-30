#pragma once

// Internal: the C API's context object and helpers shared by every ui_*
// function. Not part of the public interface.

#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <thread>

#include "foundation/allocator.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "foundation/slot_map.h"
#include "foundation/tracking_resource.h"
#include "platform/core.h"
#include "platform/host_shell.h"
#include "ui/ui.h"

namespace ui::capi {

struct Engine;

// A host-owned surface registered through ui_surface_create().
struct CapiSurface {
  CapiSurface(ui_context& owner, const platform::SurfaceInfo& info) noexcept;

  ui_context* context;
  platform::HostSurface surface;
  bool destroy_pending = false;  // ui_surface_destroy() during dispatch.
};

struct QueuedEvent {
  std::int32_t type;
  std::int32_t code;
  std::int32_t level;
  std::uint64_t surface;
  std::size_t text_offset;
  std::uint32_t text_length;
};

// Everything that allocates, so it can be torn down before the context's
// own leak check.
struct Engine {
  explicit Engine(ui_context& context);
  ~Engine();

  platform::HostShell shell;
  platform::Core core;
  foundation::Vector<foundation::UniquePtr<CapiSurface>> surfaces;
  foundation::Vector<platform::SurfaceId> pending_destroy;
  foundation::Vector<QueuedEvent> queue;
  std::size_t queue_head = 0;
  foundation::String queue_text;   // Messages of queued events.
  foundation::String polled_text;  // Message of the last polled event.
  std::uint64_t dropped_events = 0;
};

}  // namespace ui::capi

struct ui_context {
  explicit ui_context(ui_log_level min_level) noexcept;
  ~ui_context();

  ui_context(const ui_context&) = delete;
  ui_context& operator=(const ui_context&) = delete;

  ui::foundation::HeapResource heap;
  // Every allocation the context makes goes through `memory`, so
  // ui_context_get_allocation_stats() covers all of it.
  ui::foundation::TrackingResource memory{heap};
  ui::foundation::Logger logger;

  std::thread::id owner = std::this_thread::get_id();
  int depth = 0;  // Nested API calls in progress (callbacks may re-enter).

  ui_log_callback log_callback = nullptr;
  void* log_user_data = nullptr;
  ui_event_callback event_callback = nullptr;
  void* event_user_data = nullptr;
  std::atomic<ui_wake_callback> wake_callback{nullptr};
  std::atomic<void*> wake_user_data{nullptr};
  std::atomic<bool> wake_pending{false};

  std::optional<ui::capi::Engine> engine;
};

namespace ui::capi {

inline constexpr std::size_t kMaxQueuedEvents = 4096;

[[nodiscard]] constexpr ui_error_code to_c(foundation::ErrorCode code) noexcept {
  return static_cast<ui_error_code>(code);
}

[[nodiscard]] constexpr ui_log_level to_c(foundation::LogLevel level) noexcept {
  return static_cast<ui_log_level>(level);
}

// Marks an API call in progress. When the outermost call returns, surface
// destruction requested during it (typically from a callback) is carried out.
class ApiScope {
 public:
  explicit ApiScope(ui_context& context) noexcept : context_(context) { ++context_.depth; }
  ~ApiScope();
  ApiScope(const ApiScope&) = delete;
  ApiScope& operator=(const ApiScope&) = delete;

 private:
  ui_context& context_;
};

// The context's engine. It exists from ui_context_create() until the context
// is destroyed, so every API function may use it after check_context().
[[nodiscard]] inline Engine& engine(ui_context& context) noexcept {
  UI_ASSERT(context.engine.has_value(), "context has no engine");
  return *context.engine;  // NOLINT(bugprone-unchecked-optional-access): asserted above.
}

// Validates the context pointer and the calling thread.
[[nodiscard]] ui_error_code check_context(const ui_context* context) noexcept;

// Delivers an engine-to-host event: to the event callback if one is set,
// otherwise into the queue drained by ui_context_poll_event().
void deliver_event(ui_context& context, ui_event_type type, ui_handle surface, ui_error_code code,
                   ui_log_level level, std::string_view message);

// Reports a handle that failed to resolve in `function` as a diagnostic.
void report_bad_handle(const ui_context& context, const foundation::Error& error, ui_handle handle,
                       const char* function);

// Resolves a handle received through the C API. Every ui_* function that
// takes a handle uses this, so stale, mistyped, null and forged handles are
// always reported and turned into an error code instead of undefined
// behavior.
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

// Resolves a surface handle (also rejecting surfaces whose destruction is
// pending).
[[nodiscard]] foundation::Result<CapiSurface*> resolve_surface(ui_context& context,
                                                               ui_handle handle,
                                                               const char* function);

}  // namespace ui::capi
