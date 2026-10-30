#pragma once

#include <compare>
#include <limits>

#include "foundation/assertions.h"

namespace ui::foundation {

// Coordinate-space tags. Geometry types are parameterized by space, so a
// logical rect cannot be passed where a physical rect is expected; moving
// between the two requires an explicit ScaleFactor.
struct LogicalSpace {};   // Device-independent pixels (what layout and widgets use).
struct PhysicalSpace {};  // Actual framebuffer pixels.
struct TextureSpace {};   // Normalized texture coordinates, [0, 1].

// A one-dimensional length in a given space.
template <class Space>
struct Length {
  float value = 0.0f;

  constexpr Length() = default;
  constexpr explicit Length(float v) noexcept : value(v) {}

  constexpr Length operator-() const noexcept { return Length{-value}; }
  constexpr Length& operator+=(Length other) noexcept {
    value += other.value;
    return *this;
  }
  constexpr Length& operator-=(Length other) noexcept {
    value -= other.value;
    return *this;
  }
  constexpr Length& operator*=(float factor) noexcept {
    value *= factor;
    return *this;
  }
  constexpr Length& operator/=(float divisor) noexcept {
    value /= divisor;
    return *this;
  }

  friend constexpr Length operator+(Length a, Length b) noexcept {
    return Length{a.value + b.value};
  }
  friend constexpr Length operator-(Length a, Length b) noexcept {
    return Length{a.value - b.value};
  }
  friend constexpr Length operator*(Length a, float f) noexcept { return Length{a.value * f}; }
  friend constexpr Length operator*(float f, Length a) noexcept { return Length{a.value * f}; }
  friend constexpr Length operator/(Length a, float f) noexcept { return Length{a.value / f}; }
  // Ratio of two lengths in the same space is unitless.
  friend constexpr float operator/(Length a, Length b) noexcept { return a.value / b.value; }

  friend constexpr bool operator==(Length, Length) = default;
  friend constexpr auto operator<=>(Length, Length) = default;
};

using LogicalPx = Length<LogicalSpace>;
using PhysicalPx = Length<PhysicalSpace>;

// Physical pixels per logical pixel. Always finite and strictly positive.
class ScaleFactor {
 public:
  static constexpr float kMin = 1.0f / 64.0f;
  static constexpr float kMax = 64.0f;

  // Platform-provided values must be checked with is_valid() first;
  // constructing from an invalid value is a programming error.
  constexpr explicit ScaleFactor(float factor) : value_(factor) {
    UI_ASSERT(is_valid(factor), "scale factor must be finite and within [1/64, 64]");
  }

  [[nodiscard]] static constexpr bool is_valid(float factor) noexcept {
    return factor >= kMin && factor <= kMax;  // Also rejects NaN.
  }

  [[nodiscard]] constexpr float value() const noexcept { return value_; }

  friend constexpr bool operator==(ScaleFactor, ScaleFactor) = default;

 private:
  float value_;
};

[[nodiscard]] constexpr PhysicalPx to_physical(LogicalPx length, ScaleFactor scale) noexcept {
  return PhysicalPx{length.value * scale.value()};
}

[[nodiscard]] constexpr LogicalPx to_logical(PhysicalPx length, ScaleFactor scale) noexcept {
  return LogicalPx{length.value / scale.value()};
}

}  // namespace ui::foundation
