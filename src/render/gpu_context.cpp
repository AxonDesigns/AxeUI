#include "render/gpu_context.h"

#include <cstdint>
#include <string_view>

namespace ui::render {

using foundation::Error;
using foundation::ErrorCode;
using foundation::LogLevel;

namespace {

std::string_view view(wgpu::StringView s) noexcept {
  if (s.data == nullptr) {
    return {};
  }
  return s.length == wgpu::kStrlen ? std::string_view(s.data) : std::string_view(s.data, s.length);
}

// Result of the device request, filled in by a non-capturing callback (Dawn
// would heap-allocate a capturing lambda).
struct DeviceRequest {
  wgpu::Device device;
  bool done = false;
};

}  // namespace

GpuContext::~GpuContext() {
  shutting_down_ = true;
  if (device_ != nullptr) {
    device_.Destroy();  // Reports DeviceLostReason::Destroyed, ignored below.
    process_events();
  }
}

foundation::Result<void> GpuContext::initialize(const GpuOptions& options) {
  options_ = options;
  wgpu::InstanceDescriptor instance_desc{};
#if !defined(__EMSCRIPTEN__)
  static constexpr wgpu::InstanceFeatureName kFeatures[] = {
      wgpu::InstanceFeatureName::TimedWaitAny};
  instance_desc.requiredFeatureCount = 1;
  instance_desc.requiredFeatures = kFeatures;
#endif
  instance_ = wgpu::CreateInstance(&instance_desc);
  if (instance_ == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "WebGPU is not available"});
  }
  return request_adapter_and_device();
}

foundation::Result<void> GpuContext::recreate_device() {
  if (instance_ == nullptr) {
    return foundation::fail(Error{ErrorCode::InvalidState, "GPU context was never initialized"});
  }
  queue_ = nullptr;
  device_ = nullptr;
  adapter_ = nullptr;
  return request_adapter_and_device();
}

foundation::Result<void> GpuContext::request_adapter_and_device() {
#if defined(__EMSCRIPTEN__)
  // Asynchronous path for browsers is wired up together with the web shell.
  return foundation::fail(
      Error{ErrorCode::Unavailable, "asynchronous WebGPU initialization is not implemented yet"});
#else
  wgpu::RequestAdapterOptions adapter_options{};
  adapter_options.powerPreference = options_.power == PowerPreference::HighPerformance
                                        ? wgpu::PowerPreference::HighPerformance
                                        : wgpu::PowerPreference::LowPower;
  adapter_options.backendType = options_.backend;
  adapter_options.forceFallbackAdapter = options_.force_software;

  const wgpu::Future adapter_future = instance_.RequestAdapter(
      &adapter_options, wgpu::CallbackMode::WaitAnyOnly,
      [](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message,
         GpuContext* self) {
        if (status != wgpu::RequestAdapterStatus::Success) {
          const std::string_view text = view(message);
          UI_LOGF(*self->logger_, LogLevel::Error, ErrorCode::Unavailable,
                  "no WebGPU adapter: %.*s", static_cast<int>(text.size()), text.data());
          return;
        }
        self->adapter_ = std::move(adapter);
      },
      this);
  UI_TRY(wait(adapter_future));
  if (adapter_ == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "no suitable WebGPU adapter"});
  }
  wgpu::AdapterInfo info{};
  if (adapter_.GetInfo(&info) == wgpu::Status::Success) {
    backend_ = info.backendType;
    const std::string_view desc_view = view(info.description);
    const std::string_view device_view = view(info.device);
    adapter_name_.assign(device_view.substr(0, std::min<std::size_t>(device_view.size(), 127)));
    adapter_description_.assign(desc_view.substr(0, std::min<std::size_t>(desc_view.size(), 255)));
    const std::string_view device_name = view(info.device);
    const std::string_view description = view(info.description);
    UI_LOGF(*logger_, LogLevel::Info, ErrorCode::Ok, "GPU adapter: %.*s (%.*s)",
            static_cast<int>(device_name.size()), device_name.data(),
            static_cast<int>(description.size()), description.data());
  }

  wgpu::DeviceDescriptor device_desc{};
  device_desc.label = "AxeUI device";
  // No required features and default limits: only what the renderer needs.
  device_desc.SetDeviceLostCallback(
      wgpu::CallbackMode::AllowSpontaneous,
      [](const wgpu::Device& device, wgpu::DeviceLostReason reason, wgpu::StringView message,
         GpuContext* self) { on_device_lost(device, reason, message, self); },
      this);
  device_desc.SetUncapturedErrorCallback(
      [](const wgpu::Device& device, wgpu::ErrorType type, wgpu::StringView message,
         GpuContext* self) { on_uncaptured_error(device, type, message, self); },
      this);

  DeviceRequest device_request;
  const wgpu::Future device_future = adapter_.RequestDevice(
      &device_desc, wgpu::CallbackMode::WaitAnyOnly,
      [](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message,
         DeviceRequest* request) {
        request->done = true;
        if (status == wgpu::RequestDeviceStatus::Success) {
          request->device = std::move(device);
        } else {
          static_cast<void>(message);
        }
      },
      &device_request);
  UI_TRY(wait(device_future));
  if (device_request.device == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "WebGPU device creation failed"});
  }
  device_ = std::move(device_request.device);
  queue_ = device_.GetQueue();
  device_.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message,
                                GpuContext* self) { on_dawn_log(type, message, self); },
                             this);
  lost_.store(false, std::memory_order_release);
  ++generation_;
  return {};
#endif
}

foundation::Result<void> GpuContext::wait(wgpu::Future future) {
#if defined(__EMSCRIPTEN__)
  static_cast<void>(future);
  return foundation::fail(
      Error{ErrorCode::Unsupported, "blocking waits are not available on the web"});
#else
  const wgpu::WaitStatus status = instance_.WaitAny(future, UINT64_MAX);
  if (status != wgpu::WaitStatus::Success) {
    return foundation::fail(Error{ErrorCode::GpuError, "waiting for a WebGPU operation failed"});
  }
  return {};
#endif
}

void GpuContext::process_events() {
  if (instance_ != nullptr) {
    instance_.ProcessEvents();
  }
}

foundation::Result<void> GpuContext::force_device_loss() {
#if defined(__EMSCRIPTEN__)
  return foundation::fail(Error{ErrorCode::Unsupported, "forced device loss is native-only"});
#else
  if (device_ == nullptr) {
    return foundation::fail(Error{ErrorCode::InvalidState, "no device"});
  }
  device_.ForceLoss(wgpu::DeviceLostReason::Unknown, "simulated device loss (test)");
  process_events();
  return {};
#endif
}

void GpuContext::on_device_lost(const wgpu::Device& /*device*/, wgpu::DeviceLostReason reason,
                                wgpu::StringView message, GpuContext* self) {
  if (self->shutting_down_ || reason == wgpu::DeviceLostReason::Destroyed ||
      reason == wgpu::DeviceLostReason::CallbackCancelled) {
    return;  // Our own teardown, not a loss to recover from.
  }
  self->lost_.store(true, std::memory_order_release);
  const std::string_view text = view(message);
  UI_LOGF(*self->logger_, LogLevel::Error, ErrorCode::DeviceLost, "GPU device lost: %.*s",
          static_cast<int>(text.size()), text.data());
}

void GpuContext::on_uncaptured_error(const wgpu::Device& /*device*/, wgpu::ErrorType type,
                                     wgpu::StringView message, GpuContext* self) {
  const std::string_view text = view(message);
  UI_LOGF(*self->logger_, LogLevel::Error, ErrorCode::GpuError, "WebGPU error (type %u): %.*s",
          static_cast<unsigned>(type), static_cast<int>(text.size()), text.data());
}

void GpuContext::on_dawn_log(wgpu::LoggingType type, wgpu::StringView message, GpuContext* self) {
  LogLevel level = LogLevel::Debug;
  if (type == wgpu::LoggingType::Warning) {
    level = LogLevel::Warning;
  } else if (type == wgpu::LoggingType::Error) {
    level = LogLevel::Error;
  } else if (type == wgpu::LoggingType::Info) {
    level = LogLevel::Info;
  }
  const std::string_view text = view(message);
  UI_LOGF(*self->logger_, level, ErrorCode::Ok, "Dawn: %.*s", static_cast<int>(text.size()),
          text.data());
}

}  // namespace ui::render
