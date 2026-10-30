#include "foundation/float16.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "support/test_support.h"

using namespace ui::foundation;

TEST_CASE("every half value converts to float and back exactly") {
  int mismatches = 0;
  for (std::uint32_t bits = 0; bits <= 0xFFFFu; ++bits) {
    const auto half = static_cast<std::uint16_t>(bits);
    const float f = half_bits_to_float(half);
    const bool is_nan = (half & 0x7C00u) == 0x7C00u && (half & 0x03FFu) != 0;
    if (is_nan) {
      if (!std::isnan(f) || (float_to_half_bits(f) & 0x7FFFu) <= 0x7C00u) {
        ++mismatches;
      }
    } else if (float_to_half_bits(f) != half) {
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
}

TEST_CASE("float to half reference values and rounding") {
  CHECK(float_to_half_bits(0.0f) == 0x0000);
  CHECK(float_to_half_bits(-0.0f) == 0x8000);
  CHECK(float_to_half_bits(1.0f) == 0x3C00);
  CHECK(float_to_half_bits(-2.0f) == 0xC000);
  CHECK(float_to_half_bits(0.5f) == 0x3800);
  CHECK(float_to_half_bits(65504.0f) == 0x7BFF);  // Largest finite half.

  // Ties round to even.
  CHECK(float_to_half_bits(1.0f + std::ldexp(1.0f, -11)) == 0x3C00);
  CHECK(float_to_half_bits(1.0f + 3.0f * std::ldexp(1.0f, -11)) == 0x3C02);
  CHECK(float_to_half_bits(65520.0f) == 0x7C00);  // Halfway past max: rounds to infinity.
  CHECK(float_to_half_bits(65519.0f) == 0x7BFF);

  // Overflow, infinity, NaN.
  CHECK(float_to_half_bits(1e10f) == 0x7C00);
  CHECK(float_to_half_bits(-std::numeric_limits<float>::infinity()) == 0xFC00);
  const std::uint16_t nan = float_to_half_bits(std::numeric_limits<float>::quiet_NaN());
  CHECK((nan & 0x7C00) == 0x7C00);
  CHECK((nan & 0x03FF) != 0);

  // Subnormals.
  CHECK(float_to_half_bits(std::ldexp(1.0f, -24)) == 0x0001);  // Smallest subnormal.
  CHECK(float_to_half_bits(std::ldexp(1.0f, -25)) == 0x0000);  // Tie to even: zero.
  CHECK(float_to_half_bits(std::ldexp(1.5f, -25)) == 0x0001);
  CHECK(float_to_half_bits(std::ldexp(1.0f, -14)) == 0x0400);     // Smallest normal.
  CHECK(float_to_half_bits(std::ldexp(1023.5f, -24)) == 0x0400);  // Rounds up into normal.
  CHECK(float_to_half_bits(1e-10f) == 0x0000);
}
