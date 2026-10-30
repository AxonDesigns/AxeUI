#pragma once

// Golden-image comparison for renderer tests.
//
// Goldens live in tests/goldens/<name>.png and are generated with the
// reference configuration (Linux, Mesa lavapipe). A comparison passes when
// at most `max_differing_pixels` pixels differ by more than `per_channel` in
// any channel. On failure the actual image and a diff image are written to
// the golden output directory (uploaded as CI artifacts).
//
// Goldens are only ever written when UI_UPDATE_GOLDENS=1 is set, through
// tools/update_goldens.sh; CI never sets it.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ui::test {

struct GoldenTolerance {
  int per_channel = 2;
  std::size_t max_differing_pixels = 4;
};

struct GoldenResult {
  bool passed = false;
  std::size_t differing_pixels = 0;
  int max_channel_difference = 0;
  std::string message;
};

[[nodiscard]] bool golden_update_requested();

// `rgba` holds width * height tightly packed RGBA8 pixels.
[[nodiscard]] GoldenResult compare_with_golden(std::string_view name,
                                               std::span<const std::uint8_t> rgba,
                                               std::uint32_t width, std::uint32_t height,
                                               const GoldenTolerance& tolerance = {});

}  // namespace ui::test
