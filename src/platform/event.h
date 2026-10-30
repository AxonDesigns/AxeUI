#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "foundation/assertions.h"
#include "foundation/flags.h"
#include "foundation/geometry.h"
#include "foundation/timestamp.h"
#include "platform/keys.h"
#include "platform/surface.h"

namespace ui::platform {

// Events a shell sends to the core through Core::dispatch(). All positions
// and sizes are already in logical pixels; text is UTF-8.
//
// Text payloads are std::string_view into shell-owned memory and are valid
// only for the duration of the dispatch() call. The core copies what it keeps.

enum class EventKind : std::uint8_t {
  PointerDown,
  PointerUp,
  PointerMove,
  PointerCancel,
  Scroll,
  KeyDown,
  KeyUp,
  TextInput,
  ImePreedit,
  ImeCommit,
  Resize,
  ScaleFactorChanged,
  FocusGained,
  FocusLost,
  Suspend,
  Resume,
  SurfaceLost,
  CloseRequested,
};

[[nodiscard]] const char* event_kind_name(EventKind kind) noexcept;

enum class PointerKind : std::uint8_t { Mouse, Touch, Pen };

// Pointer buttons as single bits, so they double as a held-buttons set.
enum class PointerButton : std::uint16_t {
  None = 0,
  Primary = 1u << 0,    // Left mouse button, touch contact, pen tip.
  Secondary = 1u << 1,  // Right mouse button, pen barrel button.
  Middle = 1u << 2,
  Back = 1u << 3,
  Forward = 1u << 4,
  Eraser = 1u << 5,  // Pen eraser end.
};

using PointerButtons = foundation::Flags<PointerButton>;

// Identifies one pointer for the duration of its interaction (e.g. a touch
// from down to up). Shells choose ids; by convention the mouse is 0.
struct PointerId {
  std::uint64_t value = 0;
  friend constexpr bool operator==(PointerId, PointerId) = default;
};

struct PointerEvent {
  PointerId id;
  PointerKind kind = PointerKind::Mouse;
  foundation::LogicalVec2 position;
  PointerButton button = PointerButton::None;  // Button that changed (down/up), else None.
  PointerButtons buttons;                      // Buttons held after this event.
  Modifiers modifiers;
  float pressure = 0.0f;  // 0..1; 0.5 for a pressed button without pressure sensing.
};

enum class ScrollUnit : std::uint8_t {
  Pixels,  // Precise deltas (touchpads, smooth wheels), in logical pixels.
  Lines,   // Notched wheel deltas, in lines; the core picks the line height.
};

struct ScrollEvent {
  foundation::LogicalVec2 position;  // Pointer position when scrolling.
  ScrollUnit unit = ScrollUnit::Pixels;
  float delta_x = 0.0f;  // Positive scrolls content toward the right/bottom
  float delta_y = 0.0f;  // (the W3C WheelEvent convention).
  Modifiers modifiers;
};

struct KeyEvent {
  PhysicalKey physical = PhysicalKey::Unidentified;
  LogicalKey logical;
  Modifiers modifiers;  // State after this event.
  bool repeat = false;  // Auto-repeat of a held key.
};

// IME composition in progress. Cursor offsets are byte offsets into `text`;
// -1 means no cursor is shown.
struct ImePreeditEvent {
  std::string_view text;
  std::int32_t cursor_begin = -1;
  std::int32_t cursor_end = -1;
};

// Every event except Suspend and Resume concerns one surface, identified by
// surface(). Suspend and Resume apply to the whole application and carry a
// null SurfaceId.
class Event {
 public:
  using Timestamp = foundation::Timestamp;

  [[nodiscard]] static Event pointer_down(SurfaceId s, Timestamp t,
                                          const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerDown, s, t, e);
  }
  [[nodiscard]] static Event pointer_up(SurfaceId s, Timestamp t, const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerUp, s, t, e);
  }
  [[nodiscard]] static Event pointer_move(SurfaceId s, Timestamp t,
                                          const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerMove, s, t, e);
  }
  [[nodiscard]] static Event pointer_cancel(SurfaceId s, Timestamp t,
                                            const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerCancel, s, t, e);
  }
  [[nodiscard]] static Event scroll(SurfaceId s, Timestamp t, const ScrollEvent& e) noexcept {
    Event event(EventKind::Scroll, s, t);
    event.payload_.scroll = e;
    return event;
  }
  [[nodiscard]] static Event key_down(SurfaceId s, Timestamp t, const KeyEvent& e) noexcept {
    return key(EventKind::KeyDown, s, t, e);
  }
  [[nodiscard]] static Event key_up(SurfaceId s, Timestamp t, const KeyEvent& e) noexcept {
    return key(EventKind::KeyUp, s, t, e);
  }
  [[nodiscard]] static Event text_input(SurfaceId s, Timestamp t, std::string_view text) noexcept {
    return with_text(EventKind::TextInput, s, t, text);
  }
  [[nodiscard]] static Event ime_preedit(SurfaceId s, Timestamp t,
                                         const ImePreeditEvent& e) noexcept {
    Event event(EventKind::ImePreedit, s, t);
    event.payload_.preedit = e;
    return event;
  }
  [[nodiscard]] static Event ime_commit(SurfaceId s, Timestamp t, std::string_view text) noexcept {
    return with_text(EventKind::ImeCommit, s, t, text);
  }
  [[nodiscard]] static Event resize(SurfaceId s, Timestamp t, const SurfaceInfo& info) noexcept {
    return with_surface_info(EventKind::Resize, s, t, info);
  }
  [[nodiscard]] static Event scale_factor_changed(SurfaceId s, Timestamp t,
                                                  const SurfaceInfo& info) noexcept {
    return with_surface_info(EventKind::ScaleFactorChanged, s, t, info);
  }
  // Surface events without a payload: FocusGained, FocusLost, SurfaceLost,
  // CloseRequested.
  [[nodiscard]] static Event surface_event(EventKind kind, SurfaceId s, Timestamp t) noexcept {
    UI_ASSERT(kind == EventKind::FocusGained || kind == EventKind::FocusLost ||
                  kind == EventKind::SurfaceLost || kind == EventKind::CloseRequested,
              "event kind requires a payload or is application-wide");
    return Event(kind, s, t);
  }
  // Application-wide events: Suspend, Resume.
  [[nodiscard]] static Event app_event(EventKind kind, Timestamp t) noexcept {
    UI_ASSERT(kind == EventKind::Suspend || kind == EventKind::Resume,
              "not an application-wide event kind");
    return Event(kind, SurfaceId{}, t);
  }

  [[nodiscard]] EventKind kind() const noexcept { return kind_; }
  [[nodiscard]] SurfaceId surface() const noexcept { return surface_; }
  [[nodiscard]] Timestamp time() const noexcept { return time_; }

  [[nodiscard]] bool is_pointer() const noexcept {
    return kind_ == EventKind::PointerDown || kind_ == EventKind::PointerUp ||
           kind_ == EventKind::PointerMove || kind_ == EventKind::PointerCancel;
  }
  [[nodiscard]] bool is_app_wide() const noexcept {
    return kind_ == EventKind::Suspend || kind_ == EventKind::Resume;
  }

  // Payload accessors. Reading the payload of another kind is a programming
  // error and aborts, rather than reading an inactive union member.
  [[nodiscard]] const PointerEvent& pointer() const noexcept {
    UI_ASSERT(is_pointer(), "not a pointer event");
    return payload_.pointer;
  }
  [[nodiscard]] const ScrollEvent& scroll() const noexcept {
    UI_ASSERT(kind_ == EventKind::Scroll, "not a scroll event");
    return payload_.scroll;
  }
  [[nodiscard]] const KeyEvent& key() const noexcept {
    UI_ASSERT(kind_ == EventKind::KeyDown || kind_ == EventKind::KeyUp, "not a key event");
    return payload_.key;
  }
  // Text of TextInput and ImeCommit events.
  [[nodiscard]] std::string_view text() const noexcept {
    UI_ASSERT(kind_ == EventKind::TextInput || kind_ == EventKind::ImeCommit, "not a text event");
    return payload_.text;
  }
  [[nodiscard]] const ImePreeditEvent& preedit() const noexcept {
    UI_ASSERT(kind_ == EventKind::ImePreedit, "not an IME preedit event");
    return payload_.preedit;
  }
  // New size and scale of Resize and ScaleFactorChanged events.
  [[nodiscard]] const SurfaceInfo& surface_info() const noexcept {
    UI_ASSERT(kind_ == EventKind::Resize || kind_ == EventKind::ScaleFactorChanged,
              "not a resize or scale event");
    return payload_.surface_info;
  }

 private:
  struct Empty {};

  union Payload {
    Empty none{};
    PointerEvent pointer;
    ScrollEvent scroll;
    KeyEvent key;
    std::string_view text;
    ImePreeditEvent preedit;
    SurfaceInfo surface_info;
  };

  Event(EventKind kind, SurfaceId s, Timestamp t) noexcept : kind_(kind), surface_(s), time_(t) {}

  static Event pointer(EventKind kind, SurfaceId s, Timestamp t, const PointerEvent& e) noexcept {
    Event event(kind, s, t);
    event.payload_.pointer = e;
    return event;
  }
  static Event key(EventKind kind, SurfaceId s, Timestamp t, const KeyEvent& e) noexcept {
    Event event(kind, s, t);
    event.payload_.key = e;
    return event;
  }
  static Event with_text(EventKind kind, SurfaceId s, Timestamp t, std::string_view text) noexcept {
    Event event(kind, s, t);
    event.payload_.text = text;
    return event;
  }
  static Event with_surface_info(EventKind kind, SurfaceId s, Timestamp t,
                                 const SurfaceInfo& info) noexcept {
    Event event(kind, s, t);
    event.payload_.surface_info = info;
    return event;
  }

  EventKind kind_;
  SurfaceId surface_;
  Timestamp time_;
  Payload payload_;
};

static_assert(std::is_trivially_copyable_v<Event>);

}  // namespace ui::platform
