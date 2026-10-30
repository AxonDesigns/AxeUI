#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <webgpu/webgpu_cpp.h>

#include "display_list/display_list.h"
#include "foundation/allocator.h"
#include "foundation/result.h"
#include "foundation/slot_map.h"
#include "render/gpu_context.h"

namespace ui::render {

enum class TextureFormat : std::uint8_t {
  Rgba8Linear,  // 8-bit linear values (data, masks, linear images).
  Rgba8Srgb,    // 8-bit sRGB-encoded values, decoded to linear when sampled.
};

// Alpha convention of pixels passed to TextureManager::update().
enum class SourceAlpha : std::uint8_t {
  Premultiplied,  // Stored as given.
  Straight,       // Premultiplied on upload (in linear space for sRGB textures).
};

// Owns every sampled texture, addressed by display_list::TextureId (a
// generational handle with HandleType::Texture).
//
// Each texture keeps a CPU copy of its (premultiplied) pixels:
// - update() writes the copy immediately and queues the dirty rectangle;
//   flush() uploads all queued rectangles once per frame, straight from the
//   copy, so updates never allocate.
// - After device loss, restore() recreates every GPU texture from its copy,
//   with no application involvement. The cost is one CPU copy per texture
//   (documented in docs/architecture.md, "Device loss").
//
// TextureId{} (none) maps to a 1x1 opaque white texture, so untextured
// instances sample white and never force a bind-group change.
class TextureManager {
 public:
  TextureManager(GpuContext& gpu, foundation::MemoryResource& memory);
  ~TextureManager();

  TextureManager(const TextureManager&) = delete;
  TextureManager& operator=(const TextureManager&) = delete;

  // (Re)creates the bind group layout, sampler and white texture for the
  // current device. Called once at startup and after device loss (followed by
  // restore()).
  [[nodiscard]] foundation::Result<void> create_device_objects();

  [[nodiscard]] foundation::Result<display_list::TextureId> create(std::uint32_t width,
                                                                   std::uint32_t height,
                                                                   TextureFormat format);

  // Replaces the rectangle (x, y, width, height) with `pixels`: RGBA8 rows of
  // `stride` bytes. The upload happens at the next flush().
  [[nodiscard]] foundation::Result<void> update(display_list::TextureId id, std::uint32_t x,
                                                std::uint32_t y, std::uint32_t width,
                                                std::uint32_t height,
                                                std::span<const std::uint8_t> pixels,
                                                std::uint32_t stride, SourceAlpha alpha);

  [[nodiscard]] foundation::Result<void> destroy(display_list::TextureId id);

  // Uploads every rectangle queued by update() since the last flush.
  void flush();

  // Recreates all GPU textures from their CPU copies (after device loss).
  [[nodiscard]] foundation::Result<void> restore();

  // Bind group (texture + sampler) for `id`; the white texture's for
  // TextureId{}. Fails for unknown or destroyed textures.
  [[nodiscard]] foundation::Result<const wgpu::BindGroup*> bind_group(
      display_list::TextureId id) const;

  [[nodiscard]] const wgpu::BindGroupLayout& layout() const noexcept { return layout_; }
  [[nodiscard]] std::size_t count() const noexcept { return textures_.size(); }
  [[nodiscard]] std::size_t pending_updates() const noexcept { return pending_.size(); }

 private:
  struct Record {
    Record(std::uint32_t w, std::uint32_t h, TextureFormat f, foundation::MemoryResource& memory)
        : width(w), height(h), format(f), pixels(foundation::Allocator<std::uint8_t>(memory)) {}

    std::uint32_t width;
    std::uint32_t height;
    TextureFormat format;
    foundation::Vector<std::uint8_t> pixels;  // width * height * 4, premultiplied.
    wgpu::Texture texture;
    wgpu::BindGroup bind_group;
  };

  struct PendingUpdate {
    foundation::Handle handle;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  [[nodiscard]] foundation::Result<void> create_gpu_texture(Record& record);
  void upload(const Record& record, std::uint32_t x, std::uint32_t y, std::uint32_t width,
              std::uint32_t height);

  GpuContext* gpu_;
  foundation::MemoryResource* memory_;
  foundation::SlotMap<Record> textures_;
  foundation::Vector<PendingUpdate> pending_;
  std::array<float, 256> srgb_to_linear_{};

  wgpu::BindGroupLayout layout_;
  wgpu::Sampler sampler_;
  wgpu::Texture white_texture_;
  wgpu::BindGroup white_bind_group_;
};

}  // namespace ui::render
