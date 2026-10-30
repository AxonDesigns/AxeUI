#pragma once

// Shared helpers for renderer tests. Tests that need a GPU call
// make_renderer(); when no adapter exists (e.g. CI machines without GPU
// access) it returns nullptr and the test is skipped with a message.

#include <cstdlib>
#include <string_view>

#include "display_list/builder.h"
#include "render/renderer.h"
#include "support/test_support.h"

namespace ui::test {

// Creates a renderer for tests, or returns nullptr (and says why) when this
// machine has no usable adapter.
//
// Prefers a software adapter so results do not depend on the GPU:
// - Linux: CTest points the Vulkan loader at Mesa lavapipe (the golden
//   reference), so the Vulkan adapter is lavapipe;
// - Windows: Dawn's fallback adapter (WARP, part of every Windows 10+);
// - elsewhere (macOS): the default adapter, since Dawn has no software
//   adapter there.
inline foundation::UniquePtr<render::Renderer> make_renderer(foundation::MemoryResource& memory,
                                                             const foundation::Logger& logger) {
  render::RendererOptions options;
#if defined(__linux__)
  options.gpu.backend = wgpu::BackendType::Vulkan;
#elif defined(_WIN32)
  options.gpu.force_software = true;
#endif
  auto renderer = render::Renderer::create(memory, logger, options);
  if (!renderer) {
    MESSAGE("skipping: no WebGPU adapter (" << std::string_view(renderer.error().message) << ")");
    return foundation::UniquePtr<render::Renderer>(
        nullptr, foundation::ObjectDeleter<render::Renderer>(memory));
  }
  return std::move(renderer).value();
}

// Prints every diagnostic the renderer logged and fails on errors. Warnings
// are allowed: drivers and backends differ in what they report (Dawn's Metal
// backend on CI's paravirtual GPU, for example), but a GPU error never passes.
inline void check_no_errors(const LogCapture& log) {
  for (const CapturedLog& record : log.records()) {
    MESSAGE("log level " << static_cast<int>(record.level) << " code "
                         << static_cast<int>(record.code) << ": " << record.message);
    CHECK(record.level < foundation::LogLevel::Error);
  }
}

// True when the adapter is the golden-image reference (Mesa lavapipe).
inline bool is_reference_adapter(render::Renderer& renderer) {
  const std::string_view name = renderer.gpu().adapter_name();
  return name.find("llvmpipe") != std::string_view::npos;
}

}  // namespace ui::test
