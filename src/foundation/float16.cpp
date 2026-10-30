#include "foundation/float16.h"

#include <bit>

namespace ui::foundation {

std::uint16_t float_to_half_bits(float value) noexcept {
  const std::uint32_t x = std::bit_cast<std::uint32_t>(value);
  const auto sign = static_cast<std::uint16_t>((x >> 16) & 0x8000u);
  const std::uint32_t abs = x & 0x7FFFFFFFu;

  if (abs >= 0x7F800000u) {  // Infinity or NaN.
    if (abs == 0x7F800000u) {
      return static_cast<std::uint16_t>(sign | 0x7C00u);
    }
    // Quiet NaN; keep the top payload bits.
    return static_cast<std::uint16_t>(sign | 0x7E00u | ((abs >> 13) & 0x3FFu));
  }
  if (abs >= 0x47800000u) {  // >= 65536: beyond rounding range of the largest half.
    return static_cast<std::uint16_t>(sign | 0x7C00u);
  }
  if (abs < 0x38800000u) {    // Below the smallest normal half (2^-14).
    if (abs < 0x33000000u) {  // At most 2^-25 rounds to zero (ties to even).
      return sign;
    }
    const std::uint32_t exponent = abs >> 23;  // Biased, 102..112.
    const std::uint32_t mantissa = (abs & 0x007FFFFFu) | 0x00800000u;
    const std::uint32_t shift = 126u - exponent;  // 14..24.
    std::uint32_t half = mantissa >> shift;
    const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
    const std::uint32_t halfway = 1u << (shift - 1u);
    if (remainder > halfway || (remainder == halfway && (half & 1u) != 0u)) {
      ++half;  // May carry into the smallest normal; the encoding stays correct.
    }
    return static_cast<std::uint16_t>(sign | half);
  }

  // Normal: rebias the exponent from 127 to 15 and drop 13 mantissa bits.
  std::uint32_t half = (abs - 0x38000000u) >> 13;
  const std::uint32_t remainder = abs & 0x1FFFu;
  if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u) != 0u)) {
    ++half;  // A carry out of 0x7BFF correctly produces infinity (0x7C00).
  }
  return static_cast<std::uint16_t>(sign | half);
}

float half_bits_to_float(std::uint16_t bits) noexcept {
  const std::uint32_t sign = (static_cast<std::uint32_t>(bits) & 0x8000u) << 16;
  const std::uint32_t exponent = (static_cast<std::uint32_t>(bits) >> 10) & 0x1Fu;
  const std::uint32_t mantissa = static_cast<std::uint32_t>(bits) & 0x3FFu;

  if (exponent == 0) {
    // Zero or subnormal: value = mantissa * 2^-24, exact in float.
    const float magnitude = static_cast<float>(mantissa) * std::bit_cast<float>(0x33800000u);
    return sign != 0 ? -magnitude : magnitude;
  }
  if (exponent == 0x1Fu) {
    return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
  }
  return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}

}  // namespace ui::foundation
