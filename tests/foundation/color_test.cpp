#include "foundation/color.h"

#include <cstdint>
#include <limits>

#include "support/test_support.h"

using namespace ui::foundation;

TEST_CASE("every 8-bit sRGB value round-trips through linear float") {
  int mismatches = 0;
  for (int v = 0; v < 256; ++v) {
    const auto byte = static_cast<std::uint8_t>(v);
    const SrgbRgba8Straight original{byte, static_cast<std::uint8_t>(255 - v), byte, byte};
    if (to_srgb8(to_linear(original)) != original) {
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);
}

TEST_CASE("sRGB transfer function reference values") {
  CHECK(srgb_to_linear(0.0f) == 0.0f);
  CHECK(srgb_to_linear(1.0f) == doctest::Approx(1.0f));
  CHECK(srgb_to_linear(0.5f) == doctest::Approx(0.214041f).epsilon(1e-5));
  CHECK(linear_to_srgb(0.214041f) == doctest::Approx(0.5f).epsilon(1e-5));
  // Linear segment near black.
  CHECK(srgb_to_linear(0.04f) == doctest::Approx(0.04f / 12.92f));

  const LinearRgbaStraight mid = to_linear(SrgbRgba8Straight{128, 128, 128, 255});
  CHECK(mid.r == doctest::Approx(0.215861f).epsilon(1e-5));
  CHECK(mid.a == 1.0f);
}

TEST_CASE("sRGB encoding clamps out-of-range and NaN values") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const SrgbRgba8Straight c = to_srgb8(LinearRgbaStraight{-1.0f, 2.0f, nan, 1.5f});
  CHECK(c == SrgbRgba8Straight{0, 255, 0, 255});
}

TEST_CASE("premultiplication") {
  const LinearRgbaStraight straight{0.8f, 0.4f, 0.2f, 0.5f};
  const LinearRgbaPremul premul = premultiply(straight);
  CHECK(premul.r == doctest::Approx(0.4f));
  CHECK(premul.g == doctest::Approx(0.2f));
  CHECK(premul.b == doctest::Approx(0.1f));
  CHECK(premul.a == 0.5f);

  const LinearRgbaStraight back = unpremultiply(premul);
  CHECK(back.r == doctest::Approx(straight.r));
  CHECK(back.g == doctest::Approx(straight.g));
  CHECK(back.b == doctest::Approx(straight.b));
  CHECK(back.a == straight.a);

  // Opaque colors are unchanged by premultiplication.
  const LinearRgbaStraight opaque{0.3f, 0.6f, 0.9f, 1.0f};
  const LinearRgbaPremul opaque_premul = premultiply(opaque);
  CHECK(opaque_premul == LinearRgbaPremul{0.3f, 0.6f, 0.9f, 1.0f});

  // Zero alpha: color is unrecoverable, unpremultiply yields transparent black.
  CHECK(premultiply(LinearRgbaStraight{1, 1, 1, 0}) == LinearRgbaPremul::transparent());
  CHECK(unpremultiply(LinearRgbaPremul{}) == LinearRgbaStraight{0, 0, 0, 0});
}
