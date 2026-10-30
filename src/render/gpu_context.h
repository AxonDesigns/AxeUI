#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <webgpu/webgpu_cpp.h>

#include "foundation/logging.h"
#include "foundation/result.h"

namespace ui::render {

// Small inline string (no allocation) for adapter names.
template <std::size_t N>
class FixedString {
 public:
  void assign(std::string_view text) noexcept {
    size_ = std::min(text.size(), N);
    std::copy_n(text.data(), size_, data_);
  }
  operator std::string_view() const noexcept { return {data_, size_}; }  // NOLINT

 private:
  char data_[N] = {};
  std::size_t size_ = 0;
};

enum class PowerPreference : std::uint8_t { LowPower, HighPerformance };

struct GpuOptions {
  PowerPreference power = PowerPreference::LowPower;
  // Pick a specific backend (tests force Vulkan on Linux); Undefined lets
  // the implementation choose.
  wgpu::BackendType backend = wgpu::BackendType::Undefined;
  // Ask for the implementation's fallback (software) adapter, where it has
  // one: WARP on Windows, SwiftShader in Dawn builds that bundle it. Mesa
  // lavapipe is not a Dawn fallback adapter; the golden reference selects it
  // through the Vulkan loader instead (tests/CMakeLists.txt).
  bool force_software = false;
};

// The process-wide WebGPU instance, adapter, device and queue, shared by
// every surface.
//
// Only default WebGPU limits and no optional features are requested, so the
// renderer runs on any conforming implementation, including browsers.
//
// Device loss: the lost callback only sets a flag (it may fire on another
// thread). The renderer checks lost() at the start of each frame and calls
// recreate_device(), after which generation() has advanced and every
// device-owned object must be rebuilt.
//
// Native builds create devices synchronously (WaitAny). Under Emscripten the
// requests complete from the browser event loop; initialize() /
// recreate_device() then return Unavailable until ready() turns true. The
// web shell drives that in part 3.
class GpuContext {
 public:
  explicit GpuContext(const foundation::Logger& logger) noexcept : logger_(&logger) {}
  ~GpuContext();

  GpuContext(const GpuContext&) = delete;
  GpuContext& operator=(const GpuContext&) = delete;

  [[nodiscard]] foundation::Result<void> initialize(const GpuOptions& options);
  [[nodiscard]] foundation::Result<void> recreate_device();

  [[nodiscard]] bool ready() const noexcept { return device_ != nullptr && !lost(); }
  [[nodiscard]] bool lost() const noexcept { return lost_.load(std::memory_order_acquire); }
  // Advances every time a new device is created.
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  // Delivers pending callbacks (errors, device loss, map completions).
  void process_events();

  // Test hook: makes the device lost, as a driver reset would. Native only
  // (uses Dawn's ForceLoss); returns Unsupported elsewhere.
  [[nodiscard]] foundation::Result<void> force_device_loss();

  // Blocks until `future` completes. Native only.
  [[nodiscard]] foundation::Result<void> wait(wgpu::Future future);

  [[nodiscard]] const wgpu::Instance& instance() const noexcept { return instance_; }
  [[nodiscard]] const wgpu::Adapter& adapter() const noexcept { return adapter_; }
  [[nodiscard]] const wgpu::Device& device() const noexcept { return device_; }
  [[nodiscard]] const wgpu::Queue& queue() const noexcept { return queue_; }
  [[nodiscard]] const foundation::Logger& logger() const noexcept { return *logger_; }
  // Backend of the current adapter (for diagnostics and tests).
  [[nodiscard]] wgpu::BackendType backend() const noexcept { return backend_; }
  // Adapter device name and driver description, e.g. "llvmpipe (LLVM 20.1.2,
  // 256 bits)" / "Mesa 25.2.8". Used to identify the golden reference.
  [[nodiscard]] std::string_view adapter_name() const noexcept { return adapter_name_; }
  [[nodiscard]] std::string_view adapter_description() const noexcept {
    return adapter_description_;
  }

 private:
  [[nodiscard]] foundation::Result<void> request_adapter_and_device();

  static void on_device_lost(const wgpu::Device& device, wgpu::DeviceLostReason reason,
                             wgpu::StringView message, GpuContext* self);
  static void on_uncaptured_error(const wgpu::Device& device, wgpu::ErrorType type,
                                  wgpu::StringView message, GpuContext* self);
#if !defined(__EMSCRIPTEN__)
  // Dawn's logging callback is a native-only extension.
  static void on_dawn_log(wgpu::LoggingType type, wgpu::StringView message, GpuContext* self);
#endif

  const foundation::Logger* logger_;
  GpuOptions options_;
  wgpu::Instance instance_;
  wgpu::Adapter adapter_;
  wgpu::Device device_;
  wgpu::Queue queue_;
  wgpu::BackendType backend_ = wgpu::BackendType::Undefined;
  FixedString<128> adapter_name_;
  FixedString<256> adapter_description_;
  std::atomic<bool> lost_{false};
  bool shutting_down_ = false;
  std::uint64_t generation_ = 0;
};

}  // namespace ui::render
