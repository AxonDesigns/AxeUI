#pragma once

#include <cstdint>

#include "display_list/builder.h"
#include "display_list/display_list.h"
#include "foundation/allocator.h"
#include "foundation/color.h"
#include "platform/core.h"
#include "render/renderer.h"

namespace ui::frame {

// Paints one surface's frame. Called with the builder already reset to the
// surface's physical size and scale factor.
using PaintCallback = void (*)(void* user_data, platform::SurfaceId surface,
                               display_list::DisplayListBuilder& builder);

// Orchestrates shell, application and renderer for each frame:
//
//   shell --Core::frame(id, t)--> Core --render_surface--> FrameDriver
//     --paint(id, builder)--> application        (fills the display list)
//     --render_surface(...)--> Renderer           (draws and presents)
//
// This is the seam where the element tree plugs in later: the paint callback
// becomes "paint the tree of this surface". Keep it small.
//
// Each surface owns a DisplayList and a DisplayListBuilder that are reused
// frame after frame, so steady-state frames do not allocate. Surfaces render
// and present independently; a skipped frame (swapchain timeout, outdated or
// lost surface, device loss) asks the core for another frame.
class FrameDriver final : public platform::FrameHandler {
 public:
  // Registers itself as `core`'s frame handler; unregisters on destruction.
  FrameDriver(platform::Core& core, render::Renderer& renderer, foundation::MemoryResource& memory,
              PaintCallback paint, void* user_data);
  ~FrameDriver() override;

  FrameDriver(const FrameDriver&) = delete;
  FrameDriver& operator=(const FrameDriver&) = delete;

  // Background every frame starts from (default: opaque black).
  void set_clear_color(foundation::LinearRgbaPremul color) noexcept { clear_ = color; }

  // Frames presented (or rendered, for headless surfaces) so far.
  [[nodiscard]] std::uint64_t frames_presented() const noexcept { return presented_; }
  // Frames the renderer skipped (each one requested a retry).
  [[nodiscard]] std::uint64_t frames_skipped() const noexcept { return skipped_; }
  [[nodiscard]] render::PresentOutcome last_outcome() const noexcept { return last_outcome_; }

  // platform::FrameHandler
  void render_surface(platform::SurfaceId id, const platform::SurfaceState& state,
                      foundation::Timestamp time) override;
  void surface_removed(platform::SurfaceId id) override;

 private:
  struct SurfaceFrame {
    SurfaceFrame(platform::SurfaceId surface, foundation::MemoryResource& memory)
        : id(surface), list(memory), builder(list, memory, {}, foundation::ScaleFactor{1.0f}) {}

    platform::SurfaceId id;
    display_list::DisplayList list;
    display_list::DisplayListBuilder builder;
  };

  [[nodiscard]] SurfaceFrame* find_or_create(platform::SurfaceId id);

  platform::Core* core_;
  render::Renderer* renderer_;
  foundation::MemoryResource* memory_;
  PaintCallback paint_;
  void* user_data_;
  foundation::LinearRgbaPremul clear_{0.0f, 0.0f, 0.0f, 1.0f};
  foundation::Vector<foundation::UniquePtr<SurfaceFrame>> surfaces_;
  std::uint64_t presented_ = 0;
  std::uint64_t skipped_ = 0;
  render::PresentOutcome last_outcome_ = render::PresentOutcome::Presented;
};

}  // namespace ui::frame
