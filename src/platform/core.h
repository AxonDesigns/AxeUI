#pragma once

#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "foundation/geometry.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "foundation/timestamp.h"
#include "platform/event.h"
#include "platform/shell.h"

namespace ui::platform {

enum class Lifecycle : std::uint8_t { Running, Suspended };

struct SurfaceState {
  SurfaceInfo info;
  bool available = false;  // A drawable surface exists (cleared by SurfaceLost).
};

struct PointerState {
  PointerId id;
  PointerKind kind = PointerKind::Mouse;
  foundation::LogicalVec2 position;
  PointerButtons buttons;
};

struct ImePreeditState {
  foundation::String text;
  std::int32_t cursor_begin = -1;
  std::int32_t cursor_end = -1;
};

// The engine's side of the platform contract.
//
// Shells drive the core through exactly two entry points:
//   dispatch(event)  for every input, window and lifecycle event;
//   frame(time)      once per frame the core asked for.
// Real shells, embedders and tests all use these same two functions; there is
// no other way to inject input.
//
// The core never blocks and never loops. When it has work to show, it calls
// Shell::request_frame() (at most once until the frame arrives) and does the
// work inside frame(). It does not request frames while suspended or without
// a surface.
//
// Frame pipeline (stages land in later tasks; see docs/architecture.md):
//   input -> event dispatch -> state update -> rebuild dirty nodes -> layout
//   -> paint to display list -> render
class Core {
 public:
  Core(Shell& shell, foundation::MemoryResource& memory, const foundation::Logger& logger);
  ~Core();

  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  // --- Entry points for shells ---------------------------------------------

  void dispatch(const Event& event);
  void frame(foundation::Timestamp timestamp);

  // --- Services for the engine (and, later, widgets) ------------------------

  // Marks the UI as needing a new frame and asks the shell for one if
  // rendering is currently possible.
  void request_redraw();

  // Forwarded to the shell only when the value changes.
  void set_cursor(CursorShape cursor);
  void set_ime_cursor_area(const foundation::LogicalRect& area);

  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out);
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text);

  // --- Observable state -----------------------------------------------------

  [[nodiscard]] const SurfaceState& surface() const noexcept { return surface_; }
  [[nodiscard]] Lifecycle lifecycle() const noexcept { return lifecycle_; }
  [[nodiscard]] bool focused() const noexcept { return focused_; }
  [[nodiscard]] bool close_requested() const noexcept { return close_requested_; }
  [[nodiscard]] Modifiers modifiers() const noexcept { return modifiers_; }
  [[nodiscard]] const foundation::Vector<PointerState>& pointers() const noexcept {
    return pointers_;
  }
  [[nodiscard]] const ImePreeditState& ime_preedit() const noexcept { return preedit_; }

  // True while a frame has been requested from the shell and not yet run.
  [[nodiscard]] bool frame_requested() const noexcept { return frame_requested_; }
  [[nodiscard]] bool needs_redraw() const noexcept { return needs_redraw_; }
  [[nodiscard]] std::uint64_t frames_rendered() const noexcept { return frames_rendered_; }
  [[nodiscard]] std::uint64_t events_dispatched() const noexcept { return events_dispatched_; }

  [[nodiscard]] foundation::MemoryResource& memory() const noexcept { return *memory_; }
  [[nodiscard]] const foundation::Logger& logger() const noexcept { return *logger_; }

 private:
  [[nodiscard]] bool can_render() const noexcept;
  void request_frame_if_needed();
  void handle_pointer(const Event& event);
  void handle_surface(const Event& event);
  void handle_focus_lost();
  void handle_text(const Event& event);
  void render_frame(foundation::Timestamp timestamp);
  PointerState* find_pointer(PointerId id) noexcept;
  void remove_pointer(PointerId id) noexcept;

  Shell* shell_;
  foundation::MemoryResource* memory_;
  const foundation::Logger* logger_;

  SurfaceState surface_;
  Lifecycle lifecycle_ = Lifecycle::Running;
  bool focused_ = false;
  bool close_requested_ = false;
  Modifiers modifiers_;
  foundation::Vector<PointerState> pointers_;
  ImePreeditState preedit_;

  bool needs_redraw_ = false;
  bool frame_requested_ = false;
  bool has_cursor_ = false;
  CursorShape cursor_ = CursorShape::Default;
  bool has_ime_area_ = false;
  foundation::LogicalRect ime_area_;

  foundation::Timestamp last_event_time_;
  foundation::Timestamp last_frame_time_;
  bool has_frame_time_ = false;
  std::uint64_t frames_rendered_ = 0;
  std::uint64_t events_dispatched_ = 0;
};

}  // namespace ui::platform
