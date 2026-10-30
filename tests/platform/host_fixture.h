#pragma once

// A HostShell + Core pair whose callbacks record everything the core asks of
// the host. Tests drive the core only through the shell's public entry
// points (push_event / tick), exactly as an embedding application would.

#include <string>
#include <string_view>
#include <vector>

#include "platform/core.h"
#include "platform/host_shell.h"
#include "support/test_support.h"

namespace ui::test {

struct HostRecorder {
  int frame_requests = 0;
  std::vector<platform::CursorShape> cursors;
  std::vector<foundation::LogicalRect> ime_areas;
  std::string clipboard;

  [[nodiscard]] platform::HostShellCallbacks callbacks() {
    platform::HostShellCallbacks c;
    c.user_data = this;
    c.request_frame = [](void* self) { ++static_cast<HostRecorder*>(self)->frame_requests; };
    c.set_cursor = [](void* self, platform::CursorShape cursor) {
      static_cast<HostRecorder*>(self)->cursors.push_back(cursor);
    };
    c.set_ime_cursor_area = [](void* self, const foundation::LogicalRect& area) {
      static_cast<HostRecorder*>(self)->ime_areas.push_back(area);
    };
    c.clipboard_get_text = [](void* self, foundation::String& out) -> foundation::Result<void> {
      out.assign(static_cast<HostRecorder*>(self)->clipboard);
      return {};
    };
    c.clipboard_set_text = [](void* self, std::string_view text) -> foundation::Result<void> {
      static_cast<HostRecorder*>(self)->clipboard.assign(text);
      return {};
    };
    return c;
  }
};

inline constexpr platform::SurfaceInfo kTestSurface{{1000.0f, 500.0f},
                                                    foundation::ScaleFactor{2.0f}};

// Owns recorder, shell, logger and core, in construction order.
struct HostHarness {
  explicit HostHarness(foundation::MemoryResource& memory,
                       const platform::SurfaceInfo& surface = kTestSurface)
      : shell(surface, recorder.callbacks()), core(shell, memory, log.logger()) {}

  HostRecorder recorder;
  LogCapture log;
  platform::HostShell shell;
  platform::Core core;

  // Advances the shell clock and delivers a frame tick.
  void tick() {
    now_ns += 16'666'667;
    shell.tick(foundation::Timestamp{now_ns});
  }
  [[nodiscard]] foundation::Timestamp now() const { return foundation::Timestamp{now_ns}; }

  std::int64_t now_ns = 0;
};

}  // namespace ui::test
