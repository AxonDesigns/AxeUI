#pragma once

#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "foundation/geometry.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "foundation/slot_map.h"
#include "foundation/timestamp.h"
#include "platform/event.h"
#include "platform/platform.h"
#include "platform/surface.h"

namespace ui::platform {

enum class Lifecycle : std::uint8_t { Running, Suspended };

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

// Everything the core knows about one surface.
struct SurfaceState {
  SurfaceState(Surface& s, foundation::MemoryResource& memory)
      : surface(&s),
        pointers(foundation::Allocator<PointerState>(memory)),
        preedit{foundation::String(foundation::Allocator<char>(memory)), -1, -1} {}

  Surface* surface;
  SurfaceInfo info;
  bool available = false;  // A drawable surface exists (cleared by SurfaceLost).
  bool focused = false;
  bool close_requested = false;
  bool needs_redraw = false;
  bool frame_requested = false;  // Asked the surface for a frame, not delivered yet.
  bool removal_pending = false;  // remove_surface() called during dispatch.
  bool has_cursor = false;
  CursorShape cursor = CursorShape::Default;
  bool has_ime_area = false;
  foundation::LogicalRect ime_area;
  foundation::Vector<PointerState> pointers;
  ImePreeditState preedit;
  bool has_frame_time = false;
  foundation::Timestamp last_frame_time;
  std::uint64_t frames_rendered = 0;
};

// Receives the frames the core decides to render. This is the seam between
// the core and the paint/render stages: today the FrameDriver (src/frame)
// implements it with an application paint callback; later the element tree
// plugs in here.
class FrameHandler {
 public:
  FrameHandler() = default;
  FrameHandler(const FrameHandler&) = delete;
  FrameHandler& operator=(const FrameHandler&) = delete;
  virtual ~FrameHandler() = default;

  // Produce and present one frame for `id`. Called from Core::frame() only
  // when the surface needs a redraw and can be rendered.
  virtual void render_surface(SurfaceId id, const SurfaceState& state,
                              foundation::Timestamp time) = 0;

  // The surface is gone; release anything held for it.
  virtual void surface_removed(SurfaceId /*id*/) {}
};

// The engine's side of the platform contract.
//
// Shells drive the core through exactly two entry points:
//   dispatch(event)  for every input, window and lifecycle event;
//   frame(id, time)  once per frame a surface asked for.
// Real shells, embedders and tests all use these two functions; there is no
// other way to inject input.
//
// The core never blocks and never loops. When a surface has something new to
// show, the core calls Surface::request_frame() (at most once until the frame
// arrives) and does the work inside frame(). It does not request frames while
// the application is suspended or for surfaces without a drawable area.
//
// Reentrancy: a FrameHandler or anything called during dispatch() / frame()
// may call back into the core. remove_surface() called then is deferred
// until the outermost dispatch() / frame() returns.
//
// Frame pipeline (stages land in later tasks; see docs/architecture.md):
//   input -> event dispatch -> state update -> rebuild dirty nodes -> layout
//   -> paint to display list -> render
class Core {
 public:
  Core(Platform& platform, foundation::MemoryResource& memory, const foundation::Logger& logger);
  ~Core();

  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  // --- Surfaces -------------------------------------------------------------

  // Registers a shell surface and requests its first frame. The surface
  // must stay alive until it has been removed.
  [[nodiscard]] foundation::Result<SurfaceId> add_surface(Surface& surface);

  // Unregisters a surface. During dispatch() / frame() the removal is
  // deferred: the surface stops receiving events and frames immediately, and
  // is detached when the outermost call returns.
  [[nodiscard]] foundation::Result<void> remove_surface(SurfaceId id);

  // --- Entry points for shells ---------------------------------------------

  void dispatch(const Event& event);
  void frame(SurfaceId id, foundation::Timestamp timestamp);

  // --- Services for the engine (and, later, widgets) ------------------------

  void set_frame_handler(FrameHandler* handler) noexcept { frame_handler_ = handler; }

  // Marks the surface as needing a new frame and asks for one if rendering
  // is currently possible.
  [[nodiscard]] foundation::Result<void> request_redraw(SurfaceId id);
  void request_redraw_all();

  // Forwarded to the surface only when the value changes.
  [[nodiscard]] foundation::Result<void> set_cursor(SurfaceId id, CursorShape cursor);
  [[nodiscard]] foundation::Result<void> set_ime_cursor_area(SurfaceId id,
                                                             const foundation::LogicalRect& area);

  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out);
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text);

  // --- Observable state -----------------------------------------------------

  [[nodiscard]] foundation::Result<const SurfaceState*> surface(SurfaceId id) const noexcept;
  [[nodiscard]] std::size_t surface_count() const noexcept { return surfaces_.size(); }

  // Calls fn(SurfaceId, const SurfaceState&) for every live surface.
  template <class Fn>
  void for_each_surface(Fn fn) const {
    surfaces_.for_each([&](foundation::Handle h, const SurfaceState& s) {
      if (!s.removal_pending) {
        fn(SurfaceId{h}, s);
      }
    });
  }

  [[nodiscard]] Lifecycle lifecycle() const noexcept { return lifecycle_; }
  [[nodiscard]] Modifiers modifiers() const noexcept { return modifiers_; }
  [[nodiscard]] bool in_dispatch() const noexcept { return dispatch_depth_ > 0; }
  [[nodiscard]] std::uint64_t frames_rendered() const noexcept { return frames_rendered_; }
  [[nodiscard]] std::uint64_t events_dispatched() const noexcept { return events_dispatched_; }

  [[nodiscard]] foundation::MemoryResource& memory() const noexcept { return *memory_; }
  [[nodiscard]] const foundation::Logger& logger() const noexcept { return *logger_; }

 private:
  // Marks a dispatch()/frame() scope for deferred removals.
  class DispatchScope;

  [[nodiscard]] foundation::Result<SurfaceState*> find_live(SurfaceId id, const char* what);
  [[nodiscard]] bool can_render(const SurfaceState& s) const noexcept;
  void request_frame_if_needed(SurfaceState& s);
  void mark_dirty(SurfaceState& s);
  void handle_pointer(SurfaceState& s, const Event& event);
  void handle_surface_info(SurfaceState& s, const Event& event);
  void handle_focus_lost(SurfaceState& s);
  void handle_text(SurfaceState& s, const Event& event);
  void detach(SurfaceId id);
  void flush_deferred_removals();

  Platform* platform_;
  foundation::MemoryResource* memory_;
  const foundation::Logger* logger_;
  FrameHandler* frame_handler_ = nullptr;

  foundation::SlotMap<SurfaceState> surfaces_;
  foundation::Vector<SurfaceId> pending_removals_;
  int dispatch_depth_ = 0;

  Lifecycle lifecycle_ = Lifecycle::Running;
  Modifiers modifiers_;
  std::uint64_t frames_rendered_ = 0;
  std::uint64_t events_dispatched_ = 0;
};

}  // namespace ui::platform
