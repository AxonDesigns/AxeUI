#pragma once

#include <string_view>

#include "foundation/allocator.h"
#include "foundation/result.h"
#include "foundation/timestamp.h"
#include "platform/event.h"
#include "platform/shell.h"

namespace ui::platform {

// Callbacks through which a HostShell answers the core. Any callback may be
// null: frame requests, cursor and IME updates are then dropped, and
// clipboard calls fail with ErrorCode::Unavailable.
struct HostShellCallbacks {
  void* user_data = nullptr;
  void (*request_frame)(void* user_data) = nullptr;
  void (*set_cursor)(void* user_data, CursorShape cursor) = nullptr;
  void (*set_ime_cursor_area)(void* user_data, const foundation::LogicalRect& area) = nullptr;
  foundation::Result<void> (*clipboard_get_text)(void* user_data,
                                                 foundation::String& out) = nullptr;
  foundation::Result<void> (*clipboard_set_text)(void* user_data, std::string_view text) = nullptr;
};

// A shell for when the application owns the window and the loop: embedding
// the engine in another app or game engine, the web and mobile glue, and
// tests.
//
// The application reports the surface, pushes events and frame ticks in, and
// answers the core's requests through HostShellCallbacks. Events and ticks go
// straight to Core::dispatch() and Core::frame(), the same path every shell
// uses.
//
// Usage:
//   HostShell shell(surface, callbacks);
//   Core core(shell, memory, logger);  // Attaches itself to the shell.
//   shell.push_event(event);           // -> core.dispatch(event)
//   shell.tick(now);                   // -> core.frame(now), after request_frame
class HostShell final : public Shell {
 public:
  HostShell(const SurfaceInfo& surface, const HostShellCallbacks& callbacks) noexcept;
  ~HostShell() override;

  // Forwards to Core::dispatch(). Resize and ScaleFactorChanged events also
  // update the surface this shell reports.
  void push_event(const Event& event);

  // Forwards to Core::frame().
  void tick(foundation::Timestamp timestamp);

  // Convenience for applications that track the native size: dispatches
  // ScaleFactorChanged and/or Resize for whatever changed. Fails with
  // InvalidArgument (and dispatches nothing) for a negative or non-finite
  // size or an out-of-range scale.
  [[nodiscard]] foundation::Result<void> set_surface(foundation::PhysicalSize size, float scale,
                                                     foundation::Timestamp time);

  [[nodiscard]] bool attached() const noexcept { return core_ != nullptr; }

  // Shell implementation.
  [[nodiscard]] SurfaceInfo surface_info() const override { return surface_; }
  void request_frame() override;
  void set_cursor(CursorShape cursor) override;
  void set_ime_cursor_area(const foundation::LogicalRect& area) override;
  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out) override;
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text) override;
  void attach_core(Core* core) noexcept override;

 private:
  SurfaceInfo surface_;
  HostShellCallbacks callbacks_;
  Core* core_ = nullptr;
};

}  // namespace ui::platform
