#pragma once

// A HostShell + Core with HostSurfaces whose callbacks record everything the
// core asks of the host. Tests drive the core only through the shell's
// public entry points (push_event / tick / set_info), as an embedder would.

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "platform/core.h"
#include "platform/host_shell.h"
#include "support/test_support.h"

namespace ui::test {

struct SurfaceRecorder {
  int frame_requests = 0;
  std::vector<platform::CursorShape> cursors;
  std::vector<foundation::LogicalRect> ime_areas;

  [[nodiscard]] platform::HostSurfaceCallbacks callbacks() {
    platform::HostSurfaceCallbacks c;
    c.user_data = this;
    c.request_frame = [](void* self) { ++static_cast<SurfaceRecorder*>(self)->frame_requests; };
    c.set_cursor = [](void* self, platform::CursorShape cursor) {
      static_cast<SurfaceRecorder*>(self)->cursors.push_back(cursor);
    };
    c.set_ime_cursor_area = [](void* self, const foundation::LogicalRect& area) {
      static_cast<SurfaceRecorder*>(self)->ime_areas.push_back(area);
    };
    return c;
  }
};

struct PlatformRecorder {
  std::string clipboard;
  int wakes = 0;

  [[nodiscard]] platform::HostShellCallbacks callbacks() {
    platform::HostShellCallbacks c;
    c.user_data = this;
    c.clipboard_get_text = [](void* self, foundation::String& out) -> foundation::Result<void> {
      out.assign(static_cast<PlatformRecorder*>(self)->clipboard);
      return {};
    };
    c.clipboard_set_text = [](void* self, std::string_view text) -> foundation::Result<void> {
      static_cast<PlatformRecorder*>(self)->clipboard.assign(text);
      return {};
    };
    c.wake = [](void* self) { ++static_cast<PlatformRecorder*>(self)->wakes; };
    return c;
  }
};

inline constexpr platform::SurfaceInfo kTestSurface{
    {1000.0f, 500.0f}, foundation::ScaleFactor{2.0f}, 60.0f};

// A HostSurface plus its recorder, registered with the harness core.
struct TestWindow {
  explicit TestWindow(const platform::SurfaceInfo& info)
      : surface(info, platform::NativeSurfaceHandle::none(), recorder.callbacks()) {}

  SurfaceRecorder recorder;
  platform::HostSurface surface;
};

// Owns recorders, shell, logger, core and windows, in construction order.
// Windows are removed from the core before destruction.
struct HostHarness {
  explicit HostHarness(foundation::MemoryResource& memory)
      : shell(platform_recorder.callbacks()), core(shell, memory, log.logger()) {}

  ~HostHarness() {
    for (TestWindow& w : windows) {
      if (!w.surface.id().is_null()) {
        (void)core.remove_surface(w.surface.id());
      }
    }
  }

  // Creates and registers a window; returns its id.
  platform::SurfaceId open(const platform::SurfaceInfo& info = kTestSurface) {
    TestWindow& w = windows.emplace_back(info);
    return *core.add_surface(w.surface);
  }

  [[nodiscard]] TestWindow& window(std::size_t index) { return windows[index]; }

  // Advances the shell clock and delivers a frame tick to `id`.
  void tick(platform::SurfaceId id) {
    now_ns += 16'666'667;
    shell.tick(id, foundation::Timestamp{now_ns});
  }
  [[nodiscard]] foundation::Timestamp now() const { return foundation::Timestamp{now_ns}; }

  [[nodiscard]] const platform::SurfaceState& state(platform::SurfaceId id) const {
    return **core.surface(id);
  }

  PlatformRecorder platform_recorder;
  LogCapture log;
  platform::HostShell shell;
  platform::Core core;
  std::deque<TestWindow> windows;  // Stable addresses.
  std::int64_t now_ns = 0;
};

}  // namespace ui::test
