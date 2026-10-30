#include "platform/null_shell.h"

#include "platform/core.h"
#include "support/test_support.h"

using namespace ui::foundation;
using namespace ui::platform;

UI_TEST_CASE("null shell drives a core headlessly") {
  NullShell shell(memory());
  const Logger logger;
  Core core(shell, memory(), logger);
  CHECK(shell.surface_info() == NullShell::kDefaultSurface);
  CHECK(core.surface().info.logical_size() == LogicalSize{800, 600});
  CHECK(shell.frame_requests() == 1);

  core.frame(Timestamp{1});
  CHECK(core.frames_rendered() == 1);

  core.set_cursor(CursorShape::Text);  // No-op, must not crash.
  core.set_ime_cursor_area(LogicalRect::from_xywh(0, 0, 1, 1));
}

UI_TEST_CASE("null shell keeps the clipboard in memory") {
  NullShell shell(memory());
  CHECK(shell.clipboard_set_text("copied").has_value());
  String text{Allocator<char>(memory())};
  CHECK(shell.clipboard_get_text(text).has_value());
  CHECK(text == "copied");
}

UI_TEST_CASE("null shell honors a custom surface") {
  const SurfaceInfo surface{{300, 200}, ScaleFactor{1.25f}};
  NullShell shell(memory(), surface);
  CHECK(shell.surface_info() == surface);
}
