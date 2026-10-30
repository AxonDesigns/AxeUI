#include "render/renderer.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "display_list/instance.h"
#include "frame/frame_driver.h"
#include "platform/core.h"
#include "platform/null_shell.h"
#include "render/render_fixture.h"
#include "render/shader_sources.h"

using namespace ui::foundation;
using namespace ui::display_list;
using namespace ui::render;
using namespace ui::platform;
using ui::test::LogCapture;

namespace {

struct Rgba {
  int r, g, b, a;
  friend bool operator==(Rgba, Rgba) = default;
};

Rgba pixel(const Vector<std::uint8_t>& image, std::uint32_t width, std::uint32_t x,
           std::uint32_t y) {
  const std::uint8_t* p = image.data() + (static_cast<std::size_t>(y) * width + x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

bool near(Rgba a, Rgba b, int tolerance = 1) {
  return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
         std::abs(a.b - b.b) <= tolerance && std::abs(a.a - b.a) <= tolerance;
}

constexpr LinearRgbaPremul kRed{1, 0, 0, 1};
constexpr LinearRgbaPremul kBlack{0, 0, 0, 1};

// --- WGSL struct parsing (no GPU needed) -----------------------------------------

struct WgslField {
  std::string name;
  std::string type;
};

std::vector<WgslField> parse_wgsl_struct(std::string_view source, std::string_view struct_name) {
  std::vector<WgslField> fields;
  const std::string header = "struct " + std::string(struct_name) + " {";
  const std::size_t start = source.find(header);
  if (start == std::string_view::npos) {
    return fields;
  }
  const std::size_t end = source.find('}', start);
  std::string_view body = source.substr(start + header.size(), end - start - header.size());
  while (!body.empty()) {
    const std::size_t line_end = body.find('\n');
    std::string_view line = body.substr(0, line_end);
    body = line_end == std::string_view::npos ? std::string_view{} : body.substr(line_end + 1);
    if (const std::size_t comment = line.find("//"); comment != std::string_view::npos) {
      line = line.substr(0, comment);
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
      continue;
    }
    auto trim = [](std::string_view s) {
      while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0))
        s.remove_prefix(1);
      while (!s.empty() &&
             (std::isspace(static_cast<unsigned char>(s.back())) != 0 || s.back() == ','))
        s.remove_suffix(1);
      return s;
    };
    fields.push_back(
        {std::string(trim(line.substr(0, colon))), std::string(trim(line.substr(colon + 1)))});
  }
  return fields;
}

// WGSL size and alignment (host-shareable layout) of the types the instance
// struct may use.
bool wgsl_layout(std::string_view type, std::size_t& size, std::size_t& align) {
  if (type == "f32" || type == "u32" || type == "i32") {
    size = align = 4;
  } else if (type == "vec2<f32>" || type == "vec2<u32>") {
    size = align = 8;
  } else if (type == "vec4<f32>" || type == "vec4<u32>") {
    size = align = 16;
  } else {
    return false;
  }
  return true;
}

}  // namespace

TEST_CASE("the WGSL Instance struct matches the C++ instance layout") {
  struct Expected {
    const char* name;
    std::size_t offset;
    std::size_t size;
  };
  const Expected expected[] = {
      {"bounds", offsetof(Instance, bounds), sizeof(Instance::bounds)},
      {"uv", offsetof(Instance, uv), sizeof(Instance::uv)},
      {"clip", offsetof(Instance, clip), sizeof(Instance::clip)},
      {"corner_radii", offsetof(Instance, corner_radii), sizeof(Instance::corner_radii)},
      {"fill_color", offsetof(Instance, fill_color), sizeof(Instance::fill_color)},
      {"border_color", offsetof(Instance, border_color), sizeof(Instance::border_color)},
      {"border_width", offsetof(Instance, border_width), sizeof(Instance::border_width)},
      {"kind", offsetof(Instance, kind), sizeof(Instance::kind)},
      {"flags", offsetof(Instance, flags), sizeof(Instance::flags)},
      {"reserved", offsetof(Instance, reserved), sizeof(Instance::reserved)},
  };

  const std::vector<WgslField> fields =
      parse_wgsl_struct(std::string_view(shaders::kUiWgsl, shaders::kUiWgsl_size), "Instance");
  UI_REQUIRE(fields.size() == std::size(expected));

  std::size_t offset = 0;
  std::size_t struct_align = 1;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    CAPTURE(fields[i].name);
    std::size_t size = 0;
    std::size_t align = 0;
    UI_REQUIRE(wgsl_layout(fields[i].type, size, align));
    offset = (offset + align - 1) / align * align;
    struct_align = std::max(struct_align, align);
    CHECK(fields[i].name == expected[i].name);
    CHECK(offset == expected[i].offset);
    CHECK(size == expected[i].size);
    offset += size;
  }
  const std::size_t wgsl_size = (offset + struct_align - 1) / struct_align * struct_align;
  CHECK(wgsl_size == sizeof(Instance));  // Array stride in the storage buffer.
}

UI_TEST_CASE("straight-alpha uploads are premultiplied in linear space") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  TextureManager& textures = renderer->textures();
  const auto srgb = textures.create(1, 1, TextureFormat::Rgba8Srgb);
  const auto linear = textures.create(1, 1, TextureFormat::Rgba8Linear);
  UI_REQUIRE(srgb.has_value());
  UI_REQUIRE(linear.has_value());
  const std::uint8_t half_red[4] = {255, 0, 0, 128};
  CHECK(textures.update(*srgb, 0, 0, 1, 1, half_red, 4, SourceAlpha::Straight).has_value());
  CHECK(textures.update(*linear, 0, 0, 1, 1, half_red, 4, SourceAlpha::Straight).has_value());
  CHECK(textures.pending_updates() == 2);

  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {2, 1}, ScaleFactor{1.0f});
  const LinearRgbaPremul white{1, 1, 1, 1};
  builder.draw_textured_rect(LogicalRect::from_xywh(0, 0, 1, 1), *srgb, {0, 0, 1, 1}, white);
  builder.draw_textured_rect(LogicalRect::from_xywh(1, 0, 1, 1), *linear, {0, 0, 1, 1}, white);
  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->render_offscreen(list, 2, 1, kBlack, &image).has_value());
  CHECK(textures.pending_updates() == 0);
  // Linear red at 50% over black: 0.5 linear -> sRGB 188.
  CHECK(near(pixel(image, 2, 0, 0), {188, 0, 0, 255}));
  // A linear-format texture holding 255*0.5=128 means 0.5 linear too.
  CHECK(near(pixel(image, 2, 1, 0), {188, 0, 0, 255}));
  CHECK(log.records().empty());
}

UI_TEST_CASE("texture updates are validated") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  TextureManager& textures = renderer->textures();
  CHECK(textures.create(0, 4, TextureFormat::Rgba8Srgb).error().code == ErrorCode::InvalidArgument);
  const auto id = textures.create(4, 4, TextureFormat::Rgba8Srgb);
  UI_REQUIRE(id.has_value());
  const std::uint8_t data[64] = {};
  CHECK(textures.update(*id, 2, 2, 4, 4, data, 16, SourceAlpha::Premultiplied).error().code ==
        ErrorCode::InvalidArgument);
  CHECK(textures.update(*id, 0, 0, 4, 4, std::span(data, 10), 16, SourceAlpha::Premultiplied)
            .error()
            .code == ErrorCode::InvalidArgument);
  CHECK(textures.update(*id, 0, 0, 2, 2, data, 4, SourceAlpha::Premultiplied).error().code ==
        ErrorCode::InvalidArgument);  // Stride smaller than a row.
  CHECK(textures.update(*id, 1, 1, 2, 2, data, 8, SourceAlpha::Premultiplied).has_value());
  CHECK(textures.destroy(*id).has_value());
  CHECK(textures.destroy(*id).error().code == ErrorCode::StaleHandle);
  CHECK(textures.update(*id, 0, 0, 1, 1, data, 4, SourceAlpha::Premultiplied).error().code ==
        ErrorCode::StaleHandle);
}

UI_TEST_CASE("instances referencing a destroyed texture draw untextured") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  const auto id = renderer->textures().create(1, 1, TextureFormat::Rgba8Srgb);
  UI_REQUIRE(id.has_value());
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {4, 4}, ScaleFactor{1.0f});
  builder.draw_textured_rect(LogicalRect::from_xywh(0, 0, 4, 4), *id, {0, 0, 1, 1}, kRed);
  CHECK(renderer->textures().destroy(*id).has_value());
  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->render_offscreen(list, 4, 4, kBlack, &image).has_value());
  CHECK(renderer->last_frame_stats().missing_textures == 1);
  CHECK(pixel(image, 4, 1, 1) == Rgba{255, 0, 0, 255});  // White texel x red tint.
}

UI_TEST_CASE("the instance ring grows for large frames and keeps working") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {100, 100}, ScaleFactor{1.0f});
  for (int i = 0; i < 10000; ++i) {
    const float x = static_cast<float>(i % 100);
    const float y = static_cast<float>(i / 100);
    builder.fill_rect(LogicalRect::from_xywh(x, y, 1, 1),
                      LinearRgbaPremul{static_cast<float>(i % 2), 0, 1, 1});
  }
  CHECK(list.instances().size() == 10000);
  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  for (int frame = 0; frame < 4; ++frame) {  // Cycle through every ring slot.
    UI_REQUIRE(renderer->render_offscreen(list, 100, 100, kBlack, &image).has_value());
  }
  CHECK(renderer->last_frame_stats().draw_calls == 1);
  CHECK(renderer->last_frame_stats().instances == 10000);
  CHECK(pixel(image, 100, 0, 0) == Rgba{0, 0, 255, 255});
  CHECK(pixel(image, 100, 1, 0) == Rgba{255, 0, 255, 255});
  CHECK(pixel(image, 100, 99, 99) == Rgba{255, 0, 255, 255});
  CHECK(log.records().empty());
}

UI_TEST_CASE("device loss is recovered with textures restored") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  const auto texture = renderer->textures().create(2, 1, TextureFormat::Rgba8Srgb);
  UI_REQUIRE(texture.has_value());
  const std::uint8_t texels[8] = {0, 255, 0, 255, 0, 0, 255, 255};  // Green, blue.
  CHECK(renderer->textures()
            .update(*texture, 0, 0, 2, 1, texels, 8, SourceAlpha::Straight)
            .has_value());

  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {8, 4}, ScaleFactor{1.0f});
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 4, 4), kRed);
  builder.draw_textured_rect(LogicalRect::from_xywh(4, 0, 4, 4), *texture, {0, 0, 1, 1},
                             LinearRgbaPremul{1, 1, 1, 1});
  Vector<std::uint8_t> before{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->render_offscreen(list, 8, 4, kBlack, &before).has_value());

  const auto lost = renderer->simulate_device_loss();
  if (!lost && lost.error().code == ErrorCode::Unsupported) {
    MESSAGE("skipping: forced device loss is not supported here");
    return;
  }
  UI_REQUIRE(lost.has_value());
  CHECK(renderer->gpu().lost());

  Vector<std::uint8_t> after{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->render_offscreen(list, 8, 4, kBlack, &after).has_value());
  CHECK(renderer->device_recoveries() == 1);
  CHECK_FALSE(renderer->gpu().lost());
  CHECK(after == before);
  CHECK(pixel(after, 8, 4, 1) == Rgba{0, 255, 0, 255});
  CHECK(pixel(after, 8, 7, 1) == Rgba{0, 0, 255, 255});

  bool saw_loss = false;
  for (const auto& record : log.records()) {
    saw_loss = saw_loss || record.code == ErrorCode::DeviceLost;
  }
  CHECK(saw_loss);
}

UI_TEST_CASE("surfaces without a native handle render headlessly; web canvases are rejected") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {4, 4}, ScaleFactor{1.0f});
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 2, 2), kRed);
  const SurfaceId a{Handle::make(HandleType::Surface, 1, 0)};
  const SurfaceInfo info{{4, 4}, ScaleFactor{1.0f}, 60.0f};
  auto outcome = renderer->render_surface(a, NativeSurfaceHandle::none(), info, list, kBlack);
  UI_REQUIRE(outcome.has_value());
  CHECK(*outcome == PresentOutcome::Presented);
  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->read_back(a, image).has_value());
  CHECK(pixel(image, 4, 0, 0) == Rgba{255, 0, 0, 255});
  CHECK(pixel(image, 4, 3, 3) == Rgba{0, 0, 0, 255});

  const SurfaceInfo minimized{{0, 0}, ScaleFactor{1.0f}, 0.0f};
  CHECK(*renderer->render_surface(a, NativeSurfaceHandle::none(), minimized, list, kBlack) ==
        PresentOutcome::SkippedZeroSize);

  const SurfaceId b{Handle::make(HandleType::Surface, 1, 1)};
  CHECK(renderer->render_surface(b, NativeSurfaceHandle::web_canvas("#c"), info, list, kBlack)
            .error()
            .code == ErrorCode::Unsupported);
  renderer->remove_surface(a);
  CHECK(renderer->read_back(a, image).error().code == ErrorCode::InvalidState);
}

namespace {

struct Scene {
  int paints = 0;
};

void paint_scene(void* user_data, SurfaceId, DisplayListBuilder& builder) {
  ++static_cast<Scene*>(user_data)->paints;
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 40, 30), LinearRgbaPremul{1, 1, 1, 1});
  builder.push_clip(LogicalRect::from_xywh(5, 5, 30, 20));
  builder.draw_rounded_rect(LogicalRect::from_xywh(2, 2, 30, 20), {4, 4, 4, 4}, kRed,
                            Border{1.0f, LinearRgbaPremul{0, 0, 1, 1}});
  builder.pop_clip();
}

}  // namespace

UI_TEST_CASE("the frame driver paints and renders each surface; steady state allocates nothing") {
  LogCapture log(LogLevel::Warning);
  auto renderer = ui::test::make_renderer(memory(), log.logger());
  if (!renderer) {
    return;
  }
  NullShell shell(memory());
  Core core(shell, memory(), log.logger());
  Scene scene;
  ui::frame::FrameDriver driver(core, *renderer, memory(), &paint_scene, &scene);
  NullSurface window_a(SurfaceInfo{{40, 30}, ScaleFactor{1.0f}, 60.0f});
  NullSurface window_b(SurfaceInfo{{60, 45}, ScaleFactor{1.5f}, 60.0f});
  const SurfaceId a = *core.add_surface(window_a);
  const SurfaceId b = *core.add_surface(window_b);

  std::int64_t now = 0;
  auto run_frames = [&](int count) {
    for (int i = 0; i < count; ++i) {
      now += 16'666'667;
      CHECK(core.request_redraw(a).has_value());
      CHECK(core.request_redraw(b).has_value());
      core.frame(a, Timestamp{now});
      core.frame(b, Timestamp{now});
    }
  };

  run_frames(5);  // Warm-up: buffers, pipelines, targets.
  const std::size_t allocations = tracker().stats().total_allocations;
  run_frames(30);
  CHECK(tracker().stats().total_allocations == allocations);
  CHECK(scene.paints == 70);
  CHECK(driver.frames_presented() == 70);
  CHECK(driver.frames_skipped() == 0);

  Vector<std::uint8_t> image{Allocator<std::uint8_t>(memory())};
  UI_REQUIRE(renderer->read_back(b, image).has_value());
  CHECK(image.size() == 60u * 45u * 4u);
  CHECK(pixel(image, 60, 0, 0) == Rgba{255, 255, 255, 255});

  CHECK(core.remove_surface(a).has_value());
  CHECK(renderer->read_back(a, image).error().code == ErrorCode::InvalidState);
  CHECK(core.remove_surface(b).has_value());
  CHECK(log.records().empty());
}
