#pragma once

#include <cmath>

#include "foundation/geometry.h"

namespace ui::platform {

// The drawable surface as the shell reports it: physical pixel size plus the
// scale factor that maps logical to physical pixels.
struct SurfaceInfo {
  foundation::PhysicalSize physical_size;
  foundation::ScaleFactor scale{1.0f};

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

}  // namespace ui::platform
