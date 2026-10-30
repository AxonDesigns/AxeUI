#include "platform/host_fixture.h"

using namespace ui::foundation;
using namespace ui::platform;
using ui::test::HostHarness;

namespace {

PointerEvent mouse_at(float x, float y, PointerButtons buttons = {}) {
  PointerEvent e;
  e.id = PointerId{0};
  e.kind = PointerKind::Mouse;
  e.position = {x, y};
  e.buttons = buttons;
  return e;
}

PointerEvent touch(std::uint64_t id, float x, float y) {
  PointerEvent e;
  e.id = PointerId{id};
  e.kind = PointerKind::Touch;
  e.position = {x, y};
  e.button = PointerButton::Primary;
  e.buttons = PointerButton::Primary;
  e.pressure = 0.5f;
  return e;
}

}  // namespace

UI_TEST_CASE("core requests its first frame and renders it") {
  HostHarness h(memory());
  CHECK(h.recorder.frame_requests == 1);
  CHECK(h.core.frame_requested());
  CHECK(h.core.surface().available);
  CHECK(h.core.surface().info == ui::test::kTestSurface);

  h.tick();
  CHECK(h.core.frames_rendered() == 1);
  CHECK_FALSE(h.core.frame_requested());
  CHECK_FALSE(h.core.needs_redraw());

  // Nothing changed: the core asks for nothing and a stray tick does nothing.
  h.tick();
  CHECK(h.core.frames_rendered() == 1);
  CHECK(h.recorder.frame_requests == 1);
}

UI_TEST_CASE("frame requests are coalesced until the frame arrives") {
  HostHarness h(memory());
  h.core.request_redraw();
  h.core.request_redraw();
  h.shell.push_event(Event::simple(EventKind::FocusGained, h.now()));
  CHECK(h.recorder.frame_requests == 1);

  h.tick();
  CHECK(h.core.frames_rendered() == 1);
  h.core.request_redraw();
  h.core.request_redraw();
  CHECK(h.recorder.frame_requests == 2);
}

UI_TEST_CASE("no frames are requested or rendered while suspended") {
  HostHarness h(memory());
  h.tick();
  h.shell.push_event(Event::simple(EventKind::Suspend, h.now()));
  CHECK(h.core.lifecycle() == Lifecycle::Suspended);

  h.core.request_redraw();
  CHECK(h.recorder.frame_requests == 1);
  h.tick();
  CHECK(h.core.frames_rendered() == 1);

  h.shell.push_event(Event::simple(EventKind::Resume, h.now()));
  CHECK(h.core.lifecycle() == Lifecycle::Running);
  CHECK(h.recorder.frame_requests == 2);
  h.tick();
  CHECK(h.core.frames_rendered() == 2);
}

UI_TEST_CASE("a request pending at suspension is re-issued on resume") {
  HostHarness h(memory());
  CHECK(h.core.frame_requested());
  h.shell.push_event(Event::simple(EventKind::Suspend, h.now()));
  CHECK_FALSE(h.core.frame_requested());  // The platform may never deliver it.
  h.shell.push_event(Event::simple(EventKind::Resume, h.now()));
  CHECK(h.recorder.frame_requests == 2);
}

UI_TEST_CASE("surface loss stops rendering until a new surface is reported") {
  HostHarness h(memory());
  h.tick();
  h.shell.push_event(Event::simple(EventKind::SurfaceLost, h.now()));
  CHECK_FALSE(h.core.surface().available);
  h.core.request_redraw();
  CHECK(h.recorder.frame_requests == 1);

  h.shell.push_event(Event::resize(h.now(), ui::test::kTestSurface));
  CHECK(h.core.surface().available);
  CHECK(h.recorder.frame_requests == 2);
  h.tick();
  CHECK(h.core.frames_rendered() == 2);
}

UI_TEST_CASE("a zero-size surface defers the first frame until resize") {
  const SurfaceInfo minimized{{0.0f, 0.0f}, ScaleFactor{1.0f}};
  HostHarness h(memory(), minimized);
  CHECK(h.recorder.frame_requests == 0);
  CHECK(h.core.needs_redraw());

  CHECK(h.shell.set_surface({640, 480}, 1.0f, h.now()).has_value());
  CHECK(h.recorder.frame_requests == 1);
  h.tick();
  CHECK(h.core.frames_rendered() == 1);
}

UI_TEST_CASE("surface changes reach the core as resize and scale events") {
  HostHarness h(memory());
  h.tick();
  const std::uint64_t before = h.core.events_dispatched();

  CHECK(h.shell.set_surface({1500, 750}, 3.0f, h.now()).has_value());
  CHECK(h.core.events_dispatched() == before + 2);  // ScaleFactorChanged + Resize.
  CHECK(h.core.surface().info.scale == ScaleFactor{3.0f});
  CHECK(h.core.surface().info.logical_size() == LogicalSize{500, 250});
  CHECK(h.shell.surface_info() == h.core.surface().info);
  CHECK(h.recorder.frame_requests == 2);

  // Same values again: nothing is dispatched.
  CHECK(h.shell.set_surface({1500, 750}, 3.0f, h.now()).has_value());
  CHECK(h.core.events_dispatched() == before + 2);

  // Scale only.
  CHECK(h.shell.set_surface({1500, 750}, 1.5f, h.now()).has_value());
  CHECK(h.core.events_dispatched() == before + 3);
  CHECK(h.core.surface().info.logical_size() == LogicalSize{1000, 500});
}

UI_TEST_CASE("invalid surface values are rejected") {
  HostHarness h(memory());
  const std::uint64_t before = h.core.events_dispatched();
  CHECK(h.shell.set_surface({100, 100}, 0.0f, h.now()).error().code == ErrorCode::InvalidArgument);
  CHECK(h.shell.set_surface({-1, 100}, 1.0f, h.now()).error().code == ErrorCode::InvalidArgument);
  CHECK(h.core.events_dispatched() == before);

  // A malformed resize pushed directly is logged and ignored by the core.
  h.shell.push_event(Event::resize(h.now(), SurfaceInfo{{-5, 10}, ScaleFactor{1.0f}}));
  CHECK(h.core.surface().info == ui::test::kTestSurface);
  CHECK(h.shell.surface_info() == ui::test::kTestSurface);
  UI_REQUIRE(h.log.records().size() == 1);
  CHECK(h.log.records()[0].code == ErrorCode::InvalidArgument);
}

UI_TEST_CASE("pointer state follows mouse events") {
  HostHarness h(memory());
  h.shell.push_event(Event::pointer_move(h.now(), mouse_at(10, 20)));
  UI_REQUIRE(h.core.pointers().size() == 1);
  CHECK(h.core.pointers()[0].position == LogicalVec2{10, 20});
  CHECK(h.core.pointers()[0].buttons.empty());

  PointerEvent down = mouse_at(11, 21, PointerButton::Primary);
  down.button = PointerButton::Primary;
  down.modifiers = Modifier::Shift;
  h.shell.push_event(Event::pointer_down(h.now(), down));
  CHECK(h.core.pointers()[0].buttons.has(PointerButton::Primary));
  CHECK(h.core.modifiers().has(Modifier::Shift));

  PointerEvent up = mouse_at(12, 22);
  up.button = PointerButton::Primary;
  h.shell.push_event(Event::pointer_up(h.now(), up));
  UI_REQUIRE(h.core.pointers().size() == 1);  // The mouse keeps hovering.
  CHECK(h.core.pointers()[0].buttons.empty());
  CHECK(h.core.pointers()[0].position == LogicalVec2{12, 22});
}

UI_TEST_CASE("touch pointers exist only while in contact") {
  HostHarness h(memory());
  h.shell.push_event(Event::pointer_down(h.now(), touch(7, 1, 1)));
  h.shell.push_event(Event::pointer_down(h.now(), touch(8, 2, 2)));
  CHECK(h.core.pointers().size() == 2);

  h.shell.push_event(Event::pointer_up(h.now(), touch(7, 1, 1)));
  UI_REQUIRE(h.core.pointers().size() == 1);
  CHECK(h.core.pointers()[0].id == PointerId{8});

  h.shell.push_event(Event::pointer_cancel(h.now(), touch(8, 2, 2)));
  CHECK(h.core.pointers().empty());
}

UI_TEST_CASE("losing focus drops held keys, buttons, touches and preedit") {
  HostHarness h(memory());
  h.shell.push_event(Event::simple(EventKind::FocusGained, h.now()));
  CHECK(h.core.focused());

  KeyEvent key;
  key.physical = PhysicalKey::ControlLeft;
  key.logical = LogicalKey::named_key(NamedKey::Control);
  key.modifiers = Modifier::Control;
  h.shell.push_event(Event::key_down(h.now(), key));
  CHECK(h.core.modifiers().has(Modifier::Control));

  PointerEvent down = mouse_at(5, 5, PointerButton::Secondary);
  down.button = PointerButton::Secondary;
  h.shell.push_event(Event::pointer_down(h.now(), down));
  h.shell.push_event(Event::pointer_down(h.now(), touch(3, 1, 1)));
  h.shell.push_event(Event::ime_preedit(h.now(), ImePreeditEvent{"ka", 2, 2}));

  h.shell.push_event(Event::simple(EventKind::FocusLost, h.now()));
  CHECK_FALSE(h.core.focused());
  CHECK(h.core.modifiers().empty());
  UI_REQUIRE(h.core.pointers().size() == 1);
  CHECK(h.core.pointers()[0].kind == PointerKind::Mouse);
  CHECK(h.core.pointers()[0].buttons.empty());
  CHECK(h.core.ime_preedit().text.empty());
}

UI_TEST_CASE("IME preedit is tracked and cleared by commit") {
  HostHarness h(memory());
  h.shell.push_event(Event::ime_preedit(h.now(), ImePreeditEvent{"にほ", 3, 6}));
  CHECK(h.core.ime_preedit().text == "にほ");
  CHECK(h.core.ime_preedit().cursor_begin == 3);
  CHECK(h.core.ime_preedit().cursor_end == 6);

  // Out-of-range cursor is dropped, text kept.
  h.shell.push_event(Event::ime_preedit(h.now(), ImePreeditEvent{"に", 0, 99}));
  CHECK(h.core.ime_preedit().text == "に");
  CHECK(h.core.ime_preedit().cursor_begin == -1);

  h.shell.push_event(Event::ime_commit(h.now(), "日本"));
  CHECK(h.core.ime_preedit().text.empty());
  CHECK(h.log.records().empty());
}

UI_TEST_CASE("text with invalid UTF-8 is reported and ignored") {
  HostHarness h(memory());
  h.shell.push_event(Event::text_input(h.now(), "ok"));
  CHECK(h.log.records().empty());
  h.shell.push_event(Event::text_input(h.now(), std::string_view("\xC3\x28", 2)));
  h.shell.push_event(
      Event::ime_preedit(h.now(), ImePreeditEvent{std::string_view("\xFF", 1), -1, -1}));
  UI_REQUIRE(h.log.records().size() == 2);
  CHECK(h.log.records()[0].code == ErrorCode::InvalidArgument);
  CHECK(h.core.ime_preedit().text.empty());
}

UI_TEST_CASE("cursor and IME area reach the host only when they change") {
  HostHarness h(memory());
  h.core.set_cursor(CursorShape::Text);
  h.core.set_cursor(CursorShape::Text);
  h.core.set_cursor(CursorShape::Pointer);
  CHECK(h.recorder.cursors == std::vector<CursorShape>{CursorShape::Text, CursorShape::Pointer});

  const LogicalRect area = LogicalRect::from_xywh(10, 20, 2, 16);
  h.core.set_ime_cursor_area(area);
  h.core.set_ime_cursor_area(area);
  UI_REQUIRE(h.recorder.ime_areas.size() == 1);
  CHECK(h.recorder.ime_areas[0] == area);
}

UI_TEST_CASE("clipboard goes through the host callbacks") {
  HostHarness h(memory());
  CHECK(h.core.clipboard_set_text("héllo").has_value());
  CHECK(h.recorder.clipboard == "héllo");

  String text{Allocator<char>(memory())};
  CHECK(h.core.clipboard_get_text(text).has_value());
  CHECK(text == "héllo");

  CHECK(h.core.clipboard_set_text(std::string_view("\xC0\x80", 2)).error().code ==
        ErrorCode::InvalidArgument);
  h.recorder.clipboard = "\xED\xA0\x80";  // Encoded surrogate.
  CHECK(h.core.clipboard_get_text(text).error().code == ErrorCode::InvalidArgument);
  CHECK(text.empty());
}

UI_TEST_CASE("host without clipboard callbacks reports Unavailable") {
  HostShellCallbacks callbacks;  // All null.
  HostShell shell(ui::test::kTestSurface, callbacks);
  const Logger logger;
  Core core(shell, memory(), logger);
  String text{Allocator<char>(memory())};
  CHECK(core.clipboard_get_text(text).error().code == ErrorCode::Unavailable);
  CHECK(core.clipboard_set_text("x").error().code == ErrorCode::Unavailable);
  core.set_cursor(CursorShape::Wait);  // Dropped without a callback.
  shell.tick(Timestamp{1});
  CHECK(core.frames_rendered() == 1);
}

UI_TEST_CASE("close requests are recorded for the application") {
  HostHarness h(memory());
  CHECK_FALSE(h.core.close_requested());
  h.shell.push_event(Event::simple(EventKind::CloseRequested, h.now()));
  CHECK(h.core.close_requested());
}

UI_TEST_CASE("frame timestamps going backwards are reported") {
  HostHarness h(memory());
  h.shell.tick(Timestamp{2'000'000});
  h.core.request_redraw();
  h.shell.tick(Timestamp{1'000'000});
  UI_REQUIRE(h.log.records().size() == 1);
  CHECK(h.log.records()[0].level == LogLevel::Warning);
  CHECK(h.core.frames_rendered() == 2);
}

UI_TEST_CASE("scroll and key events update modifiers") {
  HostHarness h(memory());
  ScrollEvent scroll;
  scroll.unit = ScrollUnit::Lines;
  scroll.delta_y = 3;
  scroll.modifiers = Modifier::Alt;
  h.shell.push_event(Event::scroll(h.now(), scroll));
  CHECK(h.core.modifiers() == Modifiers(Modifier::Alt));

  KeyEvent key;
  key.physical = PhysicalKey::A;
  key.logical = LogicalKey::from_character(U'a');
  h.shell.push_event(Event::key_up(h.now(), key));
  CHECK(h.core.modifiers().empty());
}
