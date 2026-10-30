#include "platform/host_shell.h"

#include "platform/core.h"

namespace ui::platform {

using foundation::Error;
using foundation::ErrorCode;

HostShell::HostShell(const SurfaceInfo& surface, const HostShellCallbacks& callbacks) noexcept
    : surface_(surface), callbacks_(callbacks) {}

HostShell::~HostShell() {
  UI_ASSERT(core_ == nullptr, "HostShell destroyed while a Core is still attached");
}

void HostShell::push_event(const Event& event) {
  UI_ASSERT(core_ != nullptr, "HostShell::push_event without an attached Core");
  if ((event.kind() == EventKind::Resize || event.kind() == EventKind::ScaleFactorChanged) &&
      SurfaceInfo::is_valid_size(event.surface().physical_size)) {
    surface_ = event.surface();
  }
  core_->dispatch(event);
}

void HostShell::tick(foundation::Timestamp timestamp) {
  UI_ASSERT(core_ != nullptr, "HostShell::tick without an attached Core");
  core_->frame(timestamp);
}

foundation::Result<void> HostShell::set_surface(foundation::PhysicalSize size, float scale,
                                                foundation::Timestamp time) {
  if (!foundation::ScaleFactor::is_valid(scale)) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "invalid scale factor"});
  }
  if (!SurfaceInfo::is_valid_size(size)) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "invalid surface size"});
  }
  const SurfaceInfo next{size, foundation::ScaleFactor{scale}};
  const bool scale_changed = next.scale != surface_.scale;
  const bool size_changed = next.physical_size != surface_.physical_size;
  if (scale_changed) {
    push_event(Event::scale_factor_changed(time, next));
  }
  if (size_changed) {
    push_event(Event::resize(time, next));
  }
  return {};
}

void HostShell::request_frame() {
  if (callbacks_.request_frame != nullptr) {
    callbacks_.request_frame(callbacks_.user_data);
  }
}

void HostShell::set_cursor(CursorShape cursor) {
  if (callbacks_.set_cursor != nullptr) {
    callbacks_.set_cursor(callbacks_.user_data, cursor);
  }
}

void HostShell::set_ime_cursor_area(const foundation::LogicalRect& area) {
  if (callbacks_.set_ime_cursor_area != nullptr) {
    callbacks_.set_ime_cursor_area(callbacks_.user_data, area);
  }
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

void HostShell::attach_core(Core* core) noexcept {
  UI_ASSERT(core == nullptr || core_ == nullptr, "HostShell is already attached to a Core");
  core_ = core;
}

}  // namespace ui::platform
