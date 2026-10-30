#pragma once

#include <cmath>
#include <cstdint>

#include "foundation/geometry.h"
#include "foundation/handle.h"
#include "platform/native_surface.h"

namespace ui::platform {

class Core;

// Identifies a surface attached to a Core. Issued by Core::add_surface();
// carries HandleType::Surface, so it fits in 53 bits like every handle.
struct SurfaceId {
  foundation::Handle handle;

  [[nodiscard]] constexpr bool is_null() const noexcept { return handle.is_null(); }
  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return handle.bits(); }
  friend constexpr bool operator==(SurfaceId, SurfaceId) = default;
};

// The drawable area of a surface as its shell reports it: physical pixel size
// plus the scale factor that maps logical to physical pixels.
struct SurfaceInfo {
  foundation::PhysicalSize physical_size;
  foundation::ScaleFactor scale{1.0f};
  // Refresh rate of the display the surface is on, in Hz; 0 if unknown.
  float refresh_rate_hz = 0.0f;

  [[nodiscard]] constexpr foundation::LogicalSize logical_size() const noexcept {
    return foundation::to_logical(physical_size, scale);
  }

  // Sizes must be finite and non-negative. Zero is allowed (minimized).
  [[nodiscard]] static bool is_valid_size(foundation::PhysicalSize size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height) && size.width >= 0.0f &&
           size.height >= 0.0f;
  }

  friend constexpr bool operator==(const SurfaceInfo&, const SurfaceInfo&) = default;
};

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

// One drawable area owned by a shell: a desktop window, a mobile view, a
// canvas, or an embedding host's widget. Part of the platform contract (see
// Platform in platform.h for the process-wide part).
//
// A shell creates a Surface, then registers it with Core::add_surface(),
// which returns its SurfaceId. Every event about the surface carries that id.
// All calls happen on the UI thread.
class Surface {
 public:
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;
  virtual ~Surface() = default;

  // Current size and scale factor.
  [[nodiscard]] virtual SurfaceInfo info() const = 0;

  // The OS object to create a GPU surface from, or NativeSurfaceKind::None.
  [[nodiscard]] virtual NativeSurfaceHandle native_handle() const = 0;

  // Asks for one Core::frame(id, time) call for this surface at its next
  // vsync. Must return immediately: never block, sleep or spin.
  virtual void request_frame() = 0;

  virtual void set_cursor(CursorShape cursor) = 0;

  // Where the text cursor is, so the IME can place its candidate window.
  virtual void set_ime_cursor_area(const foundation::LogicalRect& area) = 0;

  // Called by Core::add_surface (with the core and the new id) and when the
  // surface is removed (with nullptr and a null id).
  virtual void attach(Core* /*core*/, SurfaceId /*id*/) noexcept {}

 protected:
  Surface() = default;
};

}  // namespace ui::platform
