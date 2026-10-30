// CPU cost of building and submitting a frame of 10,000 instances.
//
//   ui_render_benchmark [frames]     (default 200)
//
// Reports, per frame, the time to build the display list (DisplayListBuilder)
// and to submit it (Renderer::render_offscreen without readback: texture
// flush, instance upload, encoding, queue submit), plus the draw-call count.
// GPU execution is asynchronous and not included. Baselines are recorded in
// docs/architecture.md, "Benchmarks".

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "display_list/builder.h"
#include "foundation/tracking_resource.h"
#include "render/renderer.h"
#include "support/test_process.h"

using namespace ui::foundation;
using namespace ui::display_list;
using namespace ui::render;

namespace {

void log_to_stderr(void*, const LogRecord& record) {
  static_cast<void>(std::fprintf(stderr, "[%d] %.*s\n", static_cast<int>(record.level),
                                 static_cast<int>(record.message.size()), record.message.data()));
}

// 10,000 primitives on a 1920x1080 canvas: plain rects, bordered rounded
// rects and textured rects. Textures change every 500 instances (20 batches
// of each of 4 textures, interleaved with untextured runs).
void build_scene(DisplayListBuilder& b, const TextureId* textures) {
  for (int i = 0; i < 10000; ++i) {
    const float x = static_cast<float>((i * 37) % 1900);
    const float y = static_cast<float>((i * 53) % 1060);
    const LogicalRect rect = LogicalRect::from_xywh(x, y, 18.5f, 12.25f);
    const LinearRgbaPremul color{static_cast<float>(i % 7) / 7.0f, 0.4f, 0.6f, 1.0f};
    switch (i % 3) {
      case 0:
        b.fill_rect(rect, color);
        break;
      case 1:
        b.draw_rounded_rect(rect, CornerRadii::uniform(4), color, Border{1, {0, 0, 0, 1}});
        break;
      default:
        b.draw_textured_rect(rect, textures[(i / 500) % 4], {0, 0, 1, 1}, {1, 1, 1, 1});
        break;
    }
  }
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
  ui::test::configure_process_for_tests();
  const int frames = argc > 1 ? std::max(1, std::atoi(argv[1])) : 200;

  HeapResource heap;
  TrackingResource memory(heap);
  const Logger logger(&log_to_stderr, nullptr, LogLevel::Warning);
  auto renderer = Renderer::create(memory, logger);
  if (!renderer) {
    static_cast<void>(
        std::fprintf(stderr, "no WebGPU adapter: %s (skipping)\n", renderer.error().message));
    return 0;
  }
  TextureId textures[4];
  const std::uint8_t texel[4] = {255, 255, 255, 255};
  for (TextureId& texture : textures) {
    texture = *(*renderer)->textures().create(1, 1, TextureFormat::Rgba8Srgb);
    (void)(*renderer)->textures().update(texture, 0, 0, 1, 1, texel, 4, SourceAlpha::Straight);
  }

  DisplayList list(memory);
  DisplayListBuilder builder(list, memory, {1920, 1080}, ScaleFactor{1.0f});
  std::vector<double> build_ms;
  std::vector<double> submit_ms;
  using Clock = std::chrono::steady_clock;
  std::size_t steady_allocations = 0;
  for (int frame = 0; frame < frames + 10; ++frame) {
    if (frame == 10) {
      steady_allocations = memory.stats().total_allocations;  // After warm-up.
    }
    const auto t0 = Clock::now();
    builder.reset({1920, 1080}, ScaleFactor{1.0f});
    build_scene(builder, textures);
    const auto t1 = Clock::now();
    if (auto rendered = (*renderer)->render_offscreen(list, 1920, 1080, {0, 0, 0, 1}, nullptr);
        !rendered) {
      static_cast<void>(std::fprintf(stderr, "render failed: %s\n", rendered.error().message));
      return 1;
    }
    (*renderer)->process_events();
    const auto t2 = Clock::now();
    if (frame >= 10) {
      build_ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
      submit_ms.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
  }

  const FrameStats& stats = (*renderer)->last_frame_stats();
  std::printf("adapter:            %.*s\n",
              static_cast<int>((*renderer)->gpu().adapter_name().size()),
              (*renderer)->gpu().adapter_name().data());
  std::printf("instances:          %u\n", stats.instances);
  std::printf("draw calls:         %u\n", stats.draw_calls);
  std::printf("bind group changes: %u\n", stats.bind_group_changes);
  std::printf("frames measured:    %d\n", frames);
  std::printf("build  ms/frame:    median %.3f\n", median(build_ms));
  std::printf("submit ms/frame:    median %.3f\n", median(submit_ms));
  std::printf("engine allocations after warm-up: %zu\n",
              memory.stats().total_allocations - steady_allocations);
  return memory.stats().total_allocations == steady_allocations ? 0 : 2;
}
