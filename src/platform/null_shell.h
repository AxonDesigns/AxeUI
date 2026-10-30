#pragma once

#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "platform/shell.h"

namespace ui::platform {

// A headless shell for tests and tools that need no window: fixed surface,
// cursor and IME calls ignored, clipboard kept in memory. Frame requests are
// counted; the owner decides when to call Core::frame().
class NullShell final : public Shell {
 public:
  static constexpr SurfaceInfo kDefaultSurface{{800.0f, 600.0f}, foundation::ScaleFactor{1.0f}};

  explicit NullShell(foundation::MemoryResource& memory,
                     const SurfaceInfo& surface = kDefaultSurface) noexcept;

  [[nodiscard]] std::uint64_t frame_requests() const noexcept { return frame_requests_; }

  [[nodiscard]] SurfaceInfo surface_info() const override { return surface_; }
  void request_frame() override { ++frame_requests_; }
  void set_cursor(CursorShape /*cursor*/) override {}
  void set_ime_cursor_area(const foundation::LogicalRect& /*area*/) override {}
  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out) override;
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text) override;

 private:
  SurfaceInfo surface_;
  foundation::String clipboard_;
  std::uint64_t frame_requests_ = 0;
};

}  // namespace ui::platform
