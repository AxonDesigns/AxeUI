// hello_surfaces: two windows drawn by the WebGPU renderer through the SDL3
// shell.
//
// - The main window draws bordered rounded rects, a nested clip and a
//   textured rect; the "button" rect changes color on hover and on click
//   (hit testing is plain rect containment for now).
// - The second window draws a different scene and can be closed on its own.
// - Closing the main window quits.
//
// Both windows stay correct while resizing and when moved to a monitor with
// a different scale factor: sizes and scales arrive as Resize /
// ScaleFactorChanged events and every frame is painted in logical pixels.

#include <cstdint>
#include <cstdio>

#include "display_list/builder.h"
#include "foundation/color.h"
#include "foundation/tracking_resource.h"
#include "frame/frame_driver.h"
#include "platform/core.h"
#include "render/renderer.h"
#include "sdl3/sdl_shell.h"

using namespace ui;
using foundation::LinearRgbaPremul;
using foundation::LogicalRect;

namespace {

LinearRgbaPremul color(std::uint8_t r, std::uint8_t g, std::uint8_t b, float alpha = 1.0f) {
  foundation::LinearRgbaStraight c =
      foundation::to_linear(foundation::SrgbRgba8Straight{r, g, b, 255});
  c.a = alpha;
  return foundation::premultiply(c);
}

struct App {
  platform::Core* core = nullptr;
  platform::SurfaceId main_window;
  platform::SurfaceId second_window;
  display_list::TextureId texture;
  bool hovered = false;
  bool pressed = false;
};

const LogicalRect kButton = LogicalRect::from_xywh(40, 300, 220, 64);

void paint_main(const App& app, display_list::DisplayListBuilder& b, foundation::LogicalSize size) {
  b.fill_rect(LogicalRect::from_xywh(0, 0, size.width, size.height), color(246, 247, 250));

  // Cards with borders and different corner radii.
  b.draw_rounded_rect(LogicalRect::from_xywh(40, 40, 220, 120),
                      display_list::CornerRadii::uniform(14), color(255, 255, 255),
                      {1.0f, color(200, 204, 214)});
  b.draw_rounded_rect(LogicalRect::from_xywh(290, 40, 220, 120), {4, 24, 4, 24},
                      color(38, 139, 210), {3.0f, color(20, 70, 110)});

  // Nested clips: an outer panel clips a scrolled list of stripes, an inner
  // clip shows a highlighted window into it.
  b.push_clip(LogicalRect::from_xywh(540, 40, 220, 220));
  b.fill_rect(LogicalRect::from_xywh(540, 40, 220, 220), color(32, 36, 48));
  for (int i = 0; i < 12; ++i) {
    b.fill_rect(LogicalRect::from_xywh(520, 30 + static_cast<float>(i) * 22, 260, 12),
                color(64, 160, 43, 0.6f));
  }
  b.push_clip(LogicalRect::from_xywh(590, 90, 120, 120));
  b.draw_rounded_rect(LogicalRect::from_xywh(560, 60, 180, 180),
                      display_list::CornerRadii::uniform(90), color(250, 200, 30, 0.85f));
  b.pop_clip();
  b.pop_clip();

  // Textured rect.
  b.draw_textured_rect(LogicalRect::from_xywh(290, 190, 220, 220), app.texture, {0, 0, 1, 1},
                       color(255, 255, 255));

  // The interactive "button".
  const LinearRgbaPremul fill = app.pressed   ? color(180, 40, 40)
                                : app.hovered ? color(240, 90, 80)
                                              : color(220, 50, 47);
  b.draw_rounded_rect(kButton, display_list::CornerRadii::uniform(12), fill,
                      {app.hovered ? 3.0f : 1.0f, color(120, 20, 20)});
}

void paint_second(display_list::DisplayListBuilder& b, foundation::LogicalSize size) {
  b.fill_rect(LogicalRect::from_xywh(0, 0, size.width, size.height), color(32, 36, 48));
  for (int row = 0; row < 6; ++row) {
    for (int col = 0; col < 8; ++col) {
      const float x = 20 + static_cast<float>(col) * 48;
      const float y = 20 + static_cast<float>(row) * 48;
      b.draw_rounded_rect(LogicalRect::from_xywh(x, y, 36, 36),
                          display_list::CornerRadii::uniform(18),
                          color(static_cast<std::uint8_t>(60 + col * 24), 120,
                                static_cast<std::uint8_t>(240 - row * 30)));
    }
  }
}

void paint(void* user_data, platform::SurfaceId id, display_list::DisplayListBuilder& b) {
  const App& app = *static_cast<const App*>(user_data);
  const auto state = app.core->surface(id);
  if (!state) {
    return;
  }
  const foundation::LogicalSize size = (*state)->info.logical_size();
  if (id == app.main_window) {
    paint_main(app, b, size);
  } else {
    paint_second(b, size);
  }
}

// A 64x64 straight-alpha image: checker on top, alpha gradient below.
void make_texture(std::uint8_t* pixels) {
  for (std::uint32_t y = 0; y < 64; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      std::uint8_t* p = pixels + (std::size_t{y} * 64 + x) * 4;
      const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
      p[0] = checker ? 250 : 38;
      p[1] = checker ? 200 : 139;
      p[2] = checker ? 30 : 210;
      p[3] = static_cast<std::uint8_t>(y < 32 ? 255 : 255 - (y - 32) * 6);
    }
  }
}

void log_to_stderr(void*, const foundation::LogRecord& record) {
  static const char* const kLevels[] = {"trace", "debug", "info", "warning", "error"};
  static_cast<void>(std::fprintf(stderr, "[%s] %.*s\n", kLevels[static_cast<int>(record.level)],
                                 static_cast<int>(record.message.size()), record.message.data()));
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  foundation::HeapResource heap;
  foundation::TrackingResource memory(heap);
  const foundation::Logger logger(&log_to_stderr, nullptr, foundation::LogLevel::Info);

  auto shell = sdl3::SdlShell::create(memory, logger);
  if (!shell) {
    static_cast<void>(
        std::fprintf(stderr, "cannot start the SDL shell: %s\n", shell.error().message));
    return 1;
  }
  platform::Core core(**shell, memory, logger);
  auto renderer = render::Renderer::create(memory, logger);
  if (!renderer) {
    static_cast<void>(
        std::fprintf(stderr, "cannot start the renderer: %s\n", renderer.error().message));
    return 1;
  }

  App app;
  app.core = &core;
  frame::FrameDriver driver(core, **renderer, memory, &paint, &app);
  driver.set_clear_color(color(246, 247, 250));

  std::uint8_t pixels[64 * 64 * 4];
  make_texture(pixels);
  app.texture = *(*renderer)->textures().create(64, 64, render::TextureFormat::Rgba8Srgb);
  static_cast<void>((*renderer)->textures().update(app.texture, 0, 0, 64, 64, pixels, 64 * 4,
                                                   render::SourceAlpha::Straight));

  auto main_window = (*shell)->create_window({"AxeUI - main window", 800, 460, true});
  auto second_window = (*shell)->create_window({"AxeUI - second window", 420, 320, true});
  if (!main_window || !second_window) {
    static_cast<void>(std::fprintf(stderr, "cannot create windows\n"));
    return 1;
  }
  app.main_window = (*main_window)->id();
  app.second_window = (*second_window)->id();
  sdl3::SdlWindow* second = *second_window;

  while ((*shell)->pump()) {
    const auto main_state = core.surface(app.main_window);
    if (!main_state || (*main_state)->close_requested) {
      break;  // Closing the main window quits.
    }
    if (second != nullptr) {
      const auto second_state = core.surface(app.second_window);
      if (second_state && (*second_state)->close_requested) {
        (*shell)->destroy_window(second);  // The main window keeps running.
        second = nullptr;
      }
    }

    // Hover and click on the button: plain rect containment.
    bool hovered = false;
    bool pressed = false;
    for (const platform::PointerState& pointer : (*main_state)->pointers) {
      if (kButton.contains(pointer.position)) {
        hovered = true;
        pressed = pressed || pointer.buttons.has(platform::PointerButton::Primary);
      }
    }
    if (hovered != app.hovered || pressed != app.pressed) {
      app.hovered = hovered;
      app.pressed = pressed;
      static_cast<void>(core.set_cursor(app.main_window, hovered ? platform::CursorShape::Pointer
                                                                 : platform::CursorShape::Default));
      static_cast<void>(core.request_redraw(app.main_window));
    }
  }

  if (second != nullptr) {
    (*shell)->destroy_window(second);
  }
  (*shell)->destroy_window(*main_window);
  return 0;
}
