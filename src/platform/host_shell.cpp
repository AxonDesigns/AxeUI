#include "platform/host_shell.h"

#include "platform/core.h"

namespace ui::platform {

using foundation::Error;
using foundation::ErrorCode;

// --- HostShell ------------------------------------------------------------------

HostShell::~HostShell() {
  UI_ASSERT(core_ == nullptr, "HostShell destroyed while a Core is still attached");
}

void HostShell::push_event(const Event& event) {
  UI_ASSERT(core_ != nullptr, "HostShell::push_event without an attached Core");
  core_->dispatch(event);
}

void HostShell::tick(SurfaceId id, foundation::Timestamp timestamp) {
  UI_ASSERT(core_ != nullptr, "HostShell::tick without an attached Core");
  core_->frame(id, timestamp);
}

foundation::Result<void> HostShell::clipboard_get_text(foundation::String& out) {
  if (callbacks_.clipboard_get_text == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "host provides no clipboard"});
  }
  return callbacks_.clipboard_get_text(callbacks_.user_data, out);
}

foundation::Result<void> HostShell::clipboard_set_text(std::string_view text) {
  if (callbacks_.clipboard_set_text == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "host provides no clipboard"});
  }
  return callbacks_.clipboard_set_text(callbacks_.user_data, text);
}

void HostShell::wake() noexcept {
  if (callbacks_.wake != nullptr) {
    callbacks_.wake(callbacks_.user_data);
  }
}

void HostShell::attach_core(Core* core) noexcept {
  UI_ASSERT(core == nullptr || core_ == nullptr, "HostShell is already attached to a Core");
  core_ = core;
}

// --- HostSurface ----------------------------------------------------------------

HostSurface::~HostSurface() {
  UI_ASSERT(core_ == nullptr, "HostSurface destroyed while still registered with a Core");
}

foundation::Result<void> HostSurface::set_info(foundation::PhysicalSize size, float scale,
                                               foundation::Timestamp time, float refresh_rate_hz) {
  if (core_ == nullptr) {
    return foundation::fail(Error{ErrorCode::InvalidState, "surface is not attached to a core"});
  }
  if (!foundation::ScaleFactor::is_valid(scale)) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "invalid scale factor"});
  }
  if (!SurfaceInfo::is_valid_size(size)) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "invalid surface size"});
  }
  const SurfaceInfo next{size, foundation::ScaleFactor{scale}, refresh_rate_hz};
  const bool scale_changed = next.scale != info_.scale;
  const bool size_changed = next.physical_size != info_.physical_size;
  info_ = next;
  // Keep a local copy: dispatch may lead to this surface being detached.
  Core* core = core_;
  const SurfaceId id = id_;
  if (scale_changed) {
    core->dispatch(Event::scale_factor_changed(id, time, next));
  }
  if (size_changed) {
    core->dispatch(Event::resize(id, time, next));
  }
  return {};
}

void HostSurface::request_frame() {
  if (callbacks_.request_frame != nullptr) {
    callbacks_.request_frame(callbacks_.user_data);
  }
}

void HostSurface::set_cursor(CursorShape cursor) {
  if (callbacks_.set_cursor != nullptr) {
    callbacks_.set_cursor(callbacks_.user_data, cursor);
  }
}

void HostSurface::set_ime_cursor_area(const foundation::LogicalRect& area) {
  if (callbacks_.set_ime_cursor_area != nullptr) {
    callbacks_.set_ime_cursor_area(callbacks_.user_data, area);
  }
}

void HostSurface::attach(Core* core, SurfaceId id) noexcept {
  core_ = core;
  id_ = id;
}

}  // namespace ui::platform
