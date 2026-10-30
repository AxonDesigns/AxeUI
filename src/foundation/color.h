#pragma once

#include <cstdint>

namespace ui::foundation {

// Color types. The name of each type states its color space, precision and
// alpha convention, and every conversion between them is an explicit
// function call:
//
//   SrgbRgba8Straight --to_linear--> LinearRgbaStraight --premultiply--> LinearRgbaPremul
//                    <--to_srgb8---                     <-unpremultiply-
//
// The rendering pipeline works exclusively in LinearRgbaPremul. sRGB 8-bit
// values exist only at the edges (authoring, image decoding, readback).

// 8-bit sRGB-encoded color with straight (non-premultiplied) alpha, as found
// in CSS hex colors and most image files.
struct SrgbRgba8Straight {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  friend constexpr bool operator==(SrgbRgba8Straight, SrgbRgba8Straight) = default;
};

// Linear-light float color with straight alpha. Components are nominally
// in [0, 1]; values outside that range are allowed (HDR, wide gamut).
struct LinearRgbaStraight {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;

  friend constexpr bool operator==(LinearRgbaStraight, LinearRgbaStraight) = default;
};

// Linear-light float color with premultiplied alpha (r, g, b already
// multiplied by a). The working color type of the pipeline.
struct LinearRgbaPremul {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 0.0f;

  [[nodiscard]] static constexpr LinearRgbaPremul transparent() noexcept { return {}; }

  friend constexpr bool operator==(LinearRgbaPremul, LinearRgbaPremul) = default;
};

// The exact piecewise sRGB transfer functions (IEC 61966-2-1).
[[nodiscard]] float srgb_to_linear(float encoded) noexcept;
[[nodiscard]] float linear_to_srgb(float linear) noexcept;

// Decodes sRGB to linear light. Alpha is linear already and only rescaled.
[[nodiscard]] LinearRgbaStraight to_linear(SrgbRgba8Straight color) noexcept;

// Encodes to 8-bit sRGB, clamping each component to [0, 1] and rounding to
// nearest. Round-trips every SrgbRgba8Straight value exactly.
[[nodiscard]] SrgbRgba8Straight to_srgb8(LinearRgbaStraight color) noexcept;

[[nodiscard]] constexpr LinearRgbaPremul premultiply(LinearRgbaStraight c) noexcept {
  return {c.r * c.a, c.g * c.a, c.b * c.a, c.a};
}

// Fully transparent input yields transparent black, since the color of a
// zero-alpha premultiplied value is unrecoverable.
[[nodiscard]] constexpr LinearRgbaStraight unpremultiply(LinearRgbaPremul c) noexcept {
  if (c.a == 0.0f) {
    return {0.0f, 0.0f, 0.0f, 0.0f};
  }
  return {c.r / c.a, c.g / c.a, c.b / c.a, c.a};
}

}  // namespace ui::foundation
