#include "render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "display_list/instance.h"
#include "render/shader_sources.h"

namespace ui::render {

using display_list::DisplayList;
using display_list::DrawCommand;
using display_list::Instance;
using foundation::Error;
using foundation::ErrorCode;
using foundation::LogLevel;

namespace {

constexpr std::uint64_t kInitialInstanceCapacity = 256;
constexpr std::uint32_t kGlobalsSize = 16;
constexpr wgpu::TextureFormat kOffscreenFormat = wgpu::TextureFormat::RGBA8UnormSrgb;

bool is_srgb(wgpu::TextureFormat f) noexcept {
  return f == wgpu::TextureFormat::BGRA8UnormSrgb || f == wgpu::TextureFormat::RGBA8UnormSrgb;
}

wgpu::TextureFormat srgb_variant(wgpu::TextureFormat f) noexcept {
  switch (f) {
    case wgpu::TextureFormat::BGRA8Unorm:
      return wgpu::TextureFormat::BGRA8UnormSrgb;
    case wgpu::TextureFormat::RGBA8Unorm:
      return wgpu::TextureFormat::RGBA8UnormSrgb;
    default:
      return wgpu::TextureFormat::Undefined;
  }
}

std::uint32_t to_pixels(float v) noexcept {
  return v > 0.0f ? static_cast<std::uint32_t>(std::lround(v)) : 0u;
}

struct MapResult {
  bool ok = false;
};

struct ScopeResult {
  bool error = false;
};

}  // namespace

// --- Construction -----------------------------------------------------------------

foundation::Result<foundation::UniquePtr<Renderer>> Renderer::create(
    foundation::MemoryResource& memory, const foundation::Logger& logger,
    const RendererOptions& options) {
  auto renderer = foundation::allocate_unique<Renderer>(memory, memory, logger, options);
  if (!renderer) {
    return foundation::fail(std::move(renderer).error());
  }
  UI_TRY((*renderer)->initialize());
  return std::move(renderer).value();
}

Renderer::Renderer(foundation::MemoryResource& memory, const foundation::Logger& logger,
                   const RendererOptions& options)
    : logger_(&logger),
      options_(options),
      gpu_(logger),
      textures_(gpu_, memory),
      pipelines_(foundation::Allocator<PipelineEntry>(memory)),
      surfaces_(foundation::Allocator<SurfaceTarget>(memory)) {}

Renderer::~Renderer() {
  for (SurfaceTarget& target : surfaces_) {
    if (target.configured) {
      target.surface.Unconfigure();
    }
  }
}

foundation::Result<void> Renderer::initialize() {
  UI_TRY(gpu_.initialize(options_.gpu));
  return ensure_device();
}

foundation::Result<void> Renderer::ensure_device() {
  if (gpu_.lost()) {
    UI_TRY(gpu_.recreate_device());
  }
  if (gpu_.generation() != device_generation_) {
    const bool recovering = device_generation_ != 0;
    UI_TRY(create_device_objects());
    for (SurfaceTarget& target : surfaces_) {
      target.frame = FrameResources{};
      target.offscreen = Offscreen{};
      target.configured = false;  // Reconfigure with the new device.
    }
    offscreen_ = Offscreen{};
    if (recovering) {
      UI_TRY(textures_.restore());
      ++recoveries_;
      logger_->log(LogLevel::Warning, ErrorCode::DeviceLost, "GPU device recovered");
    }
    device_generation_ = gpu_.generation();
  }
  if (!gpu_.ready()) {
    return foundation::fail(Error{ErrorCode::DeviceLost, "GPU device is not available"});
  }
  return {};
}

foundation::Result<void> Renderer::create_device_objects() {
  const wgpu::Device& device = gpu_.device();
  UI_TRY(textures_.create_device_objects());

  wgpu::ShaderSourceWGSL wgsl{};
  wgsl.code = wgpu::StringView(shaders::kUiWgsl, shaders::kUiWgsl_size);
  wgpu::ShaderModuleDescriptor module_desc{};
  module_desc.nextInChain = &wgsl;
  module_desc.label = "AxeUI ui.wgsl";
  shader_ = device.CreateShaderModule(&module_desc);

  wgpu::BindGroupLayoutEntry entries[2]{};
  entries[0].binding = 0;
  entries[0].visibility = wgpu::ShaderStage::Vertex;
  entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
  entries[0].buffer.minBindingSize = kGlobalsSize;
  entries[1].binding = 1;
  entries[1].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
  entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
  entries[1].buffer.minBindingSize = sizeof(Instance);
  wgpu::BindGroupLayoutDescriptor layout_desc{};
  layout_desc.label = "AxeUI frame group";
  layout_desc.entryCount = 2;
  layout_desc.entries = entries;
  frame_layout_ = device.CreateBindGroupLayout(&layout_desc);

  const wgpu::BindGroupLayout layouts[2] = {frame_layout_, textures_.layout()};
  wgpu::PipelineLayoutDescriptor pipeline_layout_desc{};
  pipeline_layout_desc.bindGroupLayoutCount = 2;
  pipeline_layout_desc.bindGroupLayouts = layouts;
  pipeline_layout_ = device.CreatePipelineLayout(&pipeline_layout_desc);

  pipelines_.clear();
  if (shader_ == nullptr || frame_layout_ == nullptr || pipeline_layout_ == nullptr) {
    return foundation::fail(Error{ErrorCode::GpuError, "failed to create renderer objects"});
  }
  return {};
}

foundation::Result<const wgpu::RenderPipeline*> Renderer::pipeline(wgpu::TextureFormat format,
                                                                   bool encode_srgb) {
  for (const PipelineEntry& entry : pipelines_) {
    if (entry.format == format && entry.encode_srgb == encode_srgb) {
      return &entry.pipeline;
    }
  }

  wgpu::BlendState blend{};
  blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                 wgpu::BlendFactor::OneMinusSrcAlpha};
  blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                 wgpu::BlendFactor::OneMinusSrcAlpha};
  wgpu::ColorTargetState target{};
  target.format = format;
  target.blend = &blend;

  wgpu::ConstantEntry constant{};
  constant.key = "encode_srgb";
  constant.value = encode_srgb ? 1.0 : 0.0;

  wgpu::FragmentState fragment{};
  fragment.module = shader_;
  fragment.entryPoint = "fs_main";
  fragment.constantCount = 1;
  fragment.constants = &constant;
  fragment.targetCount = 1;
  fragment.targets = &target;

  wgpu::RenderPipelineDescriptor desc{};
  desc.label = "AxeUI primitives";
  desc.layout = pipeline_layout_;
  desc.vertex.module = shader_;
  desc.vertex.entryPoint = "vs_main";
  desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
  desc.primitive.cullMode = wgpu::CullMode::None;
  desc.fragment = &fragment;

  wgpu::RenderPipeline created = gpu_.device().CreateRenderPipeline(&desc);
  if (created == nullptr) {
    return foundation::fail(Error{ErrorCode::GpuError, "render pipeline creation failed"});
  }
  pipelines_.push_back(PipelineEntry{format, encode_srgb, std::move(created)});
  return &pipelines_.back().pipeline;
}

// --- Per-frame work ---------------------------------------------------------------

foundation::Result<void> Renderer::prepare_frame(FrameResources& frame, const DisplayList& list,
                                                 std::uint32_t width, std::uint32_t height,
                                                 std::uint32_t& slot) {
  const wgpu::Device& device = gpu_.device();
  if (frame.globals == nullptr) {
    wgpu::BufferDescriptor desc{};
    desc.label = "AxeUI globals";
    desc.size = kGlobalsSize;
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    frame.globals = device.CreateBuffer(&desc);
  }

  slot = frame.next;
  frame.next = (frame.next + 1) % kFramesInFlight;

  const std::span<const Instance> instances = list.instances();
  const std::uint64_t needed = std::max<std::uint64_t>(instances.size(), 1) * sizeof(Instance);
  if (frame.capacity[slot] < needed) {
    // Grow geometrically; never shrink.
    std::uint64_t capacity =
        std::max(frame.capacity[slot] * 2, kInitialInstanceCapacity * sizeof(Instance));
    capacity = std::max(capacity, needed);
    wgpu::BufferDescriptor desc{};
    desc.label = "AxeUI instances";
    desc.size = capacity;
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    frame.instances[slot] = device.CreateBuffer(&desc);

    wgpu::BindGroupEntry entries[2]{};
    entries[0].binding = 0;
    entries[0].buffer = frame.globals;
    entries[0].size = kGlobalsSize;
    entries[1].binding = 1;
    entries[1].buffer = frame.instances[slot];
    entries[1].size = capacity;
    wgpu::BindGroupDescriptor group_desc{};
    group_desc.layout = frame_layout_;
    group_desc.entryCount = 2;
    group_desc.entries = entries;
    frame.bind_groups[slot] = device.CreateBindGroup(&group_desc);
    if (frame.instances[slot] == nullptr || frame.bind_groups[slot] == nullptr) {
      frame.capacity[slot] = 0;
      return foundation::fail(Error{ErrorCode::GpuError, "instance buffer allocation failed"});
    }
    frame.capacity[slot] = capacity;
  }

  const float globals[4] = {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f};
  gpu_.queue().WriteBuffer(frame.globals, 0, globals, sizeof(globals));
  if (!instances.empty()) {
    gpu_.queue().WriteBuffer(frame.instances[slot], 0, instances.data(),
                             instances.size() * sizeof(Instance));
  }
  return {};
}

void Renderer::encode_pass(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& view,
                           const wgpu::RenderPipeline& pipeline, const FrameResources& frame,
                           std::uint32_t slot, const DisplayList& list,
                           foundation::LinearRgbaPremul clear) {
  wgpu::RenderPassColorAttachment color{};
  color.view = view;
  color.loadOp = wgpu::LoadOp::Clear;
  color.storeOp = wgpu::StoreOp::Store;
  color.clearValue = {clear.r, clear.g, clear.b, clear.a};
  wgpu::RenderPassDescriptor pass_desc{};
  pass_desc.label = "AxeUI frame";
  pass_desc.colorAttachmentCount = 1;
  pass_desc.colorAttachments = &color;

  stats_ = FrameStats{};
  stats_.instances = static_cast<std::uint32_t>(list.instances().size());

  const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&pass_desc);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, frame.bind_groups[slot]);
  const wgpu::BindGroup* bound = nullptr;
  for (const DrawCommand& command : list.commands()) {
    if (command.kind != display_list::CommandKind::DrawInstances || command.instance_count == 0) {
      continue;  // ClipMask is reserved and never emitted yet.
    }
    auto group = textures_.bind_group(command.texture);
    if (!group) {
      // Destroyed texture: draw the batch untextured rather than skipping it.
      stats_.missing_textures += command.instance_count;
      group = textures_.bind_group(display_list::TextureId{});
    }
    if (*group != bound) {
      pass.SetBindGroup(1, **group);
      bound = *group;
      ++stats_.bind_group_changes;
    }
    pass.Draw(6, command.instance_count, 0, command.first_instance);
    ++stats_.draw_calls;
  }
  pass.End();
}

// --- Headless ---------------------------------------------------------------------

foundation::Result<void> Renderer::ensure_offscreen(Offscreen& offscreen, std::uint32_t width,
                                                    std::uint32_t height) {
  if (offscreen.width == width && offscreen.height == height && offscreen.texture != nullptr) {
    return {};
  }
  const wgpu::Device& device = gpu_.device();
  wgpu::TextureDescriptor desc{};
  desc.label = "AxeUI offscreen";
  desc.size = {width, height, 1};
  desc.format = kOffscreenFormat;
  desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
  offscreen.texture = device.CreateTexture(&desc);
  offscreen.view = offscreen.texture.CreateView();
  // Buffer copies need rows padded to 256 bytes.
  offscreen.padded_row = (width * 4u + 255u) & ~255u;
  wgpu::BufferDescriptor buffer_desc{};
  buffer_desc.label = "AxeUI readback";
  buffer_desc.size = static_cast<std::uint64_t>(offscreen.padded_row) * height;
  buffer_desc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
  offscreen.readback = device.CreateBuffer(&buffer_desc);
  offscreen.width = width;
  offscreen.height = height;
  if (offscreen.texture == nullptr || offscreen.readback == nullptr) {
    offscreen.texture = nullptr;
    return foundation::fail(Error{ErrorCode::GpuError, "offscreen target creation failed"});
  }
  return {};
}

foundation::Result<void> Renderer::read_texture(Offscreen& offscreen,
                                                foundation::Vector<std::uint8_t>& out) {
  const std::uint32_t width = offscreen.width;
  const std::uint32_t height = offscreen.height;
  const wgpu::CommandEncoder encoder = gpu_.device().CreateCommandEncoder();
  wgpu::TexelCopyTextureInfo source{};
  source.texture = offscreen.texture;
  wgpu::TexelCopyBufferInfo destination{};
  destination.buffer = offscreen.readback;
  destination.layout.bytesPerRow = offscreen.padded_row;
  destination.layout.rowsPerImage = height;
  const wgpu::Extent3D extent{width, height, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  const wgpu::CommandBuffer commands = encoder.Finish();
  gpu_.queue().Submit(1, &commands);

  MapResult map_result;
  const std::size_t size = static_cast<std::size_t>(offscreen.padded_row) * height;
  const wgpu::Future future = offscreen.readback.MapAsync(
      wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
      [](wgpu::MapAsyncStatus status, wgpu::StringView, MapResult* result) {
        result->ok = status == wgpu::MapAsyncStatus::Success;
      },
      &map_result);
  UI_TRY(gpu_.wait(future));
  if (!map_result.ok) {
    return foundation::fail(Error{ErrorCode::GpuError, "readback mapping failed"});
  }
  const auto* mapped =
      static_cast<const std::uint8_t*>(offscreen.readback.GetConstMappedRange(0, size));
  const std::size_t row_bytes = static_cast<std::size_t>(width) * 4u;
  out.resize(row_bytes * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    std::memcpy(out.data() + row_bytes * y,
                mapped + static_cast<std::size_t>(offscreen.padded_row) * y, row_bytes);
  }
  offscreen.readback.Unmap();
  return {};
}

foundation::Result<void> Renderer::render_offscreen(const DisplayList& list, std::uint32_t width,
                                                    std::uint32_t height,
                                                    foundation::LinearRgbaPremul clear,
                                                    foundation::Vector<std::uint8_t>* readback) {
  if (width == 0 || height == 0 || width > 8192 || height > 8192) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "offscreen size must be 1..8192"});
  }
  UI_TRY(ensure_device());
  textures_.flush();
  UI_TRY(ensure_offscreen(offscreen_, width, height));
  auto pipe = pipeline(kOffscreenFormat, false);
  if (!pipe) {
    return foundation::fail(std::move(pipe).error());
  }
  std::uint32_t slot = 0;
  UI_TRY(prepare_frame(offscreen_.frame, list, width, height, slot));
  const wgpu::CommandEncoder encoder = gpu_.device().CreateCommandEncoder();
  encode_pass(encoder, offscreen_.view, **pipe, offscreen_.frame, slot, list, clear);
  const wgpu::CommandBuffer commands = encoder.Finish();
  gpu_.queue().Submit(1, &commands);
  if (readback == nullptr) {
    return {};
  }
  return read_texture(offscreen_, *readback);
}

foundation::Result<PresentOutcome> Renderer::render_headless(SurfaceTarget& target,
                                                             std::uint32_t width,
                                                             std::uint32_t height,
                                                             const DisplayList& list,
                                                             foundation::LinearRgbaPremul clear) {
  UI_TRY(ensure_offscreen(target.offscreen, width, height));
  auto pipe = pipeline(kOffscreenFormat, false);
  if (!pipe) {
    return foundation::fail(std::move(pipe).error());
  }
  std::uint32_t slot = 0;
  UI_TRY(prepare_frame(target.frame, list, width, height, slot));
  const wgpu::CommandEncoder encoder = gpu_.device().CreateCommandEncoder();
  encode_pass(encoder, target.offscreen.view, **pipe, target.frame, slot, list, clear);
  const wgpu::CommandBuffer commands = encoder.Finish();
  gpu_.queue().Submit(1, &commands);
  target.width = width;
  target.height = height;
  return PresentOutcome::Presented;
}

foundation::Result<void> Renderer::read_back(platform::SurfaceId id,
                                             foundation::Vector<std::uint8_t>& out) {
  for (SurfaceTarget& target : surfaces_) {
    if (target.id == id && target.headless && target.offscreen.texture != nullptr) {
      return read_texture(target.offscreen, out);
    }
  }
  return foundation::fail(Error{ErrorCode::InvalidState, "no headless frame for this surface"});
}

// --- Surfaces ---------------------------------------------------------------------

foundation::Result<Renderer::SurfaceTarget*> Renderer::surface_target(
    platform::SurfaceId id, const platform::NativeSurfaceHandle& native) {
  for (SurfaceTarget& target : surfaces_) {
    if (target.id == id) {
      if (!(target.native == native)) {
        // The shell recreated its native window surface.
        if (native.is_none() || target.headless) {
          return foundation::fail(Error{ErrorCode::InvalidArgument,
                                        "a surface cannot switch between headless and native"});
        }
        target.native = native;
        target.configured = false;
        target.surface = nullptr;
        UI_TRY(create_wgpu_surface(target));
      }
      return &target;
    }
  }
  SurfaceTarget& target = surfaces_.emplace_back();
  target.id = id;
  target.native = native;
  target.headless = native.is_none();
  if (target.headless) {
    return &target;
  }
  if (auto created = create_wgpu_surface(target); !created) {
    surfaces_.pop_back();
    return foundation::fail(std::move(created).error());
  }
  return &target;
}

foundation::Result<void> Renderer::create_wgpu_surface(SurfaceTarget& target) {
  wgpu::SurfaceDescriptor desc{};
  desc.label = "AxeUI surface";
  const platform::NativeSurfaceHandle& native = target.native;
#if defined(__EMSCRIPTEN__)
  wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
  if (native.kind() != platform::NativeSurfaceKind::WebCanvas) {
    return foundation::fail(Error{ErrorCode::Unsupported, "only canvas surfaces exist on the web"});
  }
  canvas.selector = native.as_web_canvas().selector;
  desc.nextInChain = &canvas;
#else
  wgpu::SurfaceSourceWindowsHWND win32{};
  wgpu::SurfaceSourceMetalLayer metal{};
  wgpu::SurfaceSourceXlibWindow xlib{};
  wgpu::SurfaceSourceWaylandSurface wayland{};
  wgpu::SurfaceSourceAndroidNativeWindow android{};
  switch (native.kind()) {
    case platform::NativeSurfaceKind::Win32:
      win32.hwnd = native.as_win32().hwnd;
      win32.hinstance = native.as_win32().hinstance;
      desc.nextInChain = &win32;
      break;
    case platform::NativeSurfaceKind::MetalLayer:
    case platform::NativeSurfaceKind::IosMetalLayer:
      metal.layer = native.as_metal_layer().layer;
      desc.nextInChain = &metal;
      break;
    case platform::NativeSurfaceKind::Xlib:
      xlib.display = native.as_xlib().display;
      xlib.window = native.as_xlib().window;
      desc.nextInChain = &xlib;
      break;
    case platform::NativeSurfaceKind::Wayland:
      wayland.display = native.as_wayland().display;
      wayland.surface = native.as_wayland().surface;
      desc.nextInChain = &wayland;
      break;
    case platform::NativeSurfaceKind::AndroidWindow:
      android.window = native.as_android_window().window;
      desc.nextInChain = &android;
      break;
    case platform::NativeSurfaceKind::None:
    case platform::NativeSurfaceKind::WebCanvas:
      return foundation::fail(
          Error{ErrorCode::Unsupported, "surface has no native handle usable on this platform"});
  }
#endif
  target.surface = gpu_.instance().CreateSurface(&desc);
  if (target.surface == nullptr) {
    return foundation::fail(Error{ErrorCode::GpuError, "creating the WebGPU surface failed"});
  }
  return {};
}

foundation::Result<void> Renderer::configure(SurfaceTarget& target, std::uint32_t width,
                                             std::uint32_t height) {
  wgpu::SurfaceCapabilities caps{};
  if (target.surface.GetCapabilities(gpu_.adapter(), &caps) != wgpu::Status::Success ||
      caps.formatCount == 0) {
    return foundation::fail(Error{ErrorCode::GpuError, "surface reports no capabilities"});
  }

  wgpu::SurfaceConfiguration config{};
  config.device = gpu_.device();
  config.usage = wgpu::TextureUsage::RenderAttachment;
  config.width = width;
  config.height = height;
  config.alphaMode = wgpu::CompositeAlphaMode::Auto;
  for (std::size_t i = 0; i < caps.alphaModeCount; ++i) {
    if (caps.alphaModes[i] == wgpu::CompositeAlphaMode::Opaque) {
      config.alphaMode = wgpu::CompositeAlphaMode::Opaque;
    }
  }
  config.presentMode = wgpu::PresentMode::Fifo;  // Always supported.
  if (options_.present == PresentPolicy::LowLatency) {
    for (std::size_t i = 0; i < caps.presentModeCount; ++i) {
      if (caps.presentModes[i] == wgpu::PresentMode::Mailbox) {
        config.presentMode = wgpu::PresentMode::Mailbox;
      }
    }
  }

  // 1. A native sRGB format: hardware encodes, blending is linear.
  wgpu::TextureFormat srgb_format = wgpu::TextureFormat::Undefined;
  for (std::size_t i = 0; i < caps.formatCount; ++i) {
    if (is_srgb(caps.formats[i])) {
      srgb_format = caps.formats[i];
      break;
    }
  }
  if (srgb_format != wgpu::TextureFormat::Undefined) {
    config.format = srgb_format;
    target.surface.Configure(&config);
    target.format = srgb_format;
    target.view_format = srgb_format;
    target.color_path = ColorOutputPath::SrgbFormat;
  } else {
    // 2. A non-sRGB format rendered through an sRGB view (browsers allow the
    //    sRGB variant as a view format).
    const wgpu::TextureFormat base = caps.formats[0];
    const wgpu::TextureFormat view = srgb_variant(base);
    config.format = base;
    bool view_ok = false;
    if (view != wgpu::TextureFormat::Undefined) {
      config.viewFormatCount = 1;
      config.viewFormats = &view;
#if defined(__EMSCRIPTEN__)
      target.surface.Configure(&config);
      view_ok = true;
#else
      ScopeResult scope;
      gpu_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
      target.surface.Configure(&config);
      const wgpu::Future future = gpu_.device().PopErrorScope(
          wgpu::CallbackMode::WaitAnyOnly,
          [](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView,
             ScopeResult* result) {
            result->error =
                status != wgpu::PopErrorScopeStatus::Success || type != wgpu::ErrorType::NoError;
          },
          &scope);
      UI_TRY(gpu_.wait(future));
      view_ok = !scope.error;
#endif
    }
    if (view_ok) {
      target.format = base;
      target.view_format = view;
      target.color_path = ColorOutputPath::SrgbViewFormat;
    } else {
      // 3. Encode in the shader.
      config.viewFormatCount = 0;
      config.viewFormats = nullptr;
      target.surface.Configure(&config);
      target.format = base;
      target.view_format = base;
      target.color_path = ColorOutputPath::ShaderEncode;
      logger_->log(LogLevel::Warning, ErrorCode::Unsupported,
                   "surface has no sRGB format or view; encoding sRGB in the shader");
    }
  }
  target.configured = true;
  target.reconfigure = false;
  target.width = width;
  target.height = height;
  return {};
}

foundation::Result<PresentOutcome> Renderer::render_surface(
    platform::SurfaceId id, const platform::NativeSurfaceHandle& native,
    const platform::SurfaceInfo& info, const DisplayList& list,
    foundation::LinearRgbaPremul clear) {
  const std::uint32_t width = to_pixels(info.physical_size.width);
  const std::uint32_t height = to_pixels(info.physical_size.height);
  if (width == 0 || height == 0) {
    return PresentOutcome::SkippedZeroSize;
  }
  if (auto device = ensure_device(); !device) {
    if (device.error().code == ErrorCode::DeviceLost) {
      return PresentOutcome::SkippedDeviceLost;
    }
    return foundation::fail(std::move(device).error());
  }
  textures_.flush();

  auto found = surface_target(id, native);
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  SurfaceTarget& target = **found;
  if (target.headless) {
    return render_headless(target, width, height, list, clear);
  }
  if (!target.configured || target.reconfigure || target.width != width ||
      target.height != height) {
    UI_TRY(configure(target, width, height));
  }

  wgpu::SurfaceTexture surface_texture{};
  target.surface.GetCurrentTexture(&surface_texture);
  switch (surface_texture.status) {
    case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
      break;
    case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
      target.reconfigure = true;  // Present this one, reconfigure next frame.
      break;
    case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
      return PresentOutcome::SkippedTimeout;
    case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
      UI_TRY(configure(target, width, height));
      return PresentOutcome::SkippedOutdated;
    case wgpu::SurfaceGetCurrentTextureStatus::Lost:
      target.configured = false;
      target.surface = nullptr;
      UI_TRY(create_wgpu_surface(target));
      UI_TRY(configure(target, width, height));
      return PresentOutcome::SkippedLost;
    case wgpu::SurfaceGetCurrentTextureStatus::Error:
    default:
      if (gpu_.lost()) {
        return PresentOutcome::SkippedDeviceLost;
      }
      target.reconfigure = true;
      return foundation::fail(Error{ErrorCode::GpuError, "acquiring the surface texture failed"});
  }

  wgpu::TextureViewDescriptor view_desc{};
  view_desc.format = target.view_format;
  view_desc.dimension = wgpu::TextureViewDimension::e2D;
  const wgpu::TextureView view = surface_texture.texture.CreateView(&view_desc);

  auto pipe = pipeline(target.view_format, target.color_path == ColorOutputPath::ShaderEncode);
  if (!pipe) {
    return foundation::fail(std::move(pipe).error());
  }
  std::uint32_t slot = 0;
  UI_TRY(prepare_frame(target.frame, list, width, height, slot));
  const wgpu::CommandEncoder encoder = gpu_.device().CreateCommandEncoder();
  encode_pass(encoder, view, **pipe, target.frame, slot, list, clear);
  const wgpu::CommandBuffer commands = encoder.Finish();
  gpu_.queue().Submit(1, &commands);
  target.surface.Present();
  return PresentOutcome::Presented;
}

void Renderer::remove_surface(platform::SurfaceId id) {
  const auto it = std::find_if(surfaces_.begin(), surfaces_.end(),
                               [id](const SurfaceTarget& t) { return t.id == id; });
  if (it == surfaces_.end()) {
    return;
  }
  if (it->configured) {
    it->surface.Unconfigure();
  }
  surfaces_.erase(it);
}

foundation::Result<ColorOutputPath> Renderer::color_output_path(platform::SurfaceId id) const {
  for (const SurfaceTarget& target : surfaces_) {
    if (target.id == id && target.configured) {
      return target.color_path;
    }
  }
  return foundation::fail(Error{ErrorCode::Unsupported, "surface is not configured"});
}

}  // namespace ui::render
