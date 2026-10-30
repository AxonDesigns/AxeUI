#include "capi/capi_context.h"

#include <algorithm>
#include <cstdio>

namespace ui::capi {

namespace {

// Bridges the engine Logger to the log callback, or to the event queue.
void forward_log(void* user_data, const foundation::LogRecord& record) {
  auto* context = static_cast<ui_context*>(user_data);
  if (context->log_callback != nullptr) {
    context->log_callback(context->log_user_data, to_c(record.level), to_c(record.code),
                          record.message.data(), static_cast<std::uint32_t>(record.message.size()));
    return;
  }
  if (context->engine.has_value()) {
    deliver_event(*context, UI_EVENT_LOG, UI_NULL_HANDLE, to_c(record.code), to_c(record.level),
                  record.message);
  }
}

void request_frame(void* user_data) {
  auto* surface = static_cast<CapiSurface*>(user_data);
  deliver_event(*surface->context, UI_EVENT_FRAME_REQUESTED, surface->surface.id().bits(), UI_OK,
                UI_LOG_TRACE, {});
}

}  // namespace

CapiSurface::CapiSurface(ui_context& owner, const platform::SurfaceInfo& info) noexcept
    : context(&owner),
      surface(info, platform::NativeSurfaceHandle::none(),
              platform::HostSurfaceCallbacks{this, &request_frame, nullptr, nullptr}) {}

Engine::Engine(ui_context& context)
    : shell(platform::HostShellCallbacks{}),
      core(shell, context.memory, context.logger),
      surfaces(foundation::Allocator<foundation::UniquePtr<CapiSurface>>(context.memory)),
      pending_destroy(foundation::Allocator<platform::SurfaceId>(context.memory)),
      queue(foundation::Allocator<QueuedEvent>(context.memory)),
      queue_text(foundation::Allocator<char>(context.memory)),
      polled_text(foundation::Allocator<char>(context.memory)) {}

Engine::~Engine() {
  // Unregister surfaces before they are destroyed.
  for (const auto& entry : surfaces) {
    static_cast<void>(core.remove_surface(entry->surface.id()));
  }
  surfaces.clear();
}

ApiScope::~ApiScope() {
  if (--context_.depth > 0 || !context_.engine.has_value()) {
    return;
  }
  Engine& engine = capi::engine(context_);
  // Destruction requested during dispatch happens now, outside of it.
  while (!engine.pending_destroy.empty()) {
    const platform::SurfaceId id = engine.pending_destroy.back();
    engine.pending_destroy.pop_back();
    ++context_.depth;  // remove_surface may log; keep further requests deferred.
    static_cast<void>(engine.core.remove_surface(id));
    --context_.depth;
  }
  // Removal detached the surfaces (their ids are null now), so match on the
  // pending flag rather than the id.
  std::erase_if(engine.surfaces, [](const auto& entry) { return entry->destroy_pending; });
}

ui_error_code check_context(const ui_context* context) noexcept {
  if (context == nullptr) {
    return UI_ERROR_INVALID_ARGUMENT;
  }
  if (std::this_thread::get_id() != context->owner) {
    return UI_ERROR_WRONG_THREAD;
  }
  return UI_OK;
}

void deliver_event(ui_context& context, ui_event_type type, ui_handle surface, ui_error_code code,
                   ui_log_level level, std::string_view message) {
  if (context.event_callback != nullptr) {
    ui_event event{};
    event.size = sizeof(ui_event);
    event.type = type;
    event.code = code;
    event.level = level;
    event.surface = surface;
    event.message = message.empty() ? nullptr : message.data();
    event.message_length = static_cast<std::uint32_t>(message.size());
    context.event_callback(context.event_user_data, &event);
    return;
  }
  Engine& engine = capi::engine(context);
  if (engine.queue.size() - engine.queue_head >= kMaxQueuedEvents) {
    ++engine.dropped_events;  // The host is not draining the queue.
    return;
  }
  const std::size_t offset = engine.queue_text.size();
  engine.queue_text.append(message);
  engine.queue.push_back(
      QueuedEvent{type, code, level, surface, offset, static_cast<std::uint32_t>(message.size())});
}

void report_bad_handle(const ui_context& context, const foundation::Error& error, ui_handle handle,
                       const char* function) {
  UI_LOGF(context.logger, foundation::LogLevel::Error, error.code,
          "%s: %s (handle 0x%016" PRIx64 ")", function, error.message, handle);
}

foundation::Result<CapiSurface*> resolve_surface(ui_context& context, ui_handle handle,
                                                 const char* function) {
  Engine& engine = capi::engine(context);
  const platform::SurfaceId id{foundation::Handle::from_bits(handle)};
  auto state = engine.core.surface(id);
  if (!state) {
    report_bad_handle(context, state.error(), handle, function);
    return foundation::fail(std::move(state).error());
  }
  for (const auto& entry : engine.surfaces) {
    if (entry->surface.id() == id && !entry->destroy_pending) {
      return entry.get();
    }
  }
  const foundation::Error stale{foundation::ErrorCode::StaleHandle, "surface is being destroyed"};
  report_bad_handle(context, stale, handle, function);
  return foundation::fail(stale);
}

}  // namespace ui::capi

ui_context::ui_context(ui_log_level min_level) noexcept
    : logger(&ui::capi::forward_log, this, static_cast<ui::foundation::LogLevel>(min_level)) {}

ui_context::~ui_context() {
  engine.reset();
  // Everything the context allocated is gone now; anything left is a leak.
  const ui::foundation::AllocationStats& stats = memory.stats();
  if (stats.live_allocations != 0 && log_callback != nullptr) {
    char message[160];
    const int length = std::snprintf(message, sizeof(message),
                                     "ui_context_destroy: %zu allocation(s) (%zu bytes) still live",
                                     stats.live_allocations, stats.live_bytes);
    log_callback(log_user_data, UI_LOG_ERROR, UI_ERROR_INTERNAL, message,
                 static_cast<std::uint32_t>(std::max(length, 0)));
  }
}
