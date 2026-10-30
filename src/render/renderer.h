#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <webgpu/webgpu_cpp.h>

#include "display_list/display_list.h"
#include "foundation/allocator.h"
#include "foundation/color.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "platform/native_surface.h"
#include "platform/surface.h"
#include "render/gpu_context.h"
#include "render/texture_manager.h"

namespace ui::render {

// How presentation is paced (see docs/architecture.md, "Multi-surface
// presentation").
enum class PresentPolicy : std::uint8_t {
  // Fifo on every surface: tear-free, waits for vsync when the swapchain is
  // full. Always supported.
  Vsync,
  // Mailbox where the surface supports it (tear-free, never blocks, newest
  // frame wins), else Fifo. The default: several windows never serialize on
  // each other's vsync.
  LowLatency,
};

struct RendererOptions {
  GpuOptions gpu;
  PresentPolicy present = PresentPolicy::LowLatency;
};

// How a surface's color output is produced (docs/architecture.md, "Color
// output").
enum class ColorOutputPath : std::uint8_t {
  SrgbFormat,      // The surface format itself is sRGB: hardware encodes and blends linearly.
  SrgbViewFormat,  // Non-sRGB surface format rendered through an sRGB view.
  ShaderEncode,    // No sRGB option: the shader encodes; blending is not linear.
};

enum class PresentOutcome : std::uint8_t {
  Presented,
  SkippedZeroSize,    // Minimized: nothing to draw, no frame needed.
  SkippedTimeout,     // No texture in time; try again next frame.
  SkippedOutdated,    // Reconfigured; draw again next frame.
  SkippedLost,        // Surface recreated; draw again next frame.
  SkippedDeviceLost,  // Device lost; it is recreated at the next render call.
};

// Counters for the most recent render call (any target).
struct FrameStats {
  std::uint32_t instances = 0;
  std::uint32_t draw_calls = 0;
  std::uint32_t bind_group_changes = 0;
  std::uint32_t missing_textures = 0;  // Instances referencing destroyed textures (drawn white).
};

// Renders display lists with WebGPU.
//
// - One GpuContext (instance/adapter/device/queue) for the process, shared
//   by all targets.
// - One surface target per SurfaceId, configured from the surface's native
//   handle and reconfigured on resize, scale change, outdated/lost status.
// - One render pipeline for every primitive; one draw call per display-list
//   command; untextured batches bind the white texture.
// - Instances go to a per-target ring of storage buffers, one per frame in
//   flight, grown geometrically and never shrunk.
// - Device loss is detected at the start of every render call and recovered
//   in place: new device, pipelines, buffers, surfaces and textures.
//
// After warm-up, rendering an unchanged scene makes no engine allocations.
class Renderer final {
 public:
  static constexpr std::uint32_t kFramesInFlight = 3;

  [[nodiscard]] static foundation::Result<foundation::UniquePtr<Renderer>> create(
      foundation::MemoryResource& memory, const foundation::Logger& logger,
      const RendererOptions& options = {});

  Renderer(foundation::MemoryResource& memory, const foundation::Logger& logger,
           const RendererOptions& options);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // --- Textures -----------------------------------------------------------------

  [[nodiscard]] TextureManager& textures() noexcept { return textures_; }

  // --- Surfaces -----------------------------------------------------------------

  // Renders `list` into the surface's next swapchain texture and presents it.
  // The first call for an id creates the target from `native`.
  [[nodiscard]] foundation::Result<PresentOutcome> render_surface(
      platform::SurfaceId id, const platform::NativeSurfaceHandle& native,
      const platform::SurfaceInfo& info, const display_list::DisplayList& list,
      foundation::LinearRgbaPremul clear);

  // Releases the target of a surface that went away.
  void remove_surface(platform::SurfaceId id);

  // Color path chosen for a configured surface (Unsupported if none yet).
  [[nodiscard]] foundation::Result<ColorOutputPath> color_output_path(platform::SurfaceId id) const;

  // --- Headless -------------------------------------------------------------------

  // Renders `list` into an offscreen RGBA8 sRGB texture of `width` x
  // `height`. With `readback`, copies the result into it as tightly packed
  // rows of RGBA8: sRGB-encoded, premultiplied alpha (the framebuffer bytes).
  [[nodiscard]] foundation::Result<void> render_offscreen(
      const display_list::DisplayList& list, std::uint32_t width, std::uint32_t height,
      foundation::LinearRgbaPremul clear, foundation::Vector<std::uint8_t>* readback);

  // --- Device ---------------------------------------------------------------------

  [[nodiscard]] GpuContext& gpu() noexcept { return gpu_; }
  // Delivers pending GPU callbacks; call once per loop iteration.
  void process_events() { gpu_.process_events(); }
  // Test hook: forces device loss (native only). Recovery happens at the
  // next render call.
  [[nodiscard]] foundation::Result<void> simulate_device_loss() { return gpu_.force_device_loss(); }
  // Number of device recoveries so far.
  [[nodiscard]] std::uint64_t device_recoveries() const noexcept { return recoveries_; }

  [[nodiscard]] const FrameStats& last_frame_stats() const noexcept { return stats_; }

 private:
  struct FrameResources {
    wgpu::Buffer globals;
    wgpu::Buffer instances[kFramesInFlight];
    wgpu::BindGroup bind_groups[kFramesInFlight];
    std::uint64_t capacity[kFramesInFlight] = {};
    std::uint32_t next = 0;
  };

  struct SurfaceTarget {
    platform::SurfaceId id;
    platform::NativeSurfaceHandle native;
    wgpu::Surface surface;
    bool configured = false;
    bool reconfigure = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;       // Configured format.
    wgpu::TextureFormat view_format = wgpu::TextureFormat::Undefined;  // Render format.
    ColorOutputPath color_path = ColorOutputPath::SrgbFormat;
    FrameResources frame;
  };

  struct PipelineEntry {
    wgpu::TextureFormat format;
    bool encode_srgb;
    wgpu::RenderPipeline pipeline;
  };

  struct Offscreen {
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Buffer readback;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t padded_row = 0;
    FrameResources frame;
  };

  [[nodiscard]] foundation::Result<void> initialize();
  [[nodiscard]] foundation::Result<void> create_device_objects();
  [[nodiscard]] foundation::Result<void> ensure_device();
  [[nodiscard]] foundation::Result<const wgpu::RenderPipeline*> pipeline(wgpu::TextureFormat format,
                                                                         bool encode_srgb);
  [[nodiscard]] foundation::Result<void> prepare_frame(FrameResources& frame,
                                                       const display_list::DisplayList& list,
                                                       std::uint32_t width, std::uint32_t height,
                                                       std::uint32_t& slot);
  void encode_pass(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& view,
                   const wgpu::RenderPipeline& pipeline, const FrameResources& frame,
                   std::uint32_t slot, const display_list::DisplayList& list,
                   foundation::LinearRgbaPremul clear);
  [[nodiscard]] foundation::Result<SurfaceTarget*> surface_target(
      platform::SurfaceId id, const platform::NativeSurfaceHandle& native);
  [[nodiscard]] foundation::Result<void> create_wgpu_surface(SurfaceTarget& target);
  [[nodiscard]] foundation::Result<void> configure(SurfaceTarget& target, std::uint32_t width,
                                                   std::uint32_t height);

  foundation::MemoryResource* memory_;
  const foundation::Logger* logger_;
  RendererOptions options_;
  GpuContext gpu_;
  TextureManager textures_;
  std::uint64_t device_generation_ = 0;
  std::uint64_t recoveries_ = 0;

  wgpu::ShaderModule shader_;
  wgpu::BindGroupLayout frame_layout_;
  wgpu::PipelineLayout pipeline_layout_;
  foundation::Vector<PipelineEntry> pipelines_;
  foundation::Vector<SurfaceTarget> surfaces_;
  Offscreen offscreen_;
  FrameStats stats_;
};

}  // namespace ui::render
