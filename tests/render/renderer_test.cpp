#include "render/renderer.h"

#include <cstdint>

#include "render/render_fixture.h"

using namespace ui::foundation;
using namespace ui::display_list;
using namespace ui::render;

namespace {

struct Rgba {
  std::uint8_t r, g, b, a;
  friend bool operator==(Rgba, Rgba) = default;
};

Rgba pixel(const Vector<std::uint8_t>& image, std::uint32_t width, std::uint32_t x,
           std::uint32_t y) {
  const std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

}  // namespace

UI_TEST_CASE("offscreen rendering draws a display list and reads it back") {
  ui::test::LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {64, 32}, ScaleFactor{1.0f});
  builder.fill_rect(LogicalRect::from_xywh(8, 8, 16, 16), LinearRgbaPremul{1, 0, 0, 1});

  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  const auto rendered =
      renderer->render_offscreen(list, 64, 32, LinearRgbaPremul{0, 0, 1, 1}, &image);
  UI_REQUIRE(rendered.has_value());
  CHECK(image.size() == 64u * 32u * 4u);
  CHECK(pixel(image, 64, 0, 0) == Rgba{0, 0, 255, 255});
  CHECK(pixel(image, 64, 8, 8) == Rgba{255, 0, 0, 255});
  CHECK(pixel(image, 64, 23, 23) == Rgba{255, 0, 0, 255});
  CHECK(pixel(image, 64, 24, 24) == Rgba{0, 0, 255, 255});
  CHECK(renderer->last_frame_stats().draw_calls == 1);
  for (const auto& record : log.records()) {
    MESSAGE(record.message);
  }
  CHECK(log.records().empty());
}
