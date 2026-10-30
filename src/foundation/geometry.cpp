#include "foundation/geometry.h"

#include <cmath>

namespace ui::foundation {

Affine2 Affine2::rotation(float radians) noexcept {
  const float s = std::sin(radians);
  const float co = std::cos(radians);
  return {co, s, -s, co, 0.0f, 0.0f};
}

Result<Affine2> Affine2::inverse() const noexcept {
  const float det = determinant();
  if (det == 0.0f || !std::isfinite(det)) {
    return fail(Error{ErrorCode::InvalidArgument, "transform is not invertible"});
  }
  const float inv = 1.0f / det;
  const Affine2 r{
      d * inv, -b * inv, -c * inv, a * inv, (c * ty - d * tx) * inv, (b * tx - a * ty) * inv};
  if (!std::isfinite(r.a) || !std::isfinite(r.b) || !std::isfinite(r.c) || !std::isfinite(r.d) ||
      !std::isfinite(r.tx) || !std::isfinite(r.ty)) {
    return fail(Error{ErrorCode::InvalidArgument, "transform inverse is not finite"});
  }
  return r;
}

bool approx_equal(const Affine2& lhs, const Affine2& rhs, float epsilon) noexcept {
  return std::fabs(lhs.a - rhs.a) <= epsilon && std::fabs(lhs.b - rhs.b) <= epsilon &&
         std::fabs(lhs.c - rhs.c) <= epsilon && std::fabs(lhs.d - rhs.d) <= epsilon &&
         std::fabs(lhs.tx - rhs.tx) <= epsilon && std::fabs(lhs.ty - rhs.ty) <= epsilon;
}

}  // namespace ui::foundation
