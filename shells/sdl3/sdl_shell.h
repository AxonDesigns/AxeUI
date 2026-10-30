#pragma once

// SDL3 desktop shell: implements the platform contract (Platform + Surface)
// for Windows, macOS and Linux (X11 and Wayland). SDL types never leave this
// directory.

#include <cstdint>
#include <string_view>

#include "foundation/allocator.h"
#include "foundation/logging.h"
#include "foundation/result.h"
#include "foundation/timestamp.h"
#include "platform/core.h"
#include "platform/platform.h"
#include "platform/surface.h"

struct SDL_Window;
struct SDL_Cursor;
union SDL_Event;

namespace ui::sdl3 {

struct WindowDesc {
  const char* title = "AxeUI";
  int width = 800;  // Logical size at creation (SDL window coordinates).
  int height = 600;
  bool resizable = true;
};

class SdlShell;

// One desktop window: a Surface whose native handle the renderer uses.
class SdlWindow final : public platform::Surface {
 public:
  SdlWindow(SdlShell& shell, SDL_Window* window, void* metal_view) noexcept;
  ~SdlWindow() override;

  [[nodiscard]] platform::SurfaceId id() const noexcept { return id_; }
  [[nodiscard]] SDL_Window* sdl_window() const noexcept { return window_; }
  [[nodiscard]] std::uint32_t sdl_window_id() const noexcept { return window_id_; }

  void set_title(const char* title);

  // Re-reads size, scale and refresh rate from SDL and dispatches
  // ScaleFactorChanged / Resize for what changed.
  void update_info(foundation::Timestamp time);

  // Converts SDL window coordinates to logical pixels.
  [[nodiscard]] foundation::LogicalVec2 to_logical(float x, float y) const noexcept;

  // Frame scheduling state, owned by the shell's loop.
  [[nodiscard]] bool frame_pending() const noexcept { return frame_pending_; }

  // Surface implementation.
  [[nodiscard]] platform::SurfaceInfo info() const override { return info_; }
  [[nodiscard]] platform::NativeSurfaceHandle native_handle() const override;
  void request_frame() override;
  void set_cursor(platform::CursorShape cursor) override;
  void set_ime_cursor_area(const foundation::LogicalRect& area) override;
  void attach(platform::Core* core, platform::SurfaceId id) noexcept override;

 private:
  friend class SdlShell;

  [[nodiscard]] platform::SurfaceInfo query_info() const;

  SdlShell* shell_;
  SDL_Window* window_;
  void* metal_view_;  // SDL_MetalView on macOS, else null.
  std::uint32_t window_id_;
  platform::Core* core_ = nullptr;
  platform::SurfaceId id_;
  platform::SurfaceInfo info_;
  bool minimized_ = false;
  bool composing_ = false;  // IME composition in progress.
  bool frame_pending_ = false;
  std::int64_t next_frame_ns_ = 0;  // Earliest time for the next frame (pacing).
};

// The process-wide part: owns SDL, the windows and the event loop.
//
// The application keeps the loop (`while (shell.pump()) { ... }`); pump()
// sleeps in SDL's event wait while no window asked for a frame, so an idle
// application uses no CPU. Frames are paced per window from its display's
// refresh rate. Everything runs on the main thread (SDL requires it on
// macOS); only wake() may be called from other threads.
class SdlShell final : public platform::Platform {
 public:
  [[nodiscard]] static foundation::Result<foundation::UniquePtr<SdlShell>> create(
      foundation::MemoryResource& memory, const foundation::Logger& logger);

  SdlShell(foundation::MemoryResource& memory, const foundation::Logger& logger) noexcept;
  ~SdlShell() override;

  // Creates a window and registers it with the attached core.
  [[nodiscard]] foundation::Result<SdlWindow*> create_window(const WindowDesc& desc);
  // Unregisters and destroys a window. Must not be called during dispatch.
  void destroy_window(SdlWindow* window);
  [[nodiscard]] SdlWindow* find_window(platform::SurfaceId id) const noexcept;
  [[nodiscard]] std::size_t window_count() const noexcept { return windows_.size(); }

  // One loop iteration: waits for events (or the next frame deadline),
  // dispatches them to the core, then delivers due frames. Returns false
  // once the application should quit (SDL_EVENT_QUIT or request_quit()).
  bool pump();
  void request_quit() noexcept { quit_ = true; }

  // Name of SDL's video driver ("x11", "wayland", "windows", "cocoa").
  [[nodiscard]] std::string_view video_driver() const noexcept;

  // Platform implementation.
  [[nodiscard]] foundation::Result<void> clipboard_get_text(foundation::String& out) override;
  [[nodiscard]] foundation::Result<void> clipboard_set_text(std::string_view text) override;
  void wake() noexcept override;
  void attach_core(platform::Core* core) noexcept override { core_ = core; }

 private:
  friend class SdlWindow;

  [[nodiscard]] foundation::Result<void> initialize();
  void handle_event(const SDL_Event& event);
  void handle_window_event(SdlWindow& window, const SDL_Event& event, foundation::Timestamp time);
  void deliver_due_frames(std::int64_t now_ns);
  void deliver_frame(SdlWindow& window, std::int64_t now_ns);
  [[nodiscard]] SdlWindow* find_by_sdl_id(std::uint32_t window_id) const noexcept;
  void apply_cursor(platform::CursorShape cursor);
  static bool event_watch(void* user_data, SDL_Event* event);

  foundation::MemoryResource* memory_;
  const foundation::Logger* logger_;
  platform::Core* core_ = nullptr;
  foundation::Vector<foundation::UniquePtr<SdlWindow>> windows_;
  foundation::String clipboard_scratch_;
  SDL_Cursor* cursors_[17] = {};
  std::uint32_t wake_event_ = 0;
  bool initialized_ = false;
  bool quit_ = false;
  bool cursor_hidden_ = false;
  std::uint16_t mouse_buttons_ = 0;  // Held mouse buttons (PointerButton bits).
  // Pen state (SDL reports pressure and buttons separately from motion).
  float pen_pressure_ = 0.0f;
  bool pen_eraser_ = false;
  bool pen_down_ = false;
  std::uint16_t pen_buttons_ = 0;
};

}  // namespace ui::sdl3
