#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "platform/platform.h"
#include "platform/surface.h"

namespace ui::platform {

// A headless platform for tests and tools that need no window: clipboard kept
// in memory, wake-ups counted.
class NullShell final : public Platform {
 public:
  explicit NullShell(foundation::MemoryResource& memory) noexcept;

  [[nodiscard]] std::uint64_t wake_count() const noexcept {
    return wakes_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out) override;
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text) override;
  void wake() noexcept override { wakes_.fetch_add(1, std::memory_order_relaxed); }

 private:
  foundation::String clipboard_;
  std::atomic<std::uint64_t> wakes_{0};
};

// A headless surface: fixed size, no native surface (headless rendering
// draws into offscreen textures instead), cursor and IME calls ignored.
// Frame requests are counted; the owner decides when to call Core::frame().
class NullSurface final : public Surface {
 public:
  static constexpr SurfaceInfo kDefaultInfo{{800.0f, 600.0f}, foundation::ScaleFactor{1.0f}, 60.0f};

  explicit NullSurface(const SurfaceInfo& info = kDefaultInfo) noexcept : info_(info) {}

  [[nodiscard]] std::uint64_t frame_requests() const noexcept { return frame_requests_; }

  [[nodiscard]] SurfaceInfo info() const override { return info_; }
  [[nodiscard]] NativeSurfaceHandle native_handle() const override {
    return NativeSurfaceHandle::none();
  }
  void request_frame() override { ++frame_requests_; }
  void set_cursor(CursorShape /*cursor*/) override {}
  void set_ime_cursor_area(const foundation::LogicalRect& /*area*/) override {}

 private:
  SurfaceInfo info_;
  std::uint64_t frame_requests_ = 0;
};

}  // namespace ui::platform
