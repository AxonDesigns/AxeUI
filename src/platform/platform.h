#pragma once

#include <string_view>

#include "foundation/allocator.h"
#include "foundation/result.h"

namespace ui::platform {

class Core;

// The process-wide part of the platform contract. Per-window functionality
// lives in Surface (surface.h).
//
// Direction of calls:
//   shell -> core: Core::dispatch(const Event&) for input, window and
//                  lifecycle events; Core::frame(SurfaceId, Timestamp) once
//                  per frame a surface asked for.
//   core -> shell: Platform and Surface virtual functions.
//
// The core never owns the main loop. Surfaces ask for frames and the shell
// delivers them from the platform's vsync source (requestAnimationFrame,
// CADisplayLink, Choreographer, a swapchain present loop). Every call happens
// on the UI thread, except wake().
class Platform {
 public:
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;
  virtual ~Platform() = default;

  // Replaces `out` with the clipboard's UTF-8 text. Fails with Unavailable
  // where synchronous clipboard reads are impossible (the web delivers
  // clipboard contents only with paste events).
  [[nodiscard]] virtual foundation::Result<void> clipboard_get_text(foundation::String& out) = 0;
  [[nodiscard]] virtual foundation::Result<void> clipboard_set_text(std::string_view text) = 0;

  // Wakes the shell's event loop. The only function that may be called from
  // any thread; it must be cheap and must not call back into the core.
  virtual void wake() noexcept = 0;

  // Called by Core's constructor (with the core) and destructor (with
  // nullptr). Shells that push events into the core keep the pointer.
  virtual void attach_core(Core* /*core*/) noexcept {}

 protected:
  Platform() = default;
};

}  // namespace ui::platform
