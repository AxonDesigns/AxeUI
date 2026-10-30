#include "platform/core.h"

#include <algorithm>
#include <cinttypes>

#include "foundation/utf8.h"

namespace ui::platform {

using foundation::Error;
using foundation::ErrorCode;
using foundation::LogLevel;

class Core::DispatchScope {
 public:
  explicit DispatchScope(Core& core) noexcept : core_(core) { ++core_.dispatch_depth_; }
  ~DispatchScope() {
    if (--core_.dispatch_depth_ == 0) {
      core_.flush_deferred_removals();
    }
  }
  DispatchScope(const DispatchScope&) = delete;
  DispatchScope& operator=(const DispatchScope&) = delete;

 private:
  Core& core_;
};

Core::Core(Platform& platform, foundation::MemoryResource& memory, const foundation::Logger& logger)
    : platform_(&platform),
      memory_(&memory),
      logger_(&logger),
      surfaces_(memory, foundation::HandleType::Surface),
      pending_removals_(foundation::Allocator<SurfaceId>(memory)) {
  platform.attach_core(this);
}

Core::~Core() {
  UI_ASSERT(dispatch_depth_ == 0, "Core destroyed during dispatch");
  surfaces_.for_each([&](foundation::Handle h, SurfaceState& s) {
    s.surface->attach(nullptr, SurfaceId{});
    if (frame_handler_ != nullptr) {
      frame_handler_->surface_removed(SurfaceId{h});
    }
  });
  platform_->attach_core(nullptr);
}

// --- Surfaces ---------------------------------------------------------------

foundation::Result<SurfaceId> Core::add_surface(Surface& surface) {
  auto handle = surfaces_.emplace(surface, *memory_);
  if (!handle) {
    return foundation::fail(std::move(handle).error());
  }
  const SurfaceId id{*handle};
  SurfaceState& state = **surfaces_.get(*handle);
  state.info = surface.info();
  state.available = !state.info.physical_size.is_empty();
  surface.attach(this, id);
  mark_dirty(state);  // First frame.
  return id;
}

foundation::Result<void> Core::remove_surface(SurfaceId id) {
  auto found = find_live(id, "remove_surface");
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  if (dispatch_depth_ > 0) {
    (*found)->removal_pending = true;
    pending_removals_.push_back(id);
    return {};
  }
  detach(id);
  return {};
}

void Core::detach(SurfaceId id) {
  auto removed = surfaces_.remove(id.handle);
  UI_ASSERT(removed.has_value(), "detaching an unknown surface");
  removed->surface->attach(nullptr, SurfaceId{});
  if (frame_handler_ != nullptr) {
    frame_handler_->surface_removed(id);
  }
}

void Core::flush_deferred_removals() {
  // Detaching may run handler code that requests more removals; those go
  // straight through since the dispatch depth is already zero.
  while (!pending_removals_.empty()) {
    const SurfaceId id = pending_removals_.back();
    pending_removals_.pop_back();
    detach(id);
  }
}

foundation::Result<SurfaceState*> Core::find_live(SurfaceId id, const char* what) {
  auto found = surfaces_.get(id.handle);
  if (!found) {
    UI_LOGF(*logger_, LogLevel::Error, found.error().code, "Core::%s: %s (surface 0x%" PRIx64 ")",
            what, found.error().message, id.bits());
    return foundation::fail(std::move(found).error());
  }
  if ((*found)->removal_pending) {
    return foundation::fail(Error{ErrorCode::StaleHandle, "surface is being removed"});
  }
  return *found;
}

foundation::Result<const SurfaceState*> Core::surface(SurfaceId id) const noexcept {
  auto found = surfaces_.get(id.handle);
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  return *found;
}

// --- Entry points -------------------------------------------------------------

void Core::dispatch(const Event& event) {
  const DispatchScope scope(*this);
  ++events_dispatched_;

  if (event.is_app_wide()) {
    if (event.kind() == EventKind::Suspend) {
      lifecycle_ = Lifecycle::Suspended;
      // The platform may never deliver frames requested before suspension
      // (background tabs stop requestAnimationFrame), so forget them.
      surfaces_.for_each([](foundation::Handle, SurfaceState& s) { s.frame_requested = false; });
    } else {
      lifecycle_ = Lifecycle::Running;
      request_redraw_all();
    }
    return;
  }

  auto found = find_live(event.surface(), event_kind_name(event.kind()));
  if (!found) {
    return;  // Reported by find_live.
  }
  SurfaceState& s = **found;

  switch (event.kind()) {
    case EventKind::PointerDown:
    case EventKind::PointerUp:
    case EventKind::PointerMove:
    case EventKind::PointerCancel:
      handle_pointer(s, event);
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
      handle_text(s, event);
      break;
    case EventKind::Resize:
    case EventKind::ScaleFactorChanged:
      handle_surface_info(s, event);
      break;
    case EventKind::FocusGained:
      s.focused = true;
      mark_dirty(s);
      break;
    case EventKind::FocusLost:
      handle_focus_lost(s);
      break;
    case EventKind::SurfaceLost:
      s.available = false;
      s.frame_requested = false;
      break;
    case EventKind::CloseRequested:
      s.close_requested = true;
      break;
    case EventKind::Suspend:
    case EventKind::Resume:
      break;  // Handled above.
  }
}

void Core::frame(SurfaceId id, foundation::Timestamp timestamp) {
  const DispatchScope scope(*this);
  auto found = find_live(id, "frame");
  if (!found) {
    return;
  }
  SurfaceState& s = **found;
  s.frame_requested = false;
  if (s.has_frame_time && timestamp < s.last_frame_time) {
    UI_LOGF(*logger_, LogLevel::Warning, ErrorCode::InvalidArgument,
            "frame timestamp went backwards (%lld ns -> %lld ns)",
            static_cast<long long>(s.last_frame_time.nanoseconds),
            static_cast<long long>(timestamp.nanoseconds));
  }
  if (!can_render(s) || !s.needs_redraw) {
    return;  // Spurious tick: cheap no-op.
  }
  s.needs_redraw = false;
  s.has_frame_time = true;
  s.last_frame_time = timestamp;
  ++s.frames_rendered;
  ++frames_rendered_;
  // Stages after event dispatch (rebuild dirty nodes -> layout -> paint ->
  // render) run behind the FrameHandler seam.
  if (frame_handler_ != nullptr) {
    frame_handler_->render_surface(id, s, timestamp);
  }
  // Whatever asked for a redraw during the frame (an animation, a handler)
  // gets the next one. Look the surface up again: the handler may have added
  // surfaces (moving slot storage) or removed this one.
  auto after = surfaces_.get(id.handle);
  if (after && !(*after)->removal_pending) {
    request_frame_if_needed(**after);
  }
}

// --- Services -----------------------------------------------------------------

foundation::Result<void> Core::request_redraw(SurfaceId id) {
  auto found = find_live(id, "request_redraw");
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  mark_dirty(**found);
  return {};
}

void Core::request_redraw_all() {
  surfaces_.for_each([&](foundation::Handle, SurfaceState& s) {
    if (!s.removal_pending) {
      mark_dirty(s);
    }
  });
}

void Core::mark_dirty(SurfaceState& s) {
  s.needs_redraw = true;
  request_frame_if_needed(s);
}

bool Core::can_render(const SurfaceState& s) const noexcept {
  return lifecycle_ == Lifecycle::Running && s.available && !s.info.physical_size.is_empty();
}

void Core::request_frame_if_needed(SurfaceState& s) {
  if (s.needs_redraw && !s.frame_requested && can_render(s)) {
    s.frame_requested = true;
    s.surface->request_frame();
  }
}

foundation::Result<void> Core::set_cursor(SurfaceId id, CursorShape cursor) {
  auto found = find_live(id, "set_cursor");
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  SurfaceState& s = **found;
  if (!s.has_cursor || cursor != s.cursor) {
    s.has_cursor = true;
    s.cursor = cursor;
    s.surface->set_cursor(cursor);
  }
  return {};
}

foundation::Result<void> Core::set_ime_cursor_area(SurfaceId id,
                                                   const foundation::LogicalRect& area) {
  auto found = find_live(id, "set_ime_cursor_area");
  if (!found) {
    return foundation::fail(std::move(found).error());
  }
  SurfaceState& s = **found;
  if (!s.has_ime_area || area != s.ime_area) {
    s.has_ime_area = true;
    s.ime_area = area;
    s.surface->set_ime_cursor_area(area);
  }
  return {};
}

foundation::Result<void> Core::clipboard_get_text(foundation::String& out) {
  UI_TRY(platform_->clipboard_get_text(out));
  if (!foundation::is_valid_utf8(out)) {
    out.clear();
    return foundation::fail(Error{ErrorCode::InvalidArgument, "clipboard text is not valid UTF-8"});
  }
  return {};
}

foundation::Result<void> Core::clipboard_set_text(std::string_view text) {
  if (!foundation::is_valid_utf8(text)) {
    return foundation::fail(Error{ErrorCode::InvalidArgument, "clipboard text is not valid UTF-8"});
  }
  return platform_->clipboard_set_text(text);
}

// --- Event handlers -------------------------------------------------------------

void Core::handle_pointer(SurfaceState& s, const Event& event) {
  const PointerEvent& e = event.pointer();
  modifiers_ = e.modifiers;
  auto same_id = [&](const PointerState& p) { return p.id == e.id; };

  // Touches exist only while in contact; mice and pens stay tracked (hover).
  const bool ends_contact = event.kind() == EventKind::PointerCancel ||
                            (event.kind() == EventKind::PointerUp && e.kind == PointerKind::Touch);
  if (ends_contact) {
    std::erase_if(s.pointers, same_id);
    return;
  }
  const auto it = std::find_if(s.pointers.begin(), s.pointers.end(), same_id);
  if (it == s.pointers.end()) {
    s.pointers.push_back(PointerState{e.id, e.kind, e.position, e.buttons});
    return;
  }
  it->kind = e.kind;
  it->position = e.position;
  it->buttons = e.buttons;
}

void Core::handle_surface_info(SurfaceState& s, const Event& event) {
  const SurfaceInfo& info = event.surface_info();
  if (!SurfaceInfo::is_valid_size(info.physical_size)) {
    UI_LOGF(*logger_, LogLevel::Error, ErrorCode::InvalidArgument,
            "%s event with invalid surface size %gx%g ignored", event_kind_name(event.kind()),
            static_cast<double>(info.physical_size.width),
            static_cast<double>(info.physical_size.height));
    return;
  }
  s.info = info;
  if (event.kind() == EventKind::Resize) {
    s.available = true;  // A size report means the shell has a (re)created surface.
  }
  mark_dirty(s);
}

void Core::handle_focus_lost(SurfaceState& s) {
  s.focused = false;
  // Key-up and pointer-up events for anything held will not arrive while the
  // window is unfocused, so drop that state now.
  modifiers_ = Modifiers{};
  std::erase_if(s.pointers, [](const PointerState& p) { return p.kind == PointerKind::Touch; });
  for (PointerState& pointer : s.pointers) {
    pointer.buttons = PointerButtons{};
  }
  s.preedit.text.clear();
  s.preedit.cursor_begin = -1;
  s.preedit.cursor_end = -1;
  mark_dirty(s);
}

void Core::handle_text(SurfaceState& s, const Event& event) {
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
    s.preedit.text.assign(text);
    s.preedit.cursor_begin = cursor_valid ? e.cursor_begin : -1;
    s.preedit.cursor_end = cursor_valid ? e.cursor_end : -1;
    mark_dirty(s);
    return;
  }

  if (event.kind() == EventKind::ImeCommit) {
    s.preedit.text.clear();
    s.preedit.cursor_begin = -1;
    s.preedit.cursor_end = -1;
  }
  // Committed text is delivered to the focused element once the element
  // tree exists.
  mark_dirty(s);
}

}  // namespace ui::platform
