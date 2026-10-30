// Golden-image tests: scenes rendered headlessly through the real renderer
// and compared with tests/goldens/*.png. They run only on the reference
// configuration (Mesa lavapipe); elsewhere they are skipped with a message.
// Update goldens with tools/update_goldens.sh.

#include "render/golden.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include "display_list/builder.h"
#include "foundation/color.h"
#include "frame/frame_driver.h"
#include "platform/core.h"
#include "platform/null_shell.h"
#include "render/render_fixture.h"

using namespace ui::foundation;
using namespace ui::display_list;
using namespace ui::render;
using namespace ui::platform;
using ui::test::LogCapture;

namespace {

LinearRgbaPremul srgb(std::uint8_t r, std::uint8_t g, std::uint8_t b, float alpha = 1.0f) {
  LinearRgbaStraight c = to_linear(SrgbRgba8Straight{r, g, b, 255});
  c.a = alpha;
  return premultiply(c);
}

const LinearRgbaPremul kWhite = srgb(255, 255, 255);
const LinearRgbaPremul kInk = srgb(32, 36, 48);
const LinearRgbaPremul kRed = srgb(220, 50, 47);
const LinearRgbaPremul kGreen = srgb(64, 160, 43);
const LinearRgbaPremul kBlue = srgb(38, 139, 210);
const LinearRgbaPremul kYellow = srgb(250, 200, 30);

// Renders `paint` at `scale` into a logical canvas and compares it with the
// golden `name`. Returns false (after recording a failure) on mismatch.
template <class Paint>
void check_scene(MemoryResource& memory, Renderer& renderer, const std::string& name,
                 LogicalSize logical, float scale, LinearRgbaPremul clear, Paint paint) {
  CAPTURE(name);
  const ScaleFactor factor{scale};
  const PhysicalSize physical = to_physical(logical, factor);
  const auto width = static_cast<std::uint32_t>(std::lround(physical.width));
  const auto height = static_cast<std::uint32_t>(std::lround(physical.height));
  DisplayList list(memory);
  DisplayListBuilder builder(list, memory, {static_cast<float>(width), static_cast<float>(height)},
                             factor);
  paint(builder);
  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory)};
  const auto rendered = renderer.render_offscreen(list, width, height, clear, &image);
  CHECK(rendered.has_value());
  if (!rendered) {
    return;
  }
  const ui::test::GoldenResult result = ui::test::compare_with_golden(name, image, width, height);
  CHECK_MESSAGE(result.passed, result.message);
  if (ui::test::golden_update_requested()) {
    MESSAGE(result.message);
  }
}

// Returns a renderer on the reference adapter, or nullptr (skip).
UniquePtr<Renderer> golden_renderer(MemoryResource& memory, const Logger& logger) {
  auto renderer = ui::test::make_renderer(memory, logger);
  if (renderer && !ui::test::is_reference_adapter(*renderer)) {
    MESSAGE("skipping golden comparison: adapter '" << renderer->gpu().adapter_name()
                                                    << "' is not the reference (lavapipe)");
    return UniquePtr<Renderer>(nullptr, ObjectDeleter<Renderer>(memory));
  }
  return renderer;
}

std::string scale_suffix(float scale) {
  return "_scale_" + std::to_string(static_cast<int>(std::lround(scale * 100.0f)));
}

// A procedural RGBA8 image with straight alpha: a 2x2 checker of opaque
// colors over a gradient whose alpha fades left to right.
void make_test_image(std::uint8_t* pixels, std::uint32_t size) {
  for (std::uint32_t y = 0; y < size; ++y) {
    for (std::uint32_t x = 0; x < size; ++x) {
      std::uint8_t* p = pixels + (std::size_t{y} * size + x) * 4;
      const bool checker = ((x / (size / 4)) + (y / (size / 4))) % 2 == 0;
      if (y < size / 2) {
        p[0] = checker ? 250 : 40;
        p[1] = checker ? 200 : 40;
        p[2] = checker ? 30 : 160;
        p[3] = 255;
      } else {
        p[0] = static_cast<std::uint8_t>(x * 255 / (size - 1));
        p[1] = 80;
        p[2] = static_cast<std::uint8_t>(255 - x * 255 / (size - 1));
        p[3] = static_cast<std::uint8_t>(64 + x * 191 / (size - 1));
      }
    }
  }
}

void paint_rects(DisplayListBuilder& b) {
  // Solid rects at fractional positions and sizes.
  b.fill_rect(LogicalRect::from_xywh(4.3f, 4.6f, 30.2f, 20.4f), kRed);
  b.fill_rect(LogicalRect::from_xywh(36.5f, 4.5f, 0.6f, 20.0f), kInk);  // Hairline.
  b.fill_rect(LogicalRect::from_xywh(40.25f, 4.75f, 10.5f, 10.5f), kGreen);
  b.fill_rect(LogicalRect::from_xywh(50.75f, 4.75f, 10.5f, 10.5f), kBlue);  // Shares an edge.
  // Rounded rects at fractional positions.
  b.draw_rounded_rect(LogicalRect::from_xywh(4.4f, 30.3f, 40.0f, 24.0f), CornerRadii::uniform(6.0f),
                      kBlue);
  b.draw_rounded_rect(LogicalRect::from_xywh(48.7f, 30.6f, 24.0f, 24.0f),
                      CornerRadii::uniform(12.0f), kYellow);
  b.draw_rounded_rect(LogicalRect::from_xywh(76.1f, 30.1f, 40.0f, 16.5f),
                      CornerRadii::uniform(3.5f), kGreen);
}

}  // namespace

UI_TEST_CASE("golden: solid and rounded rects at fractional positions and scale factors") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
    check_scene(memory(), *renderer, "rects" + scale_suffix(scale), {120, 60}, scale, kWhite,
                paint_rects);
  }
  ui::test::check_no_errors(log);
}

UI_TEST_CASE("golden: four different corner radii") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  for (const float scale : {1.0f, 2.0f}) {
    check_scene(memory(), *renderer, "corner_radii" + scale_suffix(scale), {120, 70}, scale, kWhite,
                [](DisplayListBuilder& b) {
                  b.draw_rounded_rect(LogicalRect::from_xywh(6, 6, 50, 56), {0, 8, 16, 24}, kBlue);
                  b.draw_rounded_rect(LogicalRect::from_xywh(64, 6, 50, 56), {24, 2, 12, 6}, kRed);
                });
  }
}

UI_TEST_CASE("golden: borders") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  for (const float scale : {1.0f, 1.5f}) {
    check_scene(memory(), *renderer, "borders" + scale_suffix(scale), {140, 60}, scale, kWhite,
                [](DisplayListBuilder& b) {
                  b.draw_rounded_rect(LogicalRect::from_xywh(5, 5, 40, 50), CornerRadii{}, kYellow,
                                      Border{1.0f, kInk});
                  b.draw_rounded_rect(LogicalRect::from_xywh(50, 5, 40, 50),
                                      CornerRadii::uniform(10), kWhite, Border{2.5f, kBlue});
                  b.draw_rounded_rect(LogicalRect::from_xywh(95, 5, 40, 50), {0, 12, 4, 20},
                                      srgb(220, 50, 47, 0.25f),
                                      Border{4.0f, srgb(32, 36, 48, 0.6f)});
                });
  }
}

UI_TEST_CASE("golden: nested clips") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  check_scene(memory(), *renderer, "nested_clips", {120, 80}, 1.25f, kWhite,
              [](DisplayListBuilder& b) {
                b.push_clip(LogicalRect::from_xywh(10, 10, 100, 60));
                b.fill_rect(LogicalRect::from_xywh(0, 0, 120, 80), srgb(230, 230, 235));
                b.push_clip(LogicalRect::from_xywh(30.5f, 20.5f, 60, 40));
                b.draw_rounded_rect(LogicalRect::from_xywh(20, 15, 80, 50),
                                    CornerRadii::uniform(18), kBlue, Border{3, kInk});
                CHECK(b.push_transform(Affine2::translation(40, 30)).has_value());
                b.push_clip(LogicalRect::from_xywh(0, 0, 25, 25));
                b.draw_rounded_rect(LogicalRect::from_xywh(-10, -10, 60, 60),
                                    CornerRadii::uniform(30), kYellow);
                b.pop_clip();
                b.pop_transform();
                b.pop_clip();
                b.fill_rect(LogicalRect::from_xywh(95, 50, 40, 40), kRed);  // Outer clip only.
                b.pop_clip();
              });
}

UI_TEST_CASE("golden: textured rects") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  constexpr std::uint32_t kSize = 16;
  std::uint8_t pixels[kSize * kSize * 4];
  make_test_image(pixels, kSize);
  const auto srgb_texture = renderer->textures().create(kSize, kSize, TextureFormat::Rgba8Srgb);
  const auto linear_texture = renderer->textures().create(kSize, kSize, TextureFormat::Rgba8Linear);
  UI_REQUIRE(srgb_texture.has_value());
  UI_REQUIRE(linear_texture.has_value());
  CHECK(renderer->textures()
            .update(*srgb_texture, 0, 0, kSize, kSize, pixels, kSize * 4, SourceAlpha::Straight)
            .has_value());
  CHECK(renderer->textures()
            .update(*linear_texture, 0, 0, kSize, kSize, pixels, kSize * 4, SourceAlpha::Straight)
            .has_value());

  for (const float scale : {1.0f, 1.5f}) {
    check_scene(memory(), *renderer, "textured_rects" + scale_suffix(scale), {150, 60}, scale,
                srgb(200, 200, 200), [&](DisplayListBuilder& b) {
                  b.draw_textured_rect(LogicalRect::from_xywh(5, 5, 50, 50), *srgb_texture,
                                       {0, 0, 1, 1}, kWhite);
                  b.draw_textured_rect(LogicalRect::from_xywh(60, 5, 50, 50), *linear_texture,
                                       {0, 0, 1, 1}, kWhite);
                  // Sub-rectangle of the texture, tinted at 50%.
                  b.draw_textured_rect(LogicalRect::from_xywh(115, 5, 30, 50), *srgb_texture,
                                       {0, 0.5f, 1, 1}, srgb(255, 255, 255, 0.5f));
                });
  }
}

UI_TEST_CASE("golden: premultiplied alpha blending over different backgrounds") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  check_scene(
      memory(), *renderer, "alpha_blending", {160, 80}, 1.0f, kWhite, [](DisplayListBuilder& b) {
        const LinearRgbaPremul backgrounds[] = {kWhite, srgb(0, 0, 0), srgb(128, 128, 128), kBlue};
        for (int i = 0; i < 4; ++i) {
          b.fill_rect(LogicalRect::from_xywh(static_cast<float>(i * 40), 0, 40, 80),
                      backgrounds[i]);
        }
        const float alphas[] = {0.25f, 0.5f, 0.75f};
        for (int row = 0; row < 3; ++row) {
          b.draw_rounded_rect(LogicalRect::from_xywh(4, 6 + static_cast<float>(row) * 24, 152, 18),
                              CornerRadii::uniform(9), srgb(220, 50, 47, alphas[row]));
        }
      });
}

UI_TEST_CASE("golden: texture switches produce separate batches") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  const std::uint8_t red[4] = {255, 0, 0, 255};
  const std::uint8_t green[4] = {0, 255, 0, 255};
  const auto a = renderer->textures().create(1, 1, TextureFormat::Rgba8Srgb);
  const auto b = renderer->textures().create(1, 1, TextureFormat::Rgba8Srgb);
  UI_REQUIRE(a.has_value());
  UI_REQUIRE(b.has_value());
  CHECK(renderer->textures().update(*a, 0, 0, 1, 1, red, 4, SourceAlpha::Straight).has_value());
  CHECK(renderer->textures().update(*b, 0, 0, 1, 1, green, 4, SourceAlpha::Straight).has_value());

  auto paint = [&](DisplayListBuilder& builder) {
    builder.fill_rect(LogicalRect::from_xywh(0, 0, 100, 30), kInk);  // Batch 1
    builder.draw_textured_rect(LogicalRect::from_xywh(5, 5, 20, 20), *a, {0, 0, 1, 1}, kWhite);
    builder.fill_rect(LogicalRect::from_xywh(27, 5, 4, 20), kYellow);  // Joins 1
    builder.draw_textured_rect(LogicalRect::from_xywh(33, 5, 20, 20), *b, {0, 0, 1, 1},
                               kWhite);  // 2
    builder.draw_textured_rect(LogicalRect::from_xywh(55, 5, 20, 20), *a, {0, 0, 1, 1},
                               kWhite);  // 3
    builder.draw_rounded_rect(LogicalRect::from_xywh(77, 5, 20, 20), CornerRadii::uniform(10),
                              kBlue);  // Joins 3
  };
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {100, 30}, ScaleFactor{1.0f});
  paint(builder);
  CHECK(list.commands().size() == 3);

  check_scene(memory(), *renderer, "texture_switches", {100, 30}, 1.0f, kWhite, paint);
  CHECK(renderer->last_frame_stats().draw_calls == 3);
  CHECK(renderer->last_frame_stats().bind_group_changes == 3);
}

namespace {

void paint_shared_scene(void*, SurfaceId, DisplayListBuilder& b) {
  b.fill_rect(LogicalRect::from_xywh(0, 0, 80, 50), kWhite);
  b.draw_rounded_rect(LogicalRect::from_xywh(6.5f, 6.5f, 40, 30), CornerRadii::uniform(8), kBlue,
                      Border{1.5f, kInk});
  b.fill_rect(LogicalRect::from_xywh(50.3f, 10.6f, 22, 22), kRed);
}

}  // namespace

UI_TEST_CASE("golden: the same scene on two surfaces with different scale factors") {
  LogCapture log(LogLevel::Warning);
  auto renderer = golden_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  NullShell shell(memory());
  Core core(shell, memory(), log.logger());
  ui::frame::FrameDriver driver(core, *renderer, memory(), &paint_shared_scene, nullptr);
  driver.set_clear_color(kWhite);
  NullSurface standard(SurfaceInfo{{80, 50}, ScaleFactor{1.0f}, 60.0f});
  NullSurface hidpi(SurfaceInfo{{120, 75}, ScaleFactor{1.5f}, 60.0f});
  const SurfaceId a = *core.add_surface(standard);
  const SurfaceId b = *core.add_surface(hidpi);
  core.frame(a, Timestamp{1});
  core.frame(b, Timestamp{1});

  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->read_back(a, image).has_value());
  auto result = ui::test::compare_with_golden("two_surfaces_scale_100", image, 80, 50);
  CHECK_MESSAGE(result.passed, result.message);
  UI_REQUIRE(renderer->read_back(b, image).has_value());
  result = ui::test::compare_with_golden("two_surfaces_scale_150", image, 120, 75);
  CHECK_MESSAGE(result.passed, result.message);

  CHECK(core.remove_surface(a).has_value());
  CHECK(core.remove_surface(b).has_value());
}
