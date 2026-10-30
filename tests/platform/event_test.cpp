#include "platform/event.h"

#include <string>
#include <type_traits>

#include "support/test_support.h"

using namespace ui::foundation;
using namespace ui::platform;

static_assert(std::is_trivially_copyable_v<Event>);

TEST_CASE("events carry their payload and timestamp") {
  PointerEvent pointer;
  pointer.id = PointerId{42};
  pointer.kind = PointerKind::Pen;
  pointer.position = {1.5f, 2.5f};
  pointer.pressure = 0.25f;
  const SurfaceId window{Handle::make(HandleType::Surface, 1, 3)};
  const Event down = Event::pointer_down(window, Timestamp{99}, pointer);
  CHECK(down.kind() == EventKind::PointerDown);
  CHECK(down.surface() == window);
  CHECK(down.time() == Timestamp{99});
  CHECK(down.is_pointer());
  CHECK(down.pointer().id == PointerId{42});
  CHECK(down.pointer().kind == PointerKind::Pen);
  CHECK(down.pointer().pressure == 0.25f);

  const Event text = Event::text_input(window, Timestamp{}, "abc");
  CHECK(text.text() == "abc");
  CHECK_FALSE(text.is_pointer());

  const SurfaceInfo surface{{200, 100}, ScaleFactor{2.0f}, 60.0f};
  const Event resize = Event::resize(window, Timestamp{}, surface);
  CHECK(resize.surface_info().logical_size() == LogicalSize{100, 50});

  const Event copy = resize;  // Trivially copyable.
  CHECK(copy.surface_info() == surface);

  const Event suspend = Event::app_event(EventKind::Suspend, Timestamp{});
  CHECK(suspend.is_app_wide());
  CHECK(suspend.surface().is_null());
}

TEST_CASE("every event kind has a name") {
  for (int k = 0; k <= static_cast<int>(EventKind::CloseRequested); ++k) {
    CHECK(std::string(event_kind_name(static_cast<EventKind>(k))) != "Unknown");
  }
}

TEST_CASE("modifier and button flags") {
  Modifiers mods = Modifiers(Modifier::Shift) | Modifier::Meta;
  CHECK(mods.has(Modifier::Shift));
  CHECK(mods.has(Modifier::Meta));
  CHECK_FALSE(mods.has(Modifier::Alt));
  mods.clear(Modifier::Shift);
  CHECK(mods == Modifiers(Modifier::Meta));

  PointerButtons buttons;
  CHECK(buttons.empty());
  buttons.set(PointerButton::Primary).set(PointerButton::Middle);
  CHECK(buttons.bits() == 0b101);
}

TEST_CASE("physical keys use USB HID usage IDs") {
  CHECK(static_cast<int>(PhysicalKey::A) == 0x04);
  CHECK(static_cast<int>(PhysicalKey::Enter) == 0x28);
  CHECK(static_cast<int>(PhysicalKey::ArrowUp) == 0x52);
  CHECK(static_cast<int>(PhysicalKey::MetaRight) == 0xE7);
  CHECK(LogicalKey::from_character(U'é').named == NamedKey::Character);
}
