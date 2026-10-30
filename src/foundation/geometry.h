#pragma once

#include <algorithm>

#include "foundation/result.h"
#include "foundation/units.h"

namespace ui::foundation {

// All geometry is float-based and tagged with a coordinate space (see
// units.h). Operations are only defined between values of the same space.

template <class Space>
struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Vec2 operator-() const noexcept { return {-x, -y}; }
  constexpr Vec2& operator+=(Vec2 o) noexcept {
    x += o.x;
    y += o.y;
    return *this;
  }
  constexpr Vec2& operator-=(Vec2 o) noexcept {
    x -= o.x;
    y -= o.y;
    return *this;
  }

  friend constexpr Vec2 operator+(Vec2 a, Vec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
  friend constexpr Vec2 operator-(Vec2 a, Vec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
  friend constexpr Vec2 operator*(Vec2 a, float f) noexcept { return {a.x * f, a.y * f}; }
  friend constexpr Vec2 operator*(float f, Vec2 a) noexcept { return {a.x * f, a.y * f}; }
  friend constexpr Vec2 operator/(Vec2 a, float f) noexcept { return {a.x / f, a.y / f}; }
  friend constexpr bool operator==(Vec2, Vec2) = default;
};

template <class Space>
struct Size {
  float width = 0.0f;
  float height = 0.0f;

  // Empty when either dimension is zero, negative or NaN.
  [[nodiscard]] constexpr bool is_empty() const noexcept {
    return !(width > 0.0f && height > 0.0f);
  }

  friend constexpr Size operator*(Size s, float f) noexcept { return {s.width * f, s.height * f}; }
  friend constexpr bool operator==(Size, Size) = default;
};

// Edge offsets, e.g. padding or a safe-area. Positive values shrink a rect
// when applied with Rect::inset().
template <class Space>
struct Insets {
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
  float left = 0.0f;

  [[nodiscard]] static constexpr Insets uniform(float v) noexcept { return {v, v, v, v}; }
  [[nodiscard]] static constexpr Insets symmetric(float vertical, float horizontal) noexcept {
    return {vertical, horizontal, vertical, horizontal};
  }

  [[nodiscard]] constexpr float horizontal() const noexcept { return left + right; }
  [[nodiscard]] constexpr float vertical() const noexcept { return top + bottom; }

  friend constexpr Insets operator*(Insets i, float f) noexcept {
    return {i.top * f, i.right * f, i.bottom * f, i.left * f};
  }
  friend constexpr bool operator==(Insets, Insets) = default;
};

// Axis-aligned rectangle stored as edges. The covered area is the half-open
// range [left, right) x [top, bottom).
//
// A rect whose right <= left or bottom <= top (or with any NaN edge) is
// "empty": it contains nothing, intersects nothing and does not contribute
// to unions. Negative-size rects are empty, not mirrored; use normalized()
// to turn one into the rect spanning the same corners.
template <class Space>
struct Rect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;

  [[nodiscard]] static constexpr Rect from_ltrb(float l, float t, float r, float b) noexcept {
    return {l, t, r, b};
  }
  [[nodiscard]] static constexpr Rect from_xywh(float x, float y, float w, float h) noexcept {
    return {x, y, x + w, y + h};
  }
  [[nodiscard]] static constexpr Rect from_origin_size(Vec2<Space> origin,
                                                       Size<Space> size) noexcept {
    return from_xywh(origin.x, origin.y, size.width, size.height);
  }

  [[nodiscard]] constexpr float width() const noexcept { return right - left; }
  [[nodiscard]] constexpr float height() const noexcept { return bottom - top; }
  [[nodiscard]] constexpr Vec2<Space> origin() const noexcept { return {left, top}; }
  [[nodiscard]] constexpr Size<Space> size() const noexcept { return {width(), height()}; }
  [[nodiscard]] constexpr Vec2<Space> center() const noexcept {
    return {(left + right) * 0.5f, (top + bottom) * 0.5f};
  }

  [[nodiscard]] constexpr bool is_empty() const noexcept { return !(right > left && bottom > top); }

  // Half-open: points on the right and bottom edges are outside.
  [[nodiscard]] constexpr bool contains(Vec2<Space> p) const noexcept {
    return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
  }

  // True if `other` is non-empty and lies entirely inside this rect.
  [[nodiscard]] constexpr bool contains(const Rect& other) const noexcept {
    return !other.is_empty() && !is_empty() && other.left >= left && other.top >= top &&
           other.right <= right && other.bottom <= bottom;
  }

  // True if the rects share any area. Touching edges do not intersect.
  [[nodiscard]] constexpr bool intersects(const Rect& other) const noexcept {
    return !intersection(other).is_empty();
  }

  // The overlapping area, or Rect{} (all zero) when there is none.
  [[nodiscard]] constexpr Rect intersection(const Rect& other) const noexcept {
    const Rect r{std::max(left, other.left), std::max(top, other.top), std::min(right, other.right),
                 std::min(bottom, other.bottom)};
    return r.is_empty() ? Rect{} : r;
  }

  // Smallest rect containing both. Empty inputs are ignored; the union of
  // two empty rects is Rect{}.
  [[nodiscard]] constexpr Rect union_with(const Rect& other) const noexcept {
    if (is_empty()) {
      return other.is_empty() ? Rect{} : other;
    }
    if (other.is_empty()) {
      return *this;
    }
    return {std::min(left, other.left), std::min(top, other.top), std::max(right, other.right),
            std::max(bottom, other.bottom)};
  }

  [[nodiscard]] constexpr Rect translated(Vec2<Space> offset) const noexcept {
    return {left + offset.x, top + offset.y, right + offset.x, bottom + offset.y};
  }

  // Moves each edge inward by the matching inset (outward for negative
  // insets). May produce an empty rect.
  [[nodiscard]] constexpr Rect inset(const Insets<Space>& i) const noexcept {
    return {left + i.left, top + i.top, right - i.right, bottom - i.bottom};
  }
  [[nodiscard]] constexpr Rect outset(const Insets<Space>& i) const noexcept {
    return {left - i.left, top - i.top, right + i.right, bottom + i.bottom};
  }

  // Swaps edges so that left <= right and top <= bottom.
  [[nodiscard]] constexpr Rect normalized() const noexcept {
    return {std::min(left, right), std::min(top, bottom), std::max(left, right),
            std::max(top, bottom)};
  }

  friend constexpr Rect operator*(const Rect& r, float f) noexcept {
    return {r.left * f, r.top * f, r.right * f, r.bottom * f};
  }
  friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

using LogicalVec2 = Vec2<LogicalSpace>;
using PhysicalVec2 = Vec2<PhysicalSpace>;
using LogicalSize = Size<LogicalSpace>;
using PhysicalSize = Size<PhysicalSpace>;
using LogicalRect = Rect<LogicalSpace>;
using PhysicalRect = Rect<PhysicalSpace>;
using TextureRect = Rect<TextureSpace>;
using LogicalInsets = Insets<LogicalSpace>;
using PhysicalInsets = Insets<PhysicalSpace>;

// 2D affine transform mapping points within one coordinate space:
//
//   | a  c  tx |   | x |
//   | b  d  ty | * | y |
//                  | 1 |
//
// Composition follows matrix multiplication: (A * B) applies B first.
struct Affine2 {
  float a = 1.0f;
  float b = 0.0f;
  float c = 0.0f;
  float d = 1.0f;
  float tx = 0.0f;
  float ty = 0.0f;

  [[nodiscard]] static constexpr Affine2 identity() noexcept { return {}; }
  [[nodiscard]] static constexpr Affine2 translation(float x, float y) noexcept {
    return {1.0f, 0.0f, 0.0f, 1.0f, x, y};
  }
  [[nodiscard]] static constexpr Affine2 scale(float sx, float sy) noexcept {
    return {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
  }
  // Counter-clockwise in a y-up system; clockwise on screen (y-down).
  [[nodiscard]] static Affine2 rotation(float radians) noexcept;

  [[nodiscard]] constexpr float determinant() const noexcept { return a * d - b * c; }

  [[nodiscard]] constexpr bool is_identity() const noexcept { return *this == Affine2{}; }

  // True when the transform has no rotation or shear, so it maps axis-aligned
  // rects to axis-aligned rects.
  [[nodiscard]] constexpr bool is_scale_translate() const noexcept {
    return b == 0.0f && c == 0.0f;
  }

  // Fails with InvalidArgument when the matrix is singular or the inverse is
  // not finite.
  [[nodiscard]] Result<Affine2> inverse() const noexcept;

  template <class Space>
  [[nodiscard]] constexpr Vec2<Space> transform_point(Vec2<Space> p) const noexcept {
    return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty};
  }

  // Applies the linear part only (no translation).
  template <class Space>
  [[nodiscard]] constexpr Vec2<Space> transform_vector(Vec2<Space> v) const noexcept {
    return {a * v.x + c * v.y, b * v.x + d * v.y};
  }

  // Axis-aligned bounding box of the transformed rect. Exact when
  // is_scale_translate(). Empty rects stay empty (returns Rect{}).
  template <class Space>
  [[nodiscard]] constexpr Rect<Space> transform_rect(const Rect<Space>& r) const noexcept {
    if (r.is_empty()) {
      return {};
    }
    const Vec2<Space> p0 = transform_point(Vec2<Space>{r.left, r.top});
    const Vec2<Space> p1 = transform_point(Vec2<Space>{r.right, r.top});
    const Vec2<Space> p2 = transform_point(Vec2<Space>{r.left, r.bottom});
    const Vec2<Space> p3 = transform_point(Vec2<Space>{r.right, r.bottom});
    return {std::min({p0.x, p1.x, p2.x, p3.x}), std::min({p0.y, p1.y, p2.y, p3.y}),
            std::max({p0.x, p1.x, p2.x, p3.x}), std::max({p0.y, p1.y, p2.y, p3.y})};
  }

  friend constexpr Affine2 operator*(const Affine2& l, const Affine2& r) noexcept {
    return {l.a * r.a + l.c * r.b, l.b * r.a + l.d * r.b,          l.a * r.c + l.c * r.d,
            l.b * r.c + l.d * r.d, l.a * r.tx + l.c * r.ty + l.tx, l.b * r.tx + l.d * r.ty + l.ty};
  }
  friend constexpr bool operator==(const Affine2&, const Affine2&) = default;
};

// Element-wise comparison with an absolute tolerance.
[[nodiscard]] bool approx_equal(const Affine2& lhs, const Affine2& rhs, float epsilon) noexcept;

// Explicit conversions between logical and physical space.
[[nodiscard]] constexpr PhysicalVec2 to_physical(LogicalVec2 v, ScaleFactor s) noexcept {
  return {v.x * s.value(), v.y * s.value()};
}
[[nodiscard]] constexpr PhysicalSize to_physical(LogicalSize v, ScaleFactor s) noexcept {
  return {v.width * s.value(), v.height * s.value()};
}
[[nodiscard]] constexpr PhysicalRect to_physical(const LogicalRect& r, ScaleFactor s) noexcept {
  return {r.left * s.value(), r.top * s.value(), r.right * s.value(), r.bottom * s.value()};
}
[[nodiscard]] constexpr PhysicalInsets to_physical(const LogicalInsets& i, ScaleFactor s) noexcept {
  return {i.top * s.value(), i.right * s.value(), i.bottom * s.value(), i.left * s.value()};
}
[[nodiscard]] constexpr LogicalVec2 to_logical(PhysicalVec2 v, ScaleFactor s) noexcept {
  return {v.x / s.value(), v.y / s.value()};
}
[[nodiscard]] constexpr LogicalSize to_logical(PhysicalSize v, ScaleFactor s) noexcept {
  return {v.width / s.value(), v.height / s.value()};
}
[[nodiscard]] constexpr LogicalRect to_logical(const PhysicalRect& r, ScaleFactor s) noexcept {
  return {r.left / s.value(), r.top / s.value(), r.right / s.value(), r.bottom / s.value()};
}
[[nodiscard]] constexpr LogicalInsets to_logical(const PhysicalInsets& i, ScaleFactor s) noexcept {
  return {i.top / s.value(), i.right / s.value(), i.bottom / s.value(), i.left / s.value()};
}

}  // namespace ui::foundation
