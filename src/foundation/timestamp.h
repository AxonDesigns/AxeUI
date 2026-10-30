#pragma once

#include <compare>
#include <cstdint>

namespace ui::foundation {

// A point on the shell's monotonic clock, in nanoseconds. The epoch is
// arbitrary (each shell picks one); only differences are meaningful.
struct Timestamp {
  std::int64_t nanoseconds = 0;

  [[nodiscard]] static constexpr Timestamp from_nanoseconds(std::int64_t ns) noexcept {
    return Timestamp{ns};
  }
  // requestAnimationFrame and performance.now() report milliseconds as double.
  [[nodiscard]] static constexpr Timestamp from_milliseconds(double ms) noexcept {
    return Timestamp{static_cast<std::int64_t>(ms * 1'000'000.0)};
  }

  [[nodiscard]] constexpr double seconds() const noexcept {
    return static_cast<double>(nanoseconds) / 1'000'000'000.0;
  }

  friend constexpr bool operator==(Timestamp, Timestamp) = default;
  friend constexpr auto operator<=>(Timestamp, Timestamp) = default;
};

}  // namespace ui::foundation
