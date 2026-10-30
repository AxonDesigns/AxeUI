#include "platform/core.h"

#include <vector>

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

// Records render_surface calls; optionally runs an action inside the frame.
struct RecordingFrameHandler final : FrameHandler {
  std::vector<SurfaceId> rendered;
  std::vector<SurfaceId> removed;
  void (*on_render)(RecordingFrameHandler& self, SurfaceId id) = nullptr;
  Core* core = nullptr;

  void render_surface(SurfaceId id, const SurfaceState&, Timestamp) override {
    rendered.push_back(id);
    if (on_render != nullptr) {
      on_render(*this, id);
    }
  }
  void surface_removed(SurfaceId id) override { removed.push_back(id); }
};

}  // namespace

UI_TEST_CASE("a new surface requests its first frame and renders it") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  CHECK(id.handle.type() == HandleType::Surface);
  CHECK(h.window(0).surface.id() == id);
  CHECK(h.window(0).recorder.frame_requests == 1);
  CHECK(h.state(id).frame_requested);
  CHECK(h.state(id).available);
  CHECK(h.state(id).info == ui::test::kTestSurface);

  h.tick(id);
  CHECK(h.state(id).frames_rendered == 1);
  CHECK_FALSE(h.state(id).frame_requested);
  CHECK_FALSE(h.state(id).needs_redraw);

  // Nothing changed: no request, and a stray tick does nothing.
  h.tick(id);
  CHECK(h.state(id).frames_rendered == 1);
  CHECK(h.window(0).recorder.frame_requests == 1);
}

UI_TEST_CASE("surfaces schedule frames independently") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open(SurfaceInfo{{300, 200}, ScaleFactor{1.25f}, 144.0f});
  CHECK(h.core.surface_count() == 2);
  h.tick(a);
  h.tick(b);

  CHECK(h.core.request_redraw(b).has_value());
  CHECK(h.window(0).recorder.frame_requests == 1);
  CHECK(h.window(1).recorder.frame_requests == 2);
  h.tick(a);  // a has nothing to draw.
  CHECK(h.state(a).frames_rendered == 1);
  h.tick(b);
  CHECK(h.state(b).frames_rendered == 2);
  CHECK(h.core.frames_rendered() == 3);

  int visited = 0;
  h.core.for_each_surface([&](SurfaceId, const SurfaceState&) { ++visited; });
  CHECK(visited == 2);
}

UI_TEST_CASE("frame requests are coalesced until the frame arrives") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  CHECK(h.core.request_redraw(id).has_value());
  CHECK(h.core.request_redraw(id).has_value());
  h.shell.push_event(Event::surface_event(EventKind::FocusGained, id, h.now()));
  CHECK(h.window(0).recorder.frame_requests == 1);

  h.tick(id);
  CHECK(h.core.request_redraw(id).has_value());
  CHECK(h.core.request_redraw(id).has_value());
  CHECK(h.window(0).recorder.frame_requests == 2);
}

UI_TEST_CASE("the frame handler renders exactly the frames the core decides on") {
  HostHarness h(memory());
  RecordingFrameHandler handler;
  h.core.set_frame_handler(&handler);
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  h.tick(a);
  h.tick(b);
  h.tick(b);  // Spurious.
  CHECK(handler.rendered == std::vector<SurfaceId>{a, b});

  // A redraw requested inside the frame (an animation) gets the next frame.
  handler.core = &h.core;
  handler.on_render = [](RecordingFrameHandler& self, SurfaceId id) {
    (void)self.core->request_redraw(id);
  };
  CHECK(h.core.request_redraw(a).has_value());
  const int before = h.window(0).recorder.frame_requests;
  h.tick(a);
  CHECK(h.window(0).recorder.frame_requests == before + 1);
  CHECK(h.state(a).frame_requested);
  h.core.set_frame_handler(nullptr);
}

UI_TEST_CASE("no frames are requested or rendered while suspended") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  h.tick(a);
  h.tick(b);
  h.shell.push_event(Event::app_event(EventKind::Suspend, h.now()));
  CHECK(h.core.lifecycle() == Lifecycle::Suspended);

  CHECK(h.core.request_redraw(a).has_value());
  CHECK(h.window(0).recorder.frame_requests == 1);
  h.tick(a);
  CHECK(h.state(a).frames_rendered == 1);

  h.shell.push_event(Event::app_event(EventKind::Resume, h.now()));
  CHECK(h.core.lifecycle() == Lifecycle::Running);
  CHECK(h.window(0).recorder.frame_requests == 2);
  CHECK(h.window(1).recorder.frame_requests == 2);  // Every surface redraws.
}

UI_TEST_CASE("a request pending at suspension is re-issued on resume") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  CHECK(h.state(id).frame_requested);
  h.shell.push_event(Event::app_event(EventKind::Suspend, h.now()));
  CHECK_FALSE(h.state(id).frame_requested);  // The platform may never deliver it.
  h.shell.push_event(Event::app_event(EventKind::Resume, h.now()));
  CHECK(h.window(0).recorder.frame_requests == 2);
}

UI_TEST_CASE("surface loss stops rendering until a new surface is reported") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.tick(id);
  h.shell.push_event(Event::surface_event(EventKind::SurfaceLost, id, h.now()));
  CHECK_FALSE(h.state(id).available);
  CHECK(h.core.request_redraw(id).has_value());
  CHECK(h.window(0).recorder.frame_requests == 1);

  h.shell.push_event(Event::resize(id, h.now(), ui::test::kTestSurface));
  CHECK(h.state(id).available);
  CHECK(h.window(0).recorder.frame_requests == 2);
  h.tick(id);
  CHECK(h.state(id).frames_rendered == 2);
}

UI_TEST_CASE("a zero-size surface defers its first frame until resize") {
  HostHarness h(memory());
  const SurfaceId id = h.open(SurfaceInfo{{0.0f, 0.0f}, ScaleFactor{1.0f}, 0.0f});
  CHECK(h.window(0).recorder.frame_requests == 0);
  CHECK(h.state(id).needs_redraw);

  CHECK(h.window(0).surface.set_info({640, 480}, 1.0f, h.now()).has_value());
  CHECK(h.window(0).recorder.frame_requests == 1);
  h.tick(id);
  CHECK(h.state(id).frames_rendered == 1);
}

UI_TEST_CASE("surface changes reach the core as resize and scale events") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.tick(id);
  const std::uint64_t before = h.core.events_dispatched();
  HostSurface& surface = h.window(0).surface;

  CHECK(surface.set_info({1500, 750}, 3.0f, h.now(), 120.0f).has_value());
  CHECK(h.core.events_dispatched() == before + 2);  // ScaleFactorChanged + Resize.
  CHECK(h.state(id).info.scale == ScaleFactor{3.0f});
  CHECK(h.state(id).info.refresh_rate_hz == 120.0f);
  CHECK(h.state(id).info.logical_size() == LogicalSize{500, 250});
  CHECK(surface.info() == h.state(id).info);
  CHECK(h.window(0).recorder.frame_requests == 2);

  CHECK(surface.set_info({1500, 750}, 3.0f, h.now()).has_value());  // No change.
  CHECK(h.core.events_dispatched() == before + 2);

  CHECK(surface.set_info({1500, 750}, 1.5f, h.now()).has_value());  // Scale only.
  CHECK(h.core.events_dispatched() == before + 3);
  CHECK(h.state(id).info.logical_size() == LogicalSize{1000, 500});
}

UI_TEST_CASE("invalid surface values are rejected") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  const std::uint64_t before = h.core.events_dispatched();
  HostSurface& surface = h.window(0).surface;
  CHECK(surface.set_info({100, 100}, 0.0f, h.now()).error().code == ErrorCode::InvalidArgument);
  CHECK(surface.set_info({-1, 100}, 1.0f, h.now()).error().code == ErrorCode::InvalidArgument);
  CHECK(h.core.events_dispatched() == before);

  // A malformed resize pushed directly is logged and ignored by the core.
  h.shell.push_event(Event::resize(id, h.now(), SurfaceInfo{{-5, 10}, ScaleFactor{1.0f}, 0.0f}));
  CHECK(h.state(id).info == ui::test::kTestSurface);
  UI_REQUIRE(h.log.records().size() == 1);
  CHECK(h.log.records()[0].code == ErrorCode::InvalidArgument);
}

UI_TEST_CASE("events for unknown or removed surfaces are reported and dropped") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  CHECK(h.core.remove_surface(b).has_value());
  CHECK(h.core.surface_count() == 1);
  CHECK(h.window(1).surface.id().is_null());

  h.shell.push_event(Event::surface_event(EventKind::CloseRequested, b, h.now()));
  h.shell.push_event(Event::surface_event(EventKind::CloseRequested, SurfaceId{}, h.now()));
  h.shell.tick(b, h.now());
  UI_REQUIRE(h.log.records().size() == 3);
  CHECK(h.log.records()[0].code == ErrorCode::StaleHandle);
  CHECK(h.log.records()[1].code == ErrorCode::InvalidHandle);
  CHECK(h.log.records()[2].code == ErrorCode::StaleHandle);
  CHECK_FALSE(h.state(a).close_requested);
  CHECK(h.core.remove_surface(b).error().code == ErrorCode::StaleHandle);
}

UI_TEST_CASE("removing a surface during dispatch is deferred until dispatch ends") {
  HostHarness h(memory());
  RecordingFrameHandler handler;
  handler.core = &h.core;
  h.core.set_frame_handler(&handler);
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();

  // The frame handler removes the surface it is rendering, and the other one.
  handler.on_render = [](RecordingFrameHandler& self, SurfaceId id) {
    CHECK(self.core->in_dispatch());
    CHECK(self.core->remove_surface(id).has_value());
    // Still registered (removal is deferred) but no longer addressable.
    CHECK(self.core->surface_count() == 2);
    CHECK(self.core->request_redraw(id).error().code == ErrorCode::StaleHandle);
    CHECK(self.removed.empty());
  };
  h.tick(a);
  CHECK_FALSE(h.core.in_dispatch());
  CHECK(handler.removed == std::vector<SurfaceId>{a});
  CHECK(h.core.surface_count() == 1);
  CHECK(h.core.surface(b).has_value());
  h.core.set_frame_handler(nullptr);
}

UI_TEST_CASE("pointer state is tracked per surface") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  h.shell.push_event(Event::pointer_move(a, h.now(), mouse_at(10, 20)));
  UI_REQUIRE(h.state(a).pointers.size() == 1);
  CHECK(h.state(b).pointers.empty());
  CHECK(h.state(a).pointers[0].position == LogicalVec2{10, 20});

  PointerEvent down = mouse_at(11, 21, PointerButton::Primary);
  down.button = PointerButton::Primary;
  down.modifiers = Modifier::Shift;
  h.shell.push_event(Event::pointer_down(a, h.now(), down));
  CHECK(h.state(a).pointers[0].buttons.has(PointerButton::Primary));
  CHECK(h.core.modifiers().has(Modifier::Shift));

  PointerEvent up = mouse_at(12, 22);
  up.button = PointerButton::Primary;
  h.shell.push_event(Event::pointer_up(a, h.now(), up));
  UI_REQUIRE(h.state(a).pointers.size() == 1);  // The mouse keeps hovering.
  CHECK(h.state(a).pointers[0].buttons.empty());
}

UI_TEST_CASE("touch pointers exist only while in contact") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.shell.push_event(Event::pointer_down(id, h.now(), touch(7, 1, 1)));
  h.shell.push_event(Event::pointer_down(id, h.now(), touch(8, 2, 2)));
  CHECK(h.state(id).pointers.size() == 2);
  h.shell.push_event(Event::pointer_up(id, h.now(), touch(7, 1, 1)));
  UI_REQUIRE(h.state(id).pointers.size() == 1);
  CHECK(h.state(id).pointers[0].id == PointerId{8});
  h.shell.push_event(Event::pointer_cancel(id, h.now(), touch(8, 2, 2)));
  CHECK(h.state(id).pointers.empty());
}

UI_TEST_CASE("losing focus drops held keys, buttons, touches and preedit") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.shell.push_event(Event::surface_event(EventKind::FocusGained, id, h.now()));
  CHECK(h.state(id).focused);

  KeyEvent key;
  key.physical = PhysicalKey::ControlLeft;
  key.logical = LogicalKey::named_key(NamedKey::Control);
  key.modifiers = Modifier::Control;
  h.shell.push_event(Event::key_down(id, h.now(), key));
  CHECK(h.core.modifiers().has(Modifier::Control));

  PointerEvent down = mouse_at(5, 5, PointerButton::Secondary);
  down.button = PointerButton::Secondary;
  h.shell.push_event(Event::pointer_down(id, h.now(), down));
  h.shell.push_event(Event::pointer_down(id, h.now(), touch(3, 1, 1)));
  h.shell.push_event(Event::ime_preedit(id, h.now(), ImePreeditEvent{"ka", 2, 2}));

  h.shell.push_event(Event::surface_event(EventKind::FocusLost, id, h.now()));
  CHECK_FALSE(h.state(id).focused);
  CHECK(h.core.modifiers().empty());
  UI_REQUIRE(h.state(id).pointers.size() == 1);
  CHECK(h.state(id).pointers[0].kind == PointerKind::Mouse);
  CHECK(h.state(id).pointers[0].buttons.empty());
  CHECK(h.state(id).preedit.text.empty());
}

UI_TEST_CASE("IME preedit is tracked per surface and cleared by commit") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.shell.push_event(Event::ime_preedit(id, h.now(), ImePreeditEvent{"にほ", 3, 6}));
  CHECK(h.state(id).preedit.text == "にほ");
  CHECK(h.state(id).preedit.cursor_begin == 3);
  CHECK(h.state(id).preedit.cursor_end == 6);

  h.shell.push_event(Event::ime_preedit(id, h.now(), ImePreeditEvent{"に", 0, 99}));
  CHECK(h.state(id).preedit.text == "に");
  CHECK(h.state(id).preedit.cursor_begin == -1);

  h.shell.push_event(Event::ime_commit(id, h.now(), "日本"));
  CHECK(h.state(id).preedit.text.empty());
  CHECK(h.log.records().empty());
}

UI_TEST_CASE("text with invalid UTF-8 is reported and ignored") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.shell.push_event(Event::text_input(id, h.now(), "ok"));
  CHECK(h.log.records().empty());
  h.shell.push_event(Event::text_input(id, h.now(), std::string_view("\xC3\x28", 2)));
  h.shell.push_event(
      Event::ime_preedit(id, h.now(), ImePreeditEvent{std::string_view("\xFF", 1), -1, -1}));
  UI_REQUIRE(h.log.records().size() == 2);
  CHECK(h.log.records()[0].code == ErrorCode::InvalidArgument);
  CHECK(h.state(id).preedit.text.empty());
}

UI_TEST_CASE("cursor and IME area reach the surface only when they change") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  CHECK(h.core.set_cursor(a, CursorShape::Text).has_value());
  CHECK(h.core.set_cursor(a, CursorShape::Text).has_value());
  CHECK(h.core.set_cursor(a, CursorShape::Pointer).has_value());
  CHECK(h.core.set_cursor(b, CursorShape::Wait).has_value());
  CHECK(h.window(0).recorder.cursors ==
        std::vector<CursorShape>{CursorShape::Text, CursorShape::Pointer});
  CHECK(h.window(1).recorder.cursors == std::vector<CursorShape>{CursorShape::Wait});

  const LogicalRect area = LogicalRect::from_xywh(10, 20, 2, 16);
  CHECK(h.core.set_ime_cursor_area(a, area).has_value());
  CHECK(h.core.set_ime_cursor_area(a, area).has_value());
  UI_REQUIRE(h.window(0).recorder.ime_areas.size() == 1);
  CHECK(h.window(0).recorder.ime_areas[0] == area);
  CHECK(h.window(1).recorder.ime_areas.empty());
}

UI_TEST_CASE("clipboard goes through the platform callbacks") {
  HostHarness h(memory());
  CHECK(h.core.clipboard_set_text("héllo").has_value());
  CHECK(h.platform_recorder.clipboard == "héllo");

  String text{Allocator<char>(memory())};
  CHECK(h.core.clipboard_get_text(text).has_value());
  CHECK(text == "héllo");

  CHECK(h.core.clipboard_set_text(std::string_view("\xC0\x80", 2)).error().code ==
        ErrorCode::InvalidArgument);
  h.platform_recorder.clipboard = "\xED\xA0\x80";  // Encoded surrogate.
  CHECK(h.core.clipboard_get_text(text).error().code == ErrorCode::InvalidArgument);
  CHECK(text.empty());

  h.shell.wake();
  CHECK(h.platform_recorder.wakes == 1);
}

UI_TEST_CASE("a host without callbacks degrades gracefully") {
  HostShell shell(HostShellCallbacks{});
  const Logger logger;
  Core core(shell, memory(), logger);
  HostSurface surface(ui::test::kTestSurface, NativeSurfaceHandle::none(), HostSurfaceCallbacks{});
  const SurfaceId id = *core.add_surface(surface);
  String text{Allocator<char>(memory())};
  CHECK(core.clipboard_get_text(text).error().code == ErrorCode::Unavailable);
  CHECK(core.clipboard_set_text("x").error().code == ErrorCode::Unavailable);
  CHECK(core.set_cursor(id, CursorShape::Wait).has_value());
  shell.wake();
  shell.tick(id, Timestamp{1});
  CHECK(core.frames_rendered() == 1);
  CHECK(core.remove_surface(id).has_value());
}

UI_TEST_CASE("close requests are recorded per surface") {
  HostHarness h(memory());
  const SurfaceId a = h.open();
  const SurfaceId b = h.open();
  h.shell.push_event(Event::surface_event(EventKind::CloseRequested, b, h.now()));
  CHECK_FALSE(h.state(a).close_requested);
  CHECK(h.state(b).close_requested);
}

UI_TEST_CASE("frame timestamps going backwards are reported") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  h.shell.tick(id, Timestamp{2'000'000});
  CHECK(h.core.request_redraw(id).has_value());
  h.shell.tick(id, Timestamp{1'000'000});
  UI_REQUIRE(h.log.records().size() == 1);
  CHECK(h.log.records()[0].level == LogLevel::Warning);
  CHECK(h.state(id).frames_rendered == 2);
}

UI_TEST_CASE("scroll and key events update modifiers") {
  HostHarness h(memory());
  const SurfaceId id = h.open();
  ScrollEvent scroll;
  scroll.unit = ScrollUnit::Lines;
  scroll.delta_y = 3;
  scroll.modifiers = Modifier::Alt;
  h.shell.push_event(Event::scroll(id, h.now(), scroll));
  CHECK(h.core.modifiers() == Modifiers(Modifier::Alt));

  KeyEvent key;
  key.physical = PhysicalKey::A;
  key.logical = LogicalKey::from_character(U'a');
  h.shell.push_event(Event::key_up(id, h.now(), key));
  CHECK(h.core.modifiers().empty());
}
