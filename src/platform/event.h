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

class Event {
 public:
  [[nodiscard]] static Event pointer_down(foundation::Timestamp time,
                                          const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerDown, time, e);
  }
  [[nodiscard]] static Event pointer_up(foundation::Timestamp time,
                                        const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerUp, time, e);
  }
  [[nodiscard]] static Event pointer_move(foundation::Timestamp time,
                                          const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerMove, time, e);
  }
  [[nodiscard]] static Event pointer_cancel(foundation::Timestamp time,
                                            const PointerEvent& e) noexcept {
    return pointer(EventKind::PointerCancel, time, e);
  }
  [[nodiscard]] static Event scroll(foundation::Timestamp time, const ScrollEvent& e) noexcept {
    Event event(EventKind::Scroll, time);
    event.payload_.scroll = e;
    return event;
  }
  [[nodiscard]] static Event key_down(foundation::Timestamp time, const KeyEvent& e) noexcept {
    return key(EventKind::KeyDown, time, e);
  }
  [[nodiscard]] static Event key_up(foundation::Timestamp time, const KeyEvent& e) noexcept {
    return key(EventKind::KeyUp, time, e);
  }
  [[nodiscard]] static Event text_input(foundation::Timestamp time,
                                        std::string_view text) noexcept {
    return with_text(EventKind::TextInput, time, text);
  }
  [[nodiscard]] static Event ime_preedit(foundation::Timestamp time,
                                         const ImePreeditEvent& e) noexcept {
    Event event(EventKind::ImePreedit, time);
    event.payload_.preedit = e;
    return event;
  }
  [[nodiscard]] static Event ime_commit(foundation::Timestamp time,
                                        std::string_view text) noexcept {
    return with_text(EventKind::ImeCommit, time, text);
  }
  [[nodiscard]] static Event resize(foundation::Timestamp time,
                                    const SurfaceInfo& surface) noexcept {
    return with_surface(EventKind::Resize, time, surface);
  }
  [[nodiscard]] static Event scale_factor_changed(foundation::Timestamp time,
                                                  const SurfaceInfo& surface) noexcept {
    return with_surface(EventKind::ScaleFactorChanged, time, surface);
  }
  // For kinds without a payload: focus, lifecycle and close events.
  [[nodiscard]] static Event simple(EventKind kind, foundation::Timestamp time) noexcept {
    UI_ASSERT(kind == EventKind::FocusGained || kind == EventKind::FocusLost ||
                  kind == EventKind::Suspend || kind == EventKind::Resume ||
                  kind == EventKind::SurfaceLost || kind == EventKind::CloseRequested,
              "event kind requires a payload");
    return Event(kind, time);
  }

  [[nodiscard]] EventKind kind() const noexcept { return kind_; }
  [[nodiscard]] foundation::Timestamp time() const noexcept { return time_; }

  [[nodiscard]] bool is_pointer() const noexcept {
    return kind_ == EventKind::PointerDown || kind_ == EventKind::PointerUp ||
           kind_ == EventKind::PointerMove || kind_ == EventKind::PointerCancel;
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
  // Surface of Resize and ScaleFactorChanged events.
  [[nodiscard]] const SurfaceInfo& surface() const noexcept {
    UI_ASSERT(kind_ == EventKind::Resize || kind_ == EventKind::ScaleFactorChanged,
              "not a surface event");
    return payload_.surface;
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
    SurfaceInfo surface;
  };

  Event(EventKind kind, foundation::Timestamp time) noexcept : kind_(kind), time_(time) {}

  static Event pointer(EventKind kind, foundation::Timestamp time, const PointerEvent& e) noexcept {
    Event event(kind, time);
    event.payload_.pointer = e;
    return event;
  }
  static Event key(EventKind kind, foundation::Timestamp time, const KeyEvent& e) noexcept {
    Event event(kind, time);
    event.payload_.key = e;
    return event;
  }
  static Event with_text(EventKind kind, foundation::Timestamp time,
                         std::string_view text) noexcept {
    Event event(kind, time);
    event.payload_.text = text;
    return event;
  }
  static Event with_surface(EventKind kind, foundation::Timestamp time,
                            const SurfaceInfo& surface) noexcept {
    Event event(kind, time);
    event.payload_.surface = surface;
    return event;
  }

  EventKind kind_;
  foundation::Timestamp time_;
  Payload payload_;
};

static_assert(std::is_trivially_copyable_v<Event>);

}  // namespace ui::platform
