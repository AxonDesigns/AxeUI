#include "platform/core.h"

#include <algorithm>

#include "foundation/utf8.h"

namespace ui::platform {

using foundation::ErrorCode;
using foundation::LogLevel;

Core::Core(Shell& shell, foundation::MemoryResource& memory, const foundation::Logger& logger)
    : shell_(&shell),
      memory_(&memory),
      logger_(&logger),
      pointers_(foundation::Allocator<PointerState>(memory)),
      preedit_{foundation::String(foundation::Allocator<char>(memory)), -1, -1} {
  surface_.info = shell.surface_info();
  surface_.available = !surface_.info.physical_size.is_empty();
  shell.attach_core(this);
  request_redraw();  // First frame.
}

Core::~Core() { shell_->attach_core(nullptr); }

void Core::dispatch(const Event& event) {
  ++events_dispatched_;
  last_event_time_ = event.time();

  switch (event.kind()) {
    case EventKind::PointerDown:
    case EventKind::PointerUp:
    case EventKind::PointerMove:
    case EventKind::PointerCancel:
      handle_pointer(event);
      break;
    case EventKind::Scroll:
      modifiers_ = event.scroll().modifiers;
      break;
    case EventKind::KeyDown:
    case EventKind::KeyUp:
      modifiers_ = event.key().modifiers;
      break;
    case EventKind::TextInput:
    case EventKind::ImePreedit:
    case EventKind::ImeCommit:
      handle_text(event);
      break;
    case EventKind::Resize:
    case EventKind::ScaleFactorChanged:
      handle_surface(event);
      break;
    case EventKind::FocusGained:
      focused_ = true;
      request_redraw();
      break;
    case EventKind::FocusLost:
      handle_focus_lost();
      break;
    case EventKind::Suspend:
      lifecycle_ = Lifecycle::Suspended;
      // The platform may never deliver a frame requested before suspension
      // (background tabs stop requestAnimationFrame), so forget it.
      frame_requested_ = false;
      break;
    case EventKind::Resume:
      lifecycle_ = Lifecycle::Running;
      request_redraw();
      break;
    case EventKind::SurfaceLost:
      surface_.available = false;
      frame_requested_ = false;
      break;
    case EventKind::CloseRequested:
      close_requested_ = true;
      break;
  }
}

void Core::frame(foundation::Timestamp timestamp) {
  frame_requested_ = false;
  if (has_frame_time_ && timestamp < last_frame_time_) {
    UI_LOGF(*logger_, LogLevel::Warning, ErrorCode::InvalidArgument,
            "frame timestamp went backwards (%lld ns -> %lld ns)",
            static_cast<long long>(last_frame_time_.nanoseconds),
            static_cast<long long>(timestamp.nanoseconds));
  }
  if (!can_render() || !needs_redraw_) {
    return;  // Spurious tick: cheap no-op.
  }
  render_frame(timestamp);
  // Anything that asked for a redraw during the frame (e.g. an animation)
  // gets the next one.
  request_frame_if_needed();
}

void Core::render_frame(foundation::Timestamp timestamp) {
  needs_redraw_ = false;
  last_frame_time_ = timestamp;
  has_frame_time_ = true;
  // Pipeline stages after event dispatch arrive in later tasks:
  // rebuild dirty nodes -> layout -> paint to display list -> render.
  ++frames_rendered_;
}

void Core::request_redraw() {
  needs_redraw_ = true;
  request_frame_if_needed();
}

bool Core::can_render() const noexcept {
  return lifecycle_ == Lifecycle::Running && surface_.available &&
         !surface_.info.physical_size.is_empty();
}

void Core::request_frame_if_needed() {
  if (needs_redraw_ && !frame_requested_ && can_render()) {
    frame_requested_ = true;
    shell_->request_frame();
  }
}

void Core::set_cursor(CursorShape cursor) {
  if (has_cursor_ && cursor == cursor_) {
    return;
  }
  has_cursor_ = true;
  cursor_ = cursor;
  shell_->set_cursor(cursor);
}

void Core::set_ime_cursor_area(const foundation::LogicalRect& area) {
  if (has_ime_area_ && area == ime_area_) {
    return;
  }
  has_ime_area_ = true;
  ime_area_ = area;
  shell_->set_ime_cursor_area(area);
}

foundation::Result<void> Core::clipboard_get_text(foundation::String& out) {
  UI_TRY(shell_->clipboard_get_text(out));
  if (!foundation::is_valid_utf8(out)) {
    out.clear();
    return foundation::fail(
        foundation::Error{ErrorCode::InvalidArgument, "clipboard text is not valid UTF-8"});
  }
  return {};
}

foundation::Result<void> Core::clipboard_set_text(std::string_view text) {
  if (!foundation::is_valid_utf8(text)) {
    return foundation::fail(
        foundation::Error{ErrorCode::InvalidArgument, "clipboard text is not valid UTF-8"});
  }
  return shell_->clipboard_set_text(text);
}

PointerState* Core::find_pointer(PointerId id) noexcept {
  for (PointerState& pointer : pointers_) {
    if (pointer.id == id) {
      return &pointer;
    }
  }
  return nullptr;
}

void Core::remove_pointer(PointerId id) noexcept {
  std::erase_if(pointers_, [id](const PointerState& p) { return p.id == id; });
}

void Core::handle_pointer(const Event& event) {
  const PointerEvent& e = event.pointer();
  modifiers_ = e.modifiers;

  // Touches exist only while in contact; mice and pens stay tracked (hover).
  const bool ends_contact = event.kind() == EventKind::PointerCancel ||
                            (event.kind() == EventKind::PointerUp && e.kind == PointerKind::Touch);
  if (ends_contact) {
    remove_pointer(e.id);
    return;
  }

  PointerState* pointer = find_pointer(e.id);
  if (pointer == nullptr) {
    pointers_.push_back(PointerState{e.id, e.kind, e.position, e.buttons});
    return;
  }
  pointer->kind = e.kind;
  pointer->position = e.position;
  pointer->buttons = e.buttons;
}

void Core::handle_surface(const Event& event) {
  const SurfaceInfo& info = event.surface();
  if (!SurfaceInfo::is_valid_size(info.physical_size)) {
    UI_LOGF(*logger_, LogLevel::Error, ErrorCode::InvalidArgument,
            "%s event with invalid surface size %gx%g ignored", event_kind_name(event.kind()),
            static_cast<double>(info.physical_size.width),
            static_cast<double>(info.physical_size.height));
    return;
  }
  surface_.info = info;
  if (event.kind() == EventKind::Resize) {
    // A size report means the shell has a (re)created surface.
    surface_.available = true;
  }
  request_redraw();
}

void Core::handle_focus_lost() {
  focused_ = false;
  // Key-up and pointer-up events for anything held will not arrive while the
  // window is unfocused, so drop that state now.
  modifiers_ = Modifiers{};
  std::erase_if(pointers_, [](const PointerState& p) { return p.kind == PointerKind::Touch; });
  for (PointerState& pointer : pointers_) {
    pointer.buttons = PointerButtons{};
  }
  preedit_.text.clear();
  preedit_.cursor_begin = -1;
  preedit_.cursor_end = -1;
  request_redraw();
}

void Core::handle_text(const Event& event) {
  const std::string_view text =
      event.kind() == EventKind::ImePreedit ? event.preedit().text : event.text();
  if (!foundation::is_valid_utf8(text)) {
    UI_LOGF(*logger_, LogLevel::Error, ErrorCode::InvalidArgument,
            "%s event with invalid UTF-8 ignored", event_kind_name(event.kind()));
    return;
  }

  if (event.kind() == EventKind::ImePreedit) {
    const ImePreeditEvent& e = event.preedit();
    const auto size = static_cast<std::int64_t>(text.size());
    const bool cursor_valid =
        (e.cursor_begin == -1 && e.cursor_end == -1) ||
        (e.cursor_begin >= 0 && e.cursor_begin <= e.cursor_end && e.cursor_end <= size);
    preedit_.text.assign(text);
    preedit_.cursor_begin = cursor_valid ? e.cursor_begin : -1;
    preedit_.cursor_end = cursor_valid ? e.cursor_end : -1;
    request_redraw();
    return;
  }

  if (event.kind() == EventKind::ImeCommit) {
    preedit_.text.clear();
    preedit_.cursor_begin = -1;
    preedit_.cursor_end = -1;
  }
  // Committed text is delivered to the focused widget once the node tree
  // exists (widgets task).
  request_redraw();
}

}  // namespace ui::platform
