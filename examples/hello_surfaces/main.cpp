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
//
// Options (for manual and scripted display testing):
//   --animate             redraw continuously (a moving bar in each window)
//   --present=vsync       Fifo on every window (default: lowlatency = Mailbox
//                         where available, else Fifo)
//   --stats               print per-window frame rate, size, scale and refresh
//                         rate once per second
//   --seconds=N           quit after N seconds
//   --script              resize the main window on a timer, including a burst
//                         of rapid resizes (exercises outdated/suboptimal
//                         swapchains)
//   --move-second=X,Y     move the second window to (X, Y) after 2 s (where the
//                         platform lets clients position windows, e.g. X11)
//   --lose-device=N       force GPU device loss after N seconds (native only);
//                         the renderer recovers and the windows keep drawing

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

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

struct Options {
  bool animate = false;
  bool stats = false;
  bool script = false;
  render::PresentPolicy present = render::PresentPolicy::LowLatency;
  double seconds = 0.0;  // 0: run until closed.
  bool move_second = false;
  double lose_device = 0.0;
  int move_x = 0;
  int move_y = 0;
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--animate") == 0) {
      options.animate = true;
    } else if (std::strcmp(arg, "--stats") == 0) {
      options.stats = true;
    } else if (std::strcmp(arg, "--script") == 0) {
      options.script = true;
    } else if (std::strcmp(arg, "--present=vsync") == 0) {
      options.present = render::PresentPolicy::Vsync;
    } else if (std::strcmp(arg, "--present=lowlatency") == 0) {
      options.present = render::PresentPolicy::LowLatency;
    } else if (std::strncmp(arg, "--seconds=", 10) == 0) {
      options.seconds = std::atof(arg + 10);
    } else if (std::strncmp(arg, "--lose-device=", 14) == 0) {
      options.lose_device = std::atof(arg + 14);
    } else if (std::strncmp(arg, "--move-second=", 14) == 0) {
      options.move_second = std::sscanf(arg + 14, "%d,%d", &options.move_x, &options.move_y) == 2;
    } else {
      static_cast<void>(std::fprintf(stderr, "unknown option: %s\n", arg));
    }
  }
  return options;
}

struct App {
  platform::Core* core = nullptr;
  bool animate = false;
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

// A bar sweeping across the bottom of the window, driven by frame time.
void paint_animation(display_list::DisplayListBuilder& b, foundation::LogicalSize size,
                     double seconds) {
  const float travel = std::max(size.width - 60.0f, 1.0f);
  const auto phase = static_cast<float>(std::fmod(seconds * 0.5, 1.0));
  b.fill_rect(LogicalRect::from_xywh(phase * travel, size.height - 24, 60, 16),
              color(250, 120, 30));
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
  if (app.animate) {
    paint_animation(b, size, (*state)->last_frame_time.seconds());
    static_cast<void>(app.core->request_redraw(id));  // Next frame, paced by the shell.
  }
}

void print_stats(const platform::Core& core, const sdl3::SdlShell& shell, platform::SurfaceId id,
                 const char* name, std::uint64_t& last_frames, double elapsed) {
  const auto state = core.surface(id);
  if (!state) {
    return;
  }
  const platform::SurfaceInfo& info = (*state)->info;
  int x = 0;
  int y = 0;
  if (const sdl3::SdlWindow* window = shell.find_window(id)) {
    window->position(x, y);
  }
  const std::uint64_t frames = (*state)->frames_rendered;
  static_cast<void>(std::printf(
      "%-6s fps %6.1f | refresh %6.2f Hz | scale %.3f | physical %4.0fx%-4.0f | logical "
      "%6.1fx%-6.1f | pos %d,%d\n",
      name, static_cast<double>(frames - last_frames) / elapsed,
      static_cast<double>(info.refresh_rate_hz), static_cast<double>(info.scale.value()),
      static_cast<double>(info.physical_size.width), static_cast<double>(info.physical_size.height),
      static_cast<double>(info.logical_size().width),
      static_cast<double>(info.logical_size().height), x, y));
  last_frames = frames;
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

int main(int argc, char** argv) {
  const Options options = parse_options(argc, argv);
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
  render::RendererOptions renderer_options;
  renderer_options.present = options.present;
  auto renderer = render::Renderer::create(memory, logger, renderer_options);
  if (!renderer) {
    static_cast<void>(
        std::fprintf(stderr, "cannot start the renderer: %s\n", renderer.error().message));
    return 1;
  }

  App app;
  app.core = &core;
  app.animate = options.animate;
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
  sdl3::SdlWindow* main = *main_window;

  // Timed modes need the loop to wake up even when no window wants a frame:
  // a helper thread calls the thread-safe Platform::wake() every 50 ms.
  const bool timed = options.stats || options.script || options.seconds > 0.0 ||
                     options.move_second || options.lose_device > 0.0;
  std::atomic<bool> running{true};
  std::thread ticker;
  if (timed) {
    ticker = std::thread([&] {
      while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        (*shell)->wake();
      }
    });
  }
  using Clock = std::chrono::steady_clock;
  const Clock::time_point start = Clock::now();
  Clock::time_point last_stats = start;
  std::uint64_t main_frames = 0;
  std::uint64_t second_frames = 0;
  int script_step = 0;
  bool moved = false;
  bool lost = false;

  while ((*shell)->pump()) {
    const double t = std::chrono::duration<double>(Clock::now() - start).count();
    if (options.seconds > 0.0 && t >= options.seconds) {
      break;
    }
    if (options.stats) {
      const double since = std::chrono::duration<double>(Clock::now() - last_stats).count();
      if (since >= 1.0) {
        print_stats(core, **shell, app.main_window, "main", main_frames, since);
        if (second != nullptr) {
          print_stats(core, **shell, app.second_window, "second", second_frames, since);
        }
        static_cast<void>(std::fflush(stdout));
        last_stats = Clock::now();
      }
    }
    if (options.lose_device > 0.0 && !lost && t >= options.lose_device) {
      lost = true;
      if (auto result = (*renderer)->simulate_device_loss(); !result) {
        static_cast<void>(
            std::fprintf(stderr, "cannot force device loss: %s\n", result.error().message));
      }
      core.request_redraw_all();
    }
    if (options.move_second && !moved && second != nullptr && t >= 2.0) {
      second->set_position(options.move_x, options.move_y);
      moved = true;
    }
    if (options.script) {
      // t = 1.0 s: shrink; 1.5 s: grow; 2.0-3.0 s: a resize every 50 ms;
      // 3.5 s: back to the initial size.
      if (script_step == 0 && t >= 1.0) {
        main->set_size(640, 400);
        ++script_step;
      } else if (script_step == 1 && t >= 1.5) {
        main->set_size(960, 600);
        ++script_step;
      } else if (script_step >= 2 && script_step < 22 && t >= 2.0 + 0.05 * (script_step - 2)) {
        main->set_size(static_cast<float>(600 + 23 * (script_step % 7)),
                       static_cast<float>(380 + 17 * (script_step % 5)));
        ++script_step;
      } else if (script_step == 22 && t >= 3.5) {
        main->set_size(800, 460);
        ++script_step;
        static_cast<void>(std::printf("script: resize sequence done (%llu frames skipped)\n",
                                      static_cast<unsigned long long>(driver.frames_skipped())));
        static_cast<void>(std::fflush(stdout));
      }
    }

    const auto main_state = core.surface(app.main_window);
    if (!main_state || (*main_state)->close_requested) {
      break;  // Closing the main window quits.
    }
    if (second != nullptr) {
      const auto second_state = core.surface(app.second_window);
      if (second_state && (*second_state)->close_requested) {
        static_cast<void>(std::printf("second window closed; main window keeps running\n"));
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
      if (options.stats) {
        static_cast<void>(std::printf("button: %s%s\n", hovered ? "hovered" : "normal",
                                      pressed ? ", pressed" : ""));
        static_cast<void>(std::fflush(stdout));
      }
      app.hovered = hovered;
      app.pressed = pressed;
      static_cast<void>(core.set_cursor(app.main_window, hovered ? platform::CursorShape::Pointer
                                                                 : platform::CursorShape::Default));
      static_cast<void>(core.request_redraw(app.main_window));
    }
  }

  running.store(false);
  if (ticker.joinable()) {
    ticker.join();
  }
  static_cast<void>(std::printf("frames presented %llu, skipped %llu, device recoveries %llu\n",
                                static_cast<unsigned long long>(driver.frames_presented()),
                                static_cast<unsigned long long>(driver.frames_skipped()),
                                static_cast<unsigned long long>((*renderer)->device_recoveries())));
  if (second != nullptr) {
    (*shell)->destroy_window(second);
  }
  (*shell)->destroy_window(main);
  return 0;
}
