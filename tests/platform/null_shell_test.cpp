#include "platform/null_shell.h"

#include "platform/core.h"
#include "support/test_support.h"

using namespace ui::foundation;
using namespace ui::platform;

UI_TEST_CASE("null shell and surface drive a core headlessly") {
  NullShell shell(memory());
  const Logger logger;
  Core core(shell, memory(), logger);
  NullSurface surface;
  const SurfaceId id = *core.add_surface(surface);
  CHECK(surface.native_handle().is_none());
  CHECK((*core.surface(id))->info.logical_size() == LogicalSize{800, 600});
  CHECK(surface.frame_requests() == 1);

  core.frame(id, Timestamp{1});
  CHECK(core.frames_rendered() == 1);
  CHECK(core.set_cursor(id, CursorShape::Text).has_value());  // Ignored by the surface.
  CHECK(core.remove_surface(id).has_value());
}

UI_TEST_CASE("null shell keeps the clipboard in memory and counts wake-ups") {
  NullShell shell(memory());
  CHECK(shell.clipboard_set_text("copied").has_value());
  String text{Allocator<char>(memory())};
  CHECK(shell.clipboard_get_text(text).has_value());
  CHECK(text == "copied");
  shell.wake();
  shell.wake();
  CHECK(shell.wake_count() == 2);
}

TEST_CASE("null surface honors a custom size") {
  const SurfaceInfo info{{300, 200}, ScaleFactor{1.25f}, 0.0f};
  const NullSurface surface(info);
  CHECK(surface.info() == info);
}

TEST_CASE("native surface handles are tagged") {
  int dummy = 0;
  const NativeSurfaceHandle x = NativeSurfaceHandle::xlib(&dummy, 0x1234);
  CHECK(x.kind() == NativeSurfaceKind::Xlib);
  CHECK(x.as_xlib().window == 0x1234);
  CHECK(x.as_xlib().display == &dummy);
  const NativeSurfaceHandle w = NativeSurfaceHandle::wayland(&dummy, &dummy);
  CHECK(w.as_wayland().surface == &dummy);
  CHECK(NativeSurfaceHandle::win32(&dummy, nullptr).kind() == NativeSurfaceKind::Win32);
  CHECK(NativeSurfaceHandle::metal_layer(&dummy).as_metal_layer().layer == &dummy);
  CHECK(NativeSurfaceHandle::web_canvas("#c").as_web_canvas().selector[0] == '#');
  CHECK(NativeSurfaceHandle::none().is_none());
}
