#include "foundation/color.h"

#include <cmath>

namespace ui::foundation {

namespace {

// Clamps to [0, 1] (NaN maps to 0) and rounds to the nearest 8-bit value.
std::uint8_t quantize_unorm8(float value) noexcept {
  if (!(value > 0.0f)) {
    return 0;
  }
  if (value >= 1.0f) {
    return 255;
  }
  return static_cast<std::uint8_t>(std::lround(value * 255.0f));
}

}  // namespace

float srgb_to_linear(float encoded) noexcept {
  if (encoded <= 0.04045f) {
    return encoded / 12.92f;
  }
  return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float linear) noexcept {
  if (linear <= 0.0031308f) {
    return linear * 12.92f;
  }
  return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

LinearRgbaStraight to_linear(SrgbRgba8Straight color) noexcept {
  constexpr float kInv255 = 1.0f / 255.0f;
  return {srgb_to_linear(static_cast<float>(color.r) * kInv255),
          srgb_to_linear(static_cast<float>(color.g) * kInv255),
          srgb_to_linear(static_cast<float>(color.b) * kInv255),
          static_cast<float>(color.a) * kInv255};
}

SrgbRgba8Straight to_srgb8(LinearRgbaStraight color) noexcept {
  return {quantize_unorm8(linear_to_srgb(color.r)), quantize_unorm8(linear_to_srgb(color.g)),
          quantize_unorm8(linear_to_srgb(color.b)), quantize_unorm8(color.a)};
}

}  // namespace ui::foundation
