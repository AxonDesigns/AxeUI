#pragma once

#include <cstdint>

namespace ui::foundation {

// IEEE 754 binary16 conversions (C++20 has no std::float16_t).
//
// float_to_half_bits rounds to nearest, ties to even. Values beyond the half
// range become +/-infinity, tiny values become subnormals or signed zero, and
// NaN stays NaN (quiet, payload truncated). half_bits_to_float is exact.
[[nodiscard]] std::uint16_t float_to_half_bits(float value) noexcept;
[[nodiscard]] float half_bits_to_float(std::uint16_t bits) noexcept;

}  // namespace ui::foundation
