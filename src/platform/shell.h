#pragma once

#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "foundation/geometry.h"
#include "foundation/result.h"
#include "platform/surface.h"

namespace ui::platform {

class Core;

enum class CursorShape : std::uint8_t {
  Default,
  Text,
  Pointer,  // Hand, for links and clickable items.
  Crosshair,
  Move,
  NotAllowed,
  Wait,
  Progress,
  Grab,
  Grabbing,
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  ResizeColumn,
  ResizeRow,
  Hidden,
};

// The platform contract: everything the core needs from the OS, behind one
// interface. Thin per-platform shells implement it; the core contains no
// OS-specific code.
//
// Direction of calls:
//   shell -> core: Core::dispatch(const Event&) for input and lifecycle,
//                  Core::frame(Timestamp) once per display refresh it was asked for.
//   core -> shell: the virtual functions below.
//
// The core never owns the main loop. It asks for frames with request_frame()
// and the shell delivers them from the platform's vsync source
// (requestAnimationFrame, CADisplayLink, Choreographer, a swapchain present
// loop). All calls happen on the UI thread.
//
// Graphics surface (placeholder): creating, configuring and presenting the
// WebGPU surface is part of this contract but is wired in the renderer task.
// It will be a separate set of functions returning the platform's native
// surface source (HWND, CAMetalLayer, ANativeWindow, canvas selector, ...)
// for wgpu::Instance::CreateSurface, plus surface-lost / recreated handling
// (see docs/architecture.md, "Known platform hazards").
class Shell {
 public:
  Shell(const Shell&) = delete;
  Shell& operator=(const Shell&) = delete;
  virtual ~Shell() = default;

  // Current drawable size and scale factor.
  [[nodiscard]] virtual SurfaceInfo surface_info() const = 0;

  // Asks for one call to Core::frame() at the next vsync. Must return
  // immediately: never block, sleep or spin. Repeated requests before the
  // frame arrives may be coalesced (the core already avoids sending them).
  virtual void request_frame() = 0;

  virtual void set_cursor(CursorShape cursor) = 0;

  // Where the text cursor is, so the IME can place its candidate window.
  virtual void set_ime_cursor_area(const foundation::LogicalRect& area) = 0;

  // Replaces `out` with the clipboard's UTF-8 text. Fails with Unavailable
  // where synchronous clipboard reads are impossible (the web delivers
  // clipboard contents only with paste events).
  [[nodiscard]] virtual foundation::Result<void> clipboard_get_text(foundation::String& out) = 0;
  [[nodiscard]] virtual foundation::Result<void> clipboard_set_text(std::string_view text) = 0;

  // Called by Core's constructor (with the core) and destructor (with
  // nullptr). Shells that push events into the core keep the pointer.
  virtual void attach_core(Core* /*core*/) noexcept {}

 protected:
  Shell() = default;
};

}  // namespace ui::platform
