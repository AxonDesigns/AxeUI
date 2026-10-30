#include "render/texture_manager.h"

#include <cmath>
#include <cstring>

#include "foundation/color.h"

namespace ui::render {

using foundation::Error;
using foundation::ErrorCode;
using foundation::Handle;

namespace {

wgpu::TextureFormat to_wgpu(TextureFormat format) noexcept {
  return format == TextureFormat::Rgba8Srgb ? wgpu::TextureFormat::RGBA8UnormSrgb
                                            : wgpu::TextureFormat::RGBA8Unorm;
}

Handle to_handle(display_list::TextureId id) noexcept { return Handle::from_bits(id.value); }

std::uint8_t premultiply_linear(std::uint8_t c, std::uint8_t a) noexcept {
  return static_cast<std::uint8_t>((static_cast<unsigned>(c) * a + 127u) / 255u);
}

}  // namespace

TextureManager::TextureManager(GpuContext& gpu, foundation::MemoryResource& memory)
    : gpu_(&gpu),
      memory_(&memory),
      textures_(memory, foundation::HandleType::Texture),
      pending_(foundation::Allocator<PendingUpdate>(memory)) {
  for (std::size_t i = 0; i < srgb_to_linear_.size(); ++i) {
    srgb_to_linear_[i] = foundation::srgb_to_linear(static_cast<float>(i) / 255.0f);
  }
}

TextureManager::~TextureManager() = default;

foundation::Result<void> TextureManager::create_device_objects() {
  const wgpu::Device& device = gpu_->device();

  wgpu::BindGroupLayoutEntry entries[2]{};
  entries[0].binding = 0;
  entries[0].visibility = wgpu::ShaderStage::Fragment;
  entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
  entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
  entries[1].binding = 1;
  entries[1].visibility = wgpu::ShaderStage::Fragment;
  entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.label = "AxeUI texture group";
  layout_desc.entryCount = 2;
  layout_desc.entries = entries;
  layout_ = device.CreateBindGroupLayout(&layout_desc);

  wgpu::SamplerDescriptor sampler_desc{};
  sampler_desc.label = "AxeUI linear clamp";
  sampler_desc.magFilter = wgpu::FilterMode::Linear;
  sampler_desc.minFilter = wgpu::FilterMode::Linear;
  sampler_desc.addressModeU = wgpu::AddressMode::ClampToEdge;
  sampler_desc.addressModeV = wgpu::AddressMode::ClampToEdge;
  sampler_ = device.CreateSampler(&sampler_desc);

  wgpu::TextureDescriptor white_desc{};
  white_desc.label = "AxeUI white";
  white_desc.size = {1, 1, 1};
  white_desc.format = wgpu::TextureFormat::RGBA8Unorm;
  white_desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  white_texture_ = device.CreateTexture(&white_desc);
  static constexpr std::uint8_t kWhite[4] = {255, 255, 255, 255};
  wgpu::TexelCopyTextureInfo destination{};
  destination.texture = white_texture_;
  wgpu::TexelCopyBufferLayout data_layout{};
  data_layout.bytesPerRow = 4;
  data_layout.rowsPerImage = 1;
  const wgpu::Extent3D extent{1, 1, 1};
  gpu_->queue().WriteTexture(&destination, kWhite, sizeof(kWhite), &data_layout, &extent);

  wgpu::BindGroupEntry group_entries[2]{};
  group_entries[0].binding = 0;
  group_entries[0].textureView = white_texture_.CreateView();
  group_entries[1].binding = 1;
  group_entries[1].sampler = sampler_;
  wgpu::BindGroupDescriptor group_desc{};
  group_desc.label = "AxeUI white group";
  group_desc.layout = layout_;
  group_desc.entryCount = 2;
  group_desc.entries = group_entries;
  white_bind_group_ = device.CreateBindGroup(&group_desc);
  if (layout_ == nullptr || sampler_ == nullptr || white_bind_group_ == nullptr) {
    return foundation::fail(Error{ErrorCode::GpuError, "failed to create texture objects"});
  }
  return {};
}

foundation::Result<void> TextureManager::create_gpu_texture(Record& record) {
  const wgpu::Device& device = gpu_->device();
  wgpu::TextureDescriptor desc{};
  desc.label = "AxeUI texture";
  desc.size = {record.width, record.height, 1};
  desc.format = to_wgpu(record.format);
  desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
  record.texture = device.CreateTexture(&desc);

  wgpu::BindGroupEntry entries[2]{};
  entries[0].binding = 0;
  entries[0].textureView = record.texture.CreateView();
  entries[1].binding = 1;
  entries[1].sampler = sampler_;
  wgpu::BindGroupDescriptor group_desc{};
  group_desc.layout = layout_;
  group_desc.entryCount = 2;
  group_desc.entries = entries;
  record.bind_group = device.CreateBindGroup(&group_desc);
  if (record.texture == nullptr || record.bind_group == nullptr) {
    return foundation::fail(Error{ErrorCode::GpuError, "texture creation failed"});
  }
  return {};
}

foundation::Result<display_list::TextureId> TextureManager::create(std::uint32_t width,
                                                                   std::uint32_t height,
                                                                   TextureFormat format) {
  // WebGPU guarantees 8192 for maxTextureDimension2D.
  if (width == 0 || height == 0 || width > 8192 || height > 8192) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "texture size must be 1..8192"});
  }
  auto handle = textures_.emplace(width, height, format, *memory_);
  if (!handle) {
    return foundation::fail(std::move(handle).error());
  }
  Record& record = **textures_.get(*handle);
  record.pixels.assign(static_cast<std::size_t>(width) * height * 4u, std::uint8_t{0});
  if (auto created = create_gpu_texture(record); !created) {
    static_cast<void>(textures_.remove(*handle));
    return foundation::fail(std::move(created).error());
  }
  return display_list::TextureId{handle->bits()};
}

foundation::Result<void> TextureManager::update(display_list::TextureId id, std::uint32_t x,
                                                std::uint32_t y, std::uint32_t width,
                                                std::uint32_t height,
                                                std::span<const std::uint8_t> pixels,
                                                std::uint32_t stride, SourceAlpha alpha) {
  auto found = textures_.get(to_handle(id));
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  Record& record = **found;
  if (width == 0 || height == 0 || x > record.width || y > record.height ||
      width > record.width - x || height > record.height - y) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "update rectangle out of bounds"});
  }
  if (stride < width * 4u ||
      pixels.size() < static_cast<std::size_t>(stride) * (height - 1u) + width * 4u) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "pixel data too small for update"});
  }

  for (std::uint32_t row = 0; row < height; ++row) {
    const std::uint8_t* src = pixels.data() + static_cast<std::size_t>(row) * stride;
    std::uint8_t* dst =
        record.pixels.data() + (static_cast<std::size_t>(y + row) * record.width + x) * 4u;
    if (alpha == SourceAlpha::Premultiplied) {
      std::memcpy(dst, src, static_cast<std::size_t>(width) * 4u);
      continue;
    }
    for (std::uint32_t col = 0; col < width; ++col) {
      const std::uint8_t* s = src + col * 4u;
      std::uint8_t* d = dst + col * 4u;
      const std::uint8_t a = s[3];
      if (record.format == TextureFormat::Rgba8Linear) {
        d[0] = premultiply_linear(s[0], a);
        d[1] = premultiply_linear(s[1], a);
        d[2] = premultiply_linear(s[2], a);
      } else {
        // Premultiply in linear light, then re-encode: sampling decodes to
        // exactly the premultiplied linear color.
        const float af = static_cast<float>(a) / 255.0f;
        for (int c = 0; c < 3; ++c) {
          const float encoded = foundation::linear_to_srgb(srgb_to_linear_[s[c]] * af);
          d[c] = static_cast<std::uint8_t>(std::lround(encoded * 255.0f));
        }
      }
      d[3] = a;
    }
  }
  pending_.push_back(PendingUpdate{to_handle(id), x, y, width, height});
  return {};
}

foundation::Result<void> TextureManager::destroy(display_list::TextureId id) {
  auto removed = textures_.remove(to_handle(id));
  if (!removed) {
    return foundation::fail(std::move(removed).error());
  }
  // Queued updates for it are skipped by flush(); GPU work already recorded
  // keeps the texture alive until it completes.
  return {};
}

void TextureManager::upload(const Record& record, std::uint32_t x, std::uint32_t y,
                            std::uint32_t width, std::uint32_t height) {
  wgpu::TexelCopyTextureInfo destination{};
  destination.texture = record.texture;
  destination.origin = {x, y, 0};
  // Point at the rectangle's first byte in the CPU copy; rows keep the full
  // texture stride. (writeTexture has no 256-byte row alignment rule.)
  const std::size_t offset = (static_cast<std::size_t>(y) * record.width + x) * 4u;
  const std::size_t size = static_cast<std::size_t>(height - 1u) * record.width * 4u +
                           static_cast<std::size_t>(width) * 4u;
  wgpu::TexelCopyBufferLayout data_layout{};
  data_layout.bytesPerRow = record.width * 4u;
  data_layout.rowsPerImage = height;
  const wgpu::Extent3D extent{width, height, 1};
  gpu_->queue().WriteTexture(&destination, record.pixels.data() + offset, size, &data_layout,
                             &extent);
}

void TextureManager::flush() {
  for (const PendingUpdate& update : pending_) {
    auto found = textures_.get(update.handle);
    if (found) {
      upload(**found, update.x, update.y, update.width, update.height);
    }
  }
  pending_.clear();
}

foundation::Result<void> TextureManager::restore() {
  pending_.clear();  // Everything is re-uploaded in full.
  foundation::Result<void> result;
  textures_.for_each([&](Handle, Record& record) {
    if (auto created = create_gpu_texture(record); !created) {
      result = std::move(created);
      return;
    }
    upload(record, 0, 0, record.width, record.height);
  });
  return result;
}

foundation::Result<const wgpu::BindGroup*> TextureManager::bind_group(
    display_list::TextureId id) const {
  if (id.is_none()) {
    return &white_bind_group_;
  }
  auto found = textures_.get(to_handle(id));
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  return &(*found)->bind_group;
}

}  // namespace ui::render
