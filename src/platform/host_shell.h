#pragma once

#include <string_view>

#include "foundation/allocator.h"
#include "foundation/result.h"
#include "foundation/timestamp.h"
#include "platform/event.h"
#include "platform/native_surface.h"
#include "platform/platform.h"
#include "platform/surface.h"

namespace ui::platform {

// Process-wide callbacks through which a HostShell answers the core. Any
// callback may be null: clipboard calls then fail with Unavailable and wake()
// does nothing.
struct HostShellCallbacks {
  void* user_data = nullptr;
  foundation::Result<void> (*clipboard_get_text)(void* user_data,
                                                 foundation::String& out) = nullptr;
  foundation::Result<void> (*clipboard_set_text)(void* user_data, std::string_view text) = nullptr;
  // Must be safe to call from any thread.
  void (*wake)(void* user_data) = nullptr;
};

// Per-surface callbacks of a HostSurface. Any callback may be null: the call
// is then dropped.
struct HostSurfaceCallbacks {
  void* user_data = nullptr;
  void (*request_frame)(void* user_data) = nullptr;
  void (*set_cursor)(void* user_data, CursorShape cursor) = nullptr;
  void (*set_ime_cursor_area)(void* user_data, const foundation::LogicalRect& area) = nullptr;
};

// A shell for when the application owns the windows and the loop: embedding
// the engine in another app or game engine, the web and mobile glue, the C
// API, and tests.
//
// The application reports surfaces, pushes events and frame ticks in, and
// answers the core's requests through callbacks. Events and ticks go
// straight to Core::dispatch() and Core::frame(), the same path every shell
// uses.
//
// Usage:
//   HostShell shell(callbacks);
//   Core core(shell, memory, logger);         // Attaches itself to the shell.
//   HostSurface window(info, native, surface_callbacks);
//   SurfaceId id = *core.add_surface(window);
//   shell.push_event(Event::pointer_move(id, now, e));  // -> core.dispatch
//   shell.tick(id, now);                     // -> core.frame, after request_frame
class HostShell final : public Platform {
 public:
  explicit HostShell(const HostShellCallbacks& callbacks) noexcept : callbacks_(callbacks) {}
  ~HostShell() override;

  // Forwards to Core::dispatch().
  void push_event(const Event& event);
  // Forwards to Core::frame().
  void tick(SurfaceId id, foundation::Timestamp timestamp);

  [[nodiscard]] bool attached() const noexcept { return core_ != nullptr; }
  [[nodiscard]] Core* core() const noexcept { return core_; }

  // Platform implementation.
  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out) override;
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text) override;
  void wake() noexcept override;
  void attach_core(Core* core) noexcept override;

 private:
  HostShellCallbacks callbacks_;
  Core* core_ = nullptr;
};

// A surface whose size, scale and native handle the application supplies.
class HostSurface final : public Surface {
 public:
  HostSurface(const SurfaceInfo& info, const NativeSurfaceHandle& native,
              const HostSurfaceCallbacks& callbacks) noexcept
      : info_(info), native_(native), callbacks_(callbacks) {}
  ~HostSurface() override;

  // The id the core assigned, or a null id while not attached.
  [[nodiscard]] SurfaceId id() const noexcept { return id_; }

  // Records a new size / scale / refresh rate and dispatches
  // ScaleFactorChanged and/or Resize for what changed. Fails with
  // InvalidArgument (dispatching nothing) for a negative or non-finite size or
  // an out-of-range scale, and with InvalidState when not attached.
  [[nodiscard]] foundation::Result<void> set_info(foundation::PhysicalSize size, float scale,
                                                  foundation::Timestamp time,
                                                  float refresh_rate_hz = 0.0f);

  // Replaces the native handle (e.g. after the OS recreated the window
  // surface). Takes effect the next time the renderer configures the surface.
  void set_native_handle(const NativeSurfaceHandle& native) noexcept { native_ = native; }

  // Surface implementation.
  [[nodiscard]] SurfaceInfo info() const override { return info_; }
  [[nodiscard]] NativeSurfaceHandle native_handle() const override { return native_; }
  void request_frame() override;
  void set_cursor(CursorShape cursor) override;
  void set_ime_cursor_area(const foundation::LogicalRect& area) override;
  void attach(Core* core, SurfaceId id) noexcept override;

 private:
  SurfaceInfo info_;
  NativeSurfaceHandle native_;
  HostSurfaceCallbacks callbacks_;
  Core* core_ = nullptr;
  SurfaceId id_;
};

}  // namespace ui::platform
