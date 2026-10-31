#include "frame/frame_driver.h"

#include <algorithm>
#include <cinttypes>

namespace ui::frame {

using foundation::ErrorCode;
using foundation::LogLevel;
using render::PresentOutcome;

FrameDriver::FrameDriver(platform::Core& core, render::Renderer& renderer,
                         foundation::MemoryResource& memory, PaintCallback paint, void* user_data)
    : core_(&core),
      renderer_(&renderer),
      memory_(&memory),
      paint_(paint),
      user_data_(user_data),
      surfaces_(foundation::Allocator<foundation::UniquePtr<SurfaceFrame>>(memory)) {
  core.set_frame_handler(this);
}

FrameDriver::~FrameDriver() { core_->set_frame_handler(nullptr); }

FrameDriver::SurfaceFrame* FrameDriver::find_or_create(platform::SurfaceId id) {
  for (const auto& frame : surfaces_) {
    if (frame->id == id) {
      return frame.get();
    }
  }
  auto created = foundation::allocate_unique<SurfaceFrame>(*memory_, id, *memory_);
  if (!created) {
    return nullptr;
  }
  surfaces_.push_back(std::move(created).value());
  return surfaces_.back().get();
}

void FrameDriver::render_surface(platform::SurfaceId id, const platform::SurfaceState& state,
                                 foundation::Timestamp /*time*/) {
  SurfaceFrame* frame = find_or_create(id);
  if (frame == nullptr) {
    core_->logger().log(LogLevel::Error, ErrorCode::OutOfMemory, "FrameDriver: out of memory");
    return;
  }

  frame->builder.reset(state.info.physical_size, state.info.scale);
  if (paint_ != nullptr) {
    paint_(user_data_, id, frame->builder);
  }

  auto outcome = renderer_->render_surface(id, state.surface->native_handle(), state.info,
                                           frame->list, clear_);
  if (!outcome) {
    core_->logger().report(outcome.error());
    return;
  }
  last_outcome_ = *outcome;
  switch (*outcome) {
    case PresentOutcome::Presented:
      ++presented_;
      break;
    case PresentOutcome::SkippedZeroSize:
      break;  // Nothing visible; the next resize brings a frame.
    case PresentOutcome::SkippedTimeout:
    case PresentOutcome::SkippedOutdated:
    case PresentOutcome::SkippedLost:
    case PresentOutcome::SkippedDeviceLost:
      ++skipped_;
      UI_LOGF(core_->logger(), LogLevel::Info, ErrorCode::Ok,
              "surface 0x%" PRIx64 ": frame skipped (%s), retrying", id.bits(),
              *outcome == PresentOutcome::SkippedTimeout    ? "timeout"
              : *outcome == PresentOutcome::SkippedOutdated ? "outdated"
              : *outcome == PresentOutcome::SkippedLost     ? "surface lost"
                                                            : "device lost");
      static_cast<void>(core_->request_redraw(id));  // Try again next frame.
      break;
  }
}

void FrameDriver::surface_removed(platform::SurfaceId id) {
  renderer_->remove_surface(id);
  std::erase_if(surfaces_, [id](const auto& frame) { return frame->id == id; });
}

}  // namespace ui::frame
