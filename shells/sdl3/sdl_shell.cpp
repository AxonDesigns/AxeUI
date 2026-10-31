#include "sdl3/sdl_shell.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "sdl3/sdl_keys.h"

namespace ui::sdl3 {

using foundation::Error;
using foundation::ErrorCode;
using foundation::LogLevel;
using foundation::Timestamp;
using platform::Event;
using platform::EventKind;
using platform::PointerButton;
using platform::PointerButtons;
using platform::PointerEvent;
using platform::PointerId;
using platform::PointerKind;

namespace {

constexpr std::int64_t kDefaultFrameNs = 16'666'667;  // 60 Hz when the refresh rate is unknown.
// A frame is due this close to its scheduled time. SDL waits in whole
// milliseconds; waking up to 1 ms early is accepted instead of spinning.
constexpr std::int64_t kFrameSlackNs = 1'000'000;
// Pointer id namespaces, so mouse, touch and pen ids never collide.
constexpr std::uint64_t kTouchIdBase = 1ull << 62;
constexpr std::uint64_t kPenIdBase = 2ull << 62;

Timestamp sdl_time(std::uint64_t ns) noexcept { return Timestamp{static_cast<std::int64_t>(ns)}; }

std::int64_t now_ns() noexcept { return static_cast<std::int64_t>(SDL_GetTicksNS()); }

PointerButton mouse_button(std::uint8_t button) noexcept {
  switch (button) {
    case SDL_BUTTON_LEFT:
      return PointerButton::Primary;
    case SDL_BUTTON_RIGHT:
      return PointerButton::Secondary;
    case SDL_BUTTON_MIDDLE:
      return PointerButton::Middle;
    case SDL_BUTTON_X1:
      return PointerButton::Back;
    case SDL_BUTTON_X2:
      return PointerButton::Forward;
    default:
      return PointerButton::None;
  }
}

PointerButtons mouse_buttons(SDL_MouseButtonFlags state) noexcept {
  PointerButtons buttons;
  if ((state & SDL_BUTTON_LMASK) != 0) buttons.set(PointerButton::Primary);
  if ((state & SDL_BUTTON_RMASK) != 0) buttons.set(PointerButton::Secondary);
  if ((state & SDL_BUTTON_MMASK) != 0) buttons.set(PointerButton::Middle);
  if ((state & SDL_BUTTON_X1MASK) != 0) buttons.set(PointerButton::Back);
  if ((state & SDL_BUTTON_X2MASK) != 0) buttons.set(PointerButton::Forward);
  return buttons;
}

SDL_SystemCursor system_cursor(platform::CursorShape shape) noexcept {
  using platform::CursorShape;
  switch (shape) {
    case CursorShape::Text:
      return SDL_SYSTEM_CURSOR_TEXT;
    case CursorShape::Pointer:
      return SDL_SYSTEM_CURSOR_POINTER;
    case CursorShape::Crosshair:
      return SDL_SYSTEM_CURSOR_CROSSHAIR;
    case CursorShape::Move:
    case CursorShape::Grab:  // SDL has no grab cursors.
    case CursorShape::Grabbing:
      return SDL_SYSTEM_CURSOR_MOVE;
    case CursorShape::NotAllowed:
      return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
    case CursorShape::Wait:
      return SDL_SYSTEM_CURSOR_WAIT;
    case CursorShape::Progress:
      return SDL_SYSTEM_CURSOR_PROGRESS;
    case CursorShape::ResizeEW:
    case CursorShape::ResizeColumn:
      return SDL_SYSTEM_CURSOR_EW_RESIZE;
    case CursorShape::ResizeNS:
    case CursorShape::ResizeRow:
      return SDL_SYSTEM_CURSOR_NS_RESIZE;
    case CursorShape::ResizeNESW:
      return SDL_SYSTEM_CURSOR_NESW_RESIZE;
    case CursorShape::ResizeNWSE:
      return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
    case CursorShape::Default:
    case CursorShape::Hidden:
      break;
  }
  return SDL_SYSTEM_CURSOR_DEFAULT;
}

// Converts an SDL_TextEditingEvent cursor (in UTF-8 characters) to a byte
// offset into `text`.
std::int32_t char_to_byte_offset(const char* text, std::int32_t chars) noexcept {
  if (chars < 0 || text == nullptr) {
    return -1;
  }
  std::int32_t bytes = 0;
  for (std::int32_t seen = 0; text[bytes] != '\0'; ++bytes) {
    const auto c = static_cast<unsigned char>(text[bytes]);
    if ((c & 0xC0u) != 0x80u) {  // Start of a code point.
      if (seen == chars) {
        return bytes;
      }
      ++seen;
    }
  }
  return bytes;
}

}  // namespace

// --- SdlWindow -------------------------------------------------------------------

SdlWindow::SdlWindow(SdlShell& shell, SDL_Window* window, void* metal_view) noexcept
    : shell_(&shell),
      window_(window),
      metal_view_(metal_view),
      window_id_(SDL_GetWindowID(window)) {
  // In the body, not the initializer list: query_info() reads members that
  // are declared after info_.
  info_ = query_info();
}

SdlWindow::~SdlWindow() {
#if defined(__APPLE__)
  if (metal_view_ != nullptr) {
    SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metal_view_));
  }
#endif
  SDL_DestroyWindow(window_);
}

platform::SurfaceInfo SdlWindow::query_info() const {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window_, &width, &height);
  if (width <= 0 || height <= 0) {
    // Some drivers (Wayland, dummy) learn the pixel size only from the first
    // configure event; estimate it until then. The real size arrives as a
    // Resize.
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    const float density = SDL_GetWindowPixelDensity(window_);
    width =
        static_cast<int>(std::lround(static_cast<float>(w) * (density > 0.0f ? density : 1.0f)));
    height =
        static_cast<int>(std::lround(static_cast<float>(h) * (density > 0.0f ? density : 1.0f)));
  }
  float scale = SDL_GetWindowDisplayScale(window_);
  if (!foundation::ScaleFactor::is_valid(scale)) {
    scale = 1.0f;
  }
  float refresh = 0.0f;
  if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_))) {
    refresh = mode->refresh_rate;
  }
  if (minimized_ || occluded_) {
    // A minimized or hidden window has nothing to draw; a zero size stops the
    // core from requesting frames. Presenting to an invisible surface wastes
    // GPU time (Mailbox) or gets throttled by the compositor (Fifo: ~30 Hz on
    // KWin), which would slow every window on the same UI thread.
    width = 0;
    height = 0;
  }
  return platform::SurfaceInfo{{static_cast<float>(width), static_cast<float>(height)},
                               foundation::ScaleFactor{scale},
                               refresh};
}

void SdlWindow::update_info(Timestamp time) {
  const platform::SurfaceInfo next = query_info();
  const bool scale_changed =
      next.scale != info_.scale || next.refresh_rate_hz != info_.refresh_rate_hz;
  const bool size_changed = next.physical_size != info_.physical_size;
  info_ = next;
  if (core_ == nullptr) {
    return;
  }
  if (scale_changed) {
    core_->dispatch(Event::scale_factor_changed(id_, time, next));
  }
  if (size_changed) {
    core_->dispatch(Event::resize(id_, time, next));
  }
}

foundation::LogicalVec2 SdlWindow::to_logical(float x, float y) const noexcept {
  // SDL window coordinates -> physical pixels -> logical pixels.
  const float density = SDL_GetWindowPixelDensity(window_);
  const float factor = (density > 0.0f ? density : 1.0f) / info_.scale.value();
  return {x * factor, y * factor};
}

platform::NativeSurfaceHandle SdlWindow::native_handle() const {
  const SDL_PropertiesID props = SDL_GetWindowProperties(window_);
  const std::string_view driver = shell_->video_driver();
  if (driver == "windows") {
    return platform::NativeSurfaceHandle::win32(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr),
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr));
  }
  if (driver == "cocoa") {
#if defined(__APPLE__)
    return platform::NativeSurfaceHandle::metal_layer(
        SDL_Metal_GetLayer(static_cast<SDL_MetalView>(metal_view_)));
#endif
  }
  if (driver == "x11") {
    return platform::NativeSurfaceHandle::xlib(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr),
        static_cast<std::uint64_t>(
            SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
  }
  if (driver == "wayland") {
    return platform::NativeSurfaceHandle::wayland(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr),
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
  }
  return platform::NativeSurfaceHandle::none();
}

void SdlWindow::request_frame() { frame_pending_ = true; }

void SdlWindow::set_cursor(platform::CursorShape cursor) { shell_->apply_cursor(cursor); }

void SdlWindow::set_ime_cursor_area(const foundation::LogicalRect& area) {
  // Logical pixels -> SDL window coordinates.
  const float density = SDL_GetWindowPixelDensity(window_);
  const float factor = info_.scale.value() / (density > 0.0f ? density : 1.0f);
  const SDL_Rect rect{static_cast<int>(std::lround(area.left * factor)),
                      static_cast<int>(std::lround(area.top * factor)),
                      static_cast<int>(std::lround(area.width() * factor)),
                      static_cast<int>(std::lround(area.height() * factor))};
  SDL_SetTextInputArea(window_, &rect, 0);
}

void SdlWindow::attach(platform::Core* core, platform::SurfaceId id) noexcept {
  core_ = core;
  id_ = id;
}

void SdlWindow::set_title(const char* title) { SDL_SetWindowTitle(window_, title); }

float SdlWindow::logical_to_window_factor() const noexcept {
  const float density = SDL_GetWindowPixelDensity(window_);
  float scale = SDL_GetWindowDisplayScale(window_);
  if (!foundation::ScaleFactor::is_valid(scale)) {
    scale = 1.0f;
  }
  return scale / (density > 0.0f ? density : 1.0f);
}

void SdlWindow::set_size(float width, float height) {
  const float factor = logical_to_window_factor();
  SDL_SetWindowSize(window_, static_cast<int>(std::lround(width * factor)),
                    static_cast<int>(std::lround(height * factor)));
}

void SdlWindow::set_position(int x, int y) { SDL_SetWindowPosition(window_, x, y); }

void SdlWindow::position(int& x, int& y) const {
  x = 0;
  y = 0;
  SDL_GetWindowPosition(window_, &x, &y);
}

// --- SdlShell ----------------------------------------------------------------------

foundation::Result<foundation::UniquePtr<SdlShell>> SdlShell::create(
    foundation::MemoryResource& memory, const foundation::Logger& logger) {
  auto shell = foundation::allocate_unique<SdlShell>(memory, memory, logger);
  if (!shell) {
    return foundation::fail(std::move(shell).error());
  }
  UI_TRY((*shell)->initialize());
  return std::move(shell).value();
}

SdlShell::SdlShell(foundation::MemoryResource& memory, const foundation::Logger& logger) noexcept
    : memory_(&memory),
      logger_(&logger),
      windows_(foundation::Allocator<foundation::UniquePtr<SdlWindow>>(memory)),
      clipboard_scratch_(foundation::Allocator<char>(memory)) {}

foundation::Result<void> SdlShell::initialize() {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    UI_LOGF(*logger_, LogLevel::Error, ErrorCode::Unavailable, "SDL_Init failed: %s",
            SDL_GetError());
    return foundation::fail(Error{ErrorCode::Unavailable, "SDL video initialization failed"});
  }
  initialized_ = true;
  if (!SDL_IsMainThread()) {
    return foundation::fail(
        Error{ErrorCode::WrongThread, "the SDL shell must run on the main thread"});
  }
  wake_event_ = SDL_RegisterEvents(1);
  SDL_AddEventWatch(&SdlShell::event_watch, this);
  UI_LOGF(*logger_, LogLevel::Info, ErrorCode::Ok, "SDL video driver: %s",
          SDL_GetCurrentVideoDriver());
  return {};
}

SdlShell::~SdlShell() {
  // Windows still registered with a live core are detached first.
  while (!windows_.empty()) {
    destroy_window(windows_.back().get());
  }
  if (initialized_) {
    SDL_RemoveEventWatch(&SdlShell::event_watch, this);
    for (SDL_Cursor* cursor : cursors_) {
      if (cursor != nullptr) {
        SDL_DestroyCursor(cursor);
      }
    }
    SDL_Quit();
  }
}

std::string_view SdlShell::video_driver() const noexcept {
  const char* driver = SDL_GetCurrentVideoDriver();
  return driver != nullptr ? std::string_view(driver) : std::string_view();
}

foundation::Result<SdlWindow*> SdlShell::create_window(const WindowDesc& desc) {
  UI_ASSERT(SDL_IsMainThread(), "windows must be created on the main thread");
  if (core_ == nullptr) {
    return foundation::fail(Error{ErrorCode::InvalidState, "create a Core for this shell first"});
  }
  SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (desc.resizable) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  // Metal views exist only with the Cocoa driver (not, e.g., SDL's dummy
  // driver used for headless tests).
  const bool use_metal = video_driver() == "cocoa";
  if (use_metal) {
    flags |= SDL_WINDOW_METAL;
  }
  SDL_Window* window = SDL_CreateWindow(desc.title, desc.width, desc.height, flags);
  if (window == nullptr) {
    UI_LOGF(*logger_, LogLevel::Error, ErrorCode::Unavailable, "SDL_CreateWindow failed: %s",
            SDL_GetError());
    return foundation::fail(Error{ErrorCode::Unavailable, "window creation failed"});
  }
  void* metal_view = nullptr;
#if defined(__APPLE__)
  if (use_metal) {
    // A CAMetalLayer-backed view; WebGPU (Metal) presents into its layer.
    metal_view = SDL_Metal_CreateView(window);
    if (metal_view == nullptr) {
      SDL_DestroyWindow(window);
      return foundation::fail(Error{ErrorCode::Unavailable, "creating the Metal view failed"});
    }
  }
#endif
  auto created = foundation::allocate_unique<SdlWindow>(*memory_, *this, window, metal_view);
  if (!created) {
    SDL_DestroyWindow(window);
    return foundation::fail(std::move(created).error());
  }
  SdlWindow* result = created->get();
  windows_.push_back(std::move(created).value());
  // The requested size is in logical pixels. Where SDL window coordinates
  // are pixels (X11, Windows), the window was created too small for the
  // display's content scale: correct it now that the display is known.
  if (const float factor = result->logical_to_window_factor(); std::fabs(factor - 1.0f) > 0.01f) {
    result->set_size(static_cast<float>(desc.width), static_cast<float>(desc.height));
    // X11 applies the size asynchronously; wait so the first frame uses it.
    SDL_SyncWindow(window);
    result->info_ = result->query_info();
  }
  SDL_StartTextInput(window);  // Per-window text input and IME.
  if (auto id = core_->add_surface(*result); !id) {
    windows_.pop_back();
    return foundation::fail(std::move(id).error());
  }
  return result;
}

void SdlShell::destroy_window(SdlWindow* window) {
  UI_ASSERT(core_ == nullptr || !core_->in_dispatch(), "destroy windows outside of dispatch");
  if (core_ != nullptr && !window->id().is_null()) {
    // Releases the renderer's surface before the OS window goes away.
    static_cast<void>(core_->remove_surface(window->id()));
  }
  std::erase_if(windows_, [window](const auto& w) { return w.get() == window; });
}

SdlWindow* SdlShell::find_window(platform::SurfaceId id) const noexcept {
  for (const auto& window : windows_) {
    if (window->id() == id) {
      return window.get();
    }
  }
  return nullptr;
}

SdlWindow* SdlShell::find_by_sdl_id(std::uint32_t window_id) const noexcept {
  for (const auto& window : windows_) {
    if (window->sdl_window_id() == window_id) {
      return window.get();
    }
  }
  return nullptr;
}

bool SdlShell::pump() {
  UI_ASSERT(SDL_IsMainThread(), "SdlShell::pump must run on the main thread");
  // Sleep until an event arrives or the earliest pending frame is due.
  std::int64_t deadline = -1;
  for (const auto& window : windows_) {
    if (window->frame_pending_) {
      deadline = deadline < 0 ? window->next_frame_ns_ : std::min(deadline, window->next_frame_ns_);
    }
  }
  std::int32_t timeout_ms = -1;  // Wait indefinitely: nothing to draw.
  if (deadline >= 0) {
    // Round down: the frame counts as due within kFrameSlackNs of its time.
    const std::int64_t wait = std::max<std::int64_t>(0, deadline - now_ns());
    timeout_ms = static_cast<std::int32_t>(wait / 1'000'000);
  }
  SDL_Event event;
  if (SDL_WaitEventTimeout(&event, timeout_ms)) {
    handle_event(event);
    while (SDL_PollEvent(&event)) {
      handle_event(event);
    }
  }
  deliver_due_frames(now_ns());
  return !quit_;
}

void SdlShell::deliver_due_frames(std::int64_t now) {
  // Index loop: a frame may lead to windows being added.
  for (std::size_t i = 0; i < windows_.size(); ++i) {
    SdlWindow& window = *windows_[i];
    if (window.frame_pending_ && now + kFrameSlackNs >= window.next_frame_ns_) {
      deliver_frame(window, now);
    }
  }
}

void SdlShell::deliver_frame(SdlWindow& window, std::int64_t now) {
  window.frame_pending_ = false;
  const float hz = window.info_.refresh_rate_hz;
  const std::int64_t period =
      hz > 1.0f ? static_cast<std::int64_t>(1'000'000'000.0 / static_cast<double>(hz))
                : kDefaultFrameNs;
  // Fixed-rate schedule: advance by exactly one period, so the average frame
  // rate equals the display's refresh rate even when presents never block
  // (Mailbox). After falling more than a period behind (idle, a stall),
  // restart the schedule from now instead of catching up in a burst.
  if (now - window.next_frame_ns_ > period) {
    window.next_frame_ns_ = now;
  }
  window.next_frame_ns_ += period;
  if (core_ != nullptr && !window.id().is_null()) {
    core_->frame(window.id(), Timestamp{now});
  }
}

bool SdlShell::event_watch(void* user_data, SDL_Event* event) {
  // During the Windows modal move/resize loop (and macOS live resize) the
  // application's loop does not run; SDL keeps sending live-resize expose
  // events to watchers, so frames are produced from here.
  if (event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == 1) {
    auto* self = static_cast<SdlShell*>(user_data);
    if (self->core_ == nullptr || self->core_->in_dispatch()) {
      return true;
    }
    if (SdlWindow* window = self->find_by_sdl_id(event->window.windowID)) {
      window->update_info(sdl_time(event->window.timestamp));
      static_cast<void>(self->core_->request_redraw(window->id()));
      if (window->frame_pending_) {
        self->deliver_frame(*window, now_ns());
      }
    }
  }
  return true;
}

void SdlShell::apply_cursor(platform::CursorShape cursor) {
  if (cursor == platform::CursorShape::Hidden) {
    SDL_HideCursor();
    cursor_hidden_ = true;
    return;
  }
  if (cursor_hidden_) {
    SDL_ShowCursor();
    cursor_hidden_ = false;
  }
  const SDL_SystemCursor id = system_cursor(cursor);
  const auto index = static_cast<std::size_t>(id);
  if (index < std::size(cursors_)) {
    if (cursors_[index] == nullptr) {
      cursors_[index] = SDL_CreateSystemCursor(id);
    }
    SDL_SetCursor(cursors_[index]);
  }
}

foundation::Result<void> SdlShell::clipboard_get_text(foundation::String& out) {
  char* text = SDL_GetClipboardText();
  if (text == nullptr) {
    return foundation::fail(Error{ErrorCode::Unavailable, "clipboard is unavailable"});
  }
  out.assign(text);
  SDL_free(text);
  return {};
}

foundation::Result<void> SdlShell::clipboard_set_text(std::string_view text) {
  clipboard_scratch_.assign(text);  // SDL needs a NUL-terminated string.
  if (!SDL_SetClipboardText(clipboard_scratch_.c_str())) {
    return foundation::fail(Error{ErrorCode::Unavailable, "setting the clipboard failed"});
  }
  return {};
}

void SdlShell::wake() noexcept {
  // SDL_PushEvent is thread-safe; the event only interrupts SDL_WaitEvent.
  SDL_Event event{};
  event.type = wake_event_;
  SDL_PushEvent(&event);
}

// --- Event translation ---------------------------------------------------------------

void SdlShell::handle_window_event(SdlWindow& window, const SDL_Event& event, Timestamp time) {
  const platform::SurfaceId id = window.id();
  switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
      window.update_info(time);
      break;
    case SDL_EVENT_WINDOW_MINIMIZED:
      window.minimized_ = true;
      window.update_info(time);
      break;
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
      window.minimized_ = false;
      window.update_info(time);
      break;
    case SDL_EVENT_WINDOW_OCCLUDED:
    case SDL_EVENT_WINDOW_HIDDEN:
      window.occluded_ = true;
      window.update_info(time);
      break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_SHOWN:
      if (window.occluded_) {
        window.occluded_ = false;
        window.update_info(time);  // Restores the real size: a Resize.
      }
      static_cast<void>(core_->request_redraw(id));
      break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      core_->dispatch(Event::surface_event(EventKind::FocusGained, id, time));
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      core_->dispatch(Event::surface_event(EventKind::FocusLost, id, time));
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      core_->dispatch(Event::surface_event(EventKind::CloseRequested, id, time));
      break;
    default:
      break;
  }
}

void SdlShell::handle_event(const SDL_Event& event) {
  if (event.type == wake_event_) {
    return;  // Only interrupts the wait.
  }
  if (event.type == SDL_EVENT_QUIT) {
    quit_ = true;
    return;
  }
  if (core_ == nullptr) {
    return;
  }
  if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND ||
      event.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
    core_->dispatch(Event::app_event(EventKind::Suspend, sdl_time(event.common.timestamp)));
    return;
  }
  if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
    core_->dispatch(Event::app_event(EventKind::Resume, sdl_time(event.common.timestamp)));
    return;
  }

  if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
    if (SdlWindow* window = find_by_sdl_id(event.window.windowID)) {
      handle_window_event(*window, event, sdl_time(event.window.timestamp));
    }
    return;
  }

  switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
      if (event.motion.which == SDL_TOUCH_MOUSEID || event.motion.which == SDL_PEN_MOUSEID) {
        return;  // Synthesized from touch/pen events, which are handled directly.
      }
      SdlWindow* window = find_by_sdl_id(event.motion.windowID);
      if (window == nullptr) return;
      PointerEvent e;
      e.id = PointerId{0};
      e.kind = PointerKind::Mouse;
      e.position = window->to_logical(event.motion.x, event.motion.y);
      e.buttons = mouse_buttons(event.motion.state);
      e.modifiers = to_modifiers(SDL_GetModState());
      core_->dispatch(Event::pointer_move(window->id(), sdl_time(event.motion.timestamp), e));
      return;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      if (event.button.which == SDL_TOUCH_MOUSEID || event.button.which == SDL_PEN_MOUSEID) {
        return;
      }
      SdlWindow* window = find_by_sdl_id(event.button.windowID);
      if (window == nullptr) return;
      PointerEvent e;
      e.id = PointerId{0};
      e.kind = PointerKind::Mouse;
      e.position = window->to_logical(event.button.x, event.button.y);
      e.button = mouse_button(event.button.button);
      // Track held buttons from the events themselves: SDL_GetMouseState()
      // reports the state now, not when this event happened.
      const auto bit = static_cast<std::uint16_t>(e.button);
      mouse_buttons_ = event.button.down ? static_cast<std::uint16_t>(mouse_buttons_ | bit)
                                         : static_cast<std::uint16_t>(mouse_buttons_ & ~bit);
      e.buttons = PointerButtons::from_bits(mouse_buttons_);
      e.modifiers = to_modifiers(SDL_GetModState());
      e.pressure = event.button.down ? 0.5f : 0.0f;
      const Timestamp time = sdl_time(event.button.timestamp);
      core_->dispatch(event.button.down ? Event::pointer_down(window->id(), time, e)
                                        : Event::pointer_up(window->id(), time, e));
      return;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      SdlWindow* window = find_by_sdl_id(event.wheel.windowID);
      if (window == nullptr) return;
      // SDL reports wheel movement in (fractional) notches, positive y away
      // from the user. The engine uses lines, positive y scrolling down.
      const float flip = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
      platform::ScrollEvent e;
      e.position = window->to_logical(event.wheel.mouse_x, event.wheel.mouse_y);
      e.unit = platform::ScrollUnit::Lines;
      e.delta_x = event.wheel.x * flip;
      e.delta_y = -event.wheel.y * flip;
      e.modifiers = to_modifiers(SDL_GetModState());
      core_->dispatch(Event::scroll(window->id(), sdl_time(event.wheel.timestamp), e));
      return;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED: {
      SdlWindow* window = find_by_sdl_id(event.tfinger.windowID);
      if (window == nullptr) return;
      // Finger coordinates are normalized to the window.
      const platform::SurfaceInfo& info = window->info();
      PointerEvent e;
      e.id = PointerId{kTouchIdBase | static_cast<std::uint64_t>(event.tfinger.fingerID)};
      e.kind = PointerKind::Touch;
      e.position = foundation::to_logical(
          foundation::PhysicalVec2{event.tfinger.x * info.physical_size.width,
                                   event.tfinger.y * info.physical_size.height},
          info.scale);
      e.pressure = event.tfinger.pressure;
      const bool lifting =
          event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
      e.buttons = lifting ? PointerButtons{} : PointerButtons{PointerButton::Primary};
      e.button =
          event.type == SDL_EVENT_FINGER_MOTION ? PointerButton::None : PointerButton::Primary;
      const Timestamp time = sdl_time(event.tfinger.timestamp);
      const platform::SurfaceId id = window->id();
      switch (event.type) {
        case SDL_EVENT_FINGER_DOWN:
          core_->dispatch(Event::pointer_down(id, time, e));
          break;
        case SDL_EVENT_FINGER_UP:
          core_->dispatch(Event::pointer_up(id, time, e));
          break;
        case SDL_EVENT_FINGER_MOTION:
          core_->dispatch(Event::pointer_move(id, time, e));
          break;
        default:
          core_->dispatch(Event::pointer_cancel(id, time, e));
          break;
      }
      return;
    }
    case SDL_EVENT_PEN_AXIS:
      if (event.paxis.axis == SDL_PEN_AXIS_PRESSURE) {
        pen_pressure_ = event.paxis.value;
      }
      return;
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP: {
      // The pen events share window/which/x/y in the same layout.
      std::uint32_t window_id = 0;
      std::uint64_t which = 0;
      float x = 0.0f;
      float y = 0.0f;
      std::uint64_t timestamp = 0;
      PointerButton changed = PointerButton::None;
      if (event.type == SDL_EVENT_PEN_DOWN || event.type == SDL_EVENT_PEN_UP) {
        window_id = event.ptouch.windowID;
        which = event.ptouch.which;
        x = event.ptouch.x;
        y = event.ptouch.y;
        timestamp = event.ptouch.timestamp;
        pen_down_ = event.ptouch.down;
        pen_eraser_ = event.ptouch.eraser;
        changed = pen_eraser_ ? PointerButton::Eraser : PointerButton::Primary;
      } else if (event.type == SDL_EVENT_PEN_MOTION) {
        window_id = event.pmotion.windowID;
        which = event.pmotion.which;
        x = event.pmotion.x;
        y = event.pmotion.y;
        timestamp = event.pmotion.timestamp;
      } else {
        window_id = event.pbutton.windowID;
        which = event.pbutton.which;
        x = event.pbutton.x;
        y = event.pbutton.y;
        timestamp = event.pbutton.timestamp;
        changed = event.pbutton.button == 1 ? PointerButton::Secondary : PointerButton::Middle;
        const auto bit = static_cast<std::uint16_t>(changed);
        pen_buttons_ = event.pbutton.down ? static_cast<std::uint16_t>(pen_buttons_ | bit)
                                          : static_cast<std::uint16_t>(pen_buttons_ & ~bit);
      }
      SdlWindow* window = find_by_sdl_id(window_id);
      if (window == nullptr) return;
      PointerEvent e;
      e.id = PointerId{kPenIdBase | which};
      e.kind = PointerKind::Pen;
      e.position = window->to_logical(x, y);
      e.button = changed;
      e.buttons = PointerButtons::from_bits(pen_buttons_);
      if (pen_down_) {
        e.buttons.set(pen_eraser_ ? PointerButton::Eraser : PointerButton::Primary);
      }
      e.pressure = pen_pressure_;
      e.modifiers = to_modifiers(SDL_GetModState());
      const Timestamp time = sdl_time(timestamp);
      const bool down =
          event.type == SDL_EVENT_PEN_DOWN || (event.type == SDL_EVENT_PEN_BUTTON_DOWN);
      const bool up = event.type == SDL_EVENT_PEN_UP || event.type == SDL_EVENT_PEN_BUTTON_UP;
      if (down) {
        core_->dispatch(Event::pointer_down(window->id(), time, e));
      } else if (up) {
        core_->dispatch(Event::pointer_up(window->id(), time, e));
      } else {
        core_->dispatch(Event::pointer_move(window->id(), time, e));
      }
      return;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      SdlWindow* window = find_by_sdl_id(event.key.windowID);
      if (window == nullptr) return;
      platform::KeyEvent e;
      e.physical = to_physical_key(event.key.scancode);
      e.logical = to_logical_key(event.key.key);
      e.modifiers = to_modifiers(event.key.mod);
      e.repeat = event.key.repeat;
      const Timestamp time = sdl_time(event.key.timestamp);
      core_->dispatch(event.key.down ? Event::key_down(window->id(), time, e)
                                     : Event::key_up(window->id(), time, e));
      return;
    }
    case SDL_EVENT_TEXT_EDITING: {
      SdlWindow* window = find_by_sdl_id(event.edit.windowID);
      if (window == nullptr) return;
      const char* text = event.edit.text != nullptr ? event.edit.text : "";
      window->composing_ = text[0] != '\0';
      platform::ImePreeditEvent e;
      e.text = text;
      e.cursor_begin = char_to_byte_offset(text, event.edit.start);
      e.cursor_end = event.edit.length < 0
                         ? e.cursor_begin
                         : char_to_byte_offset(text, event.edit.start + event.edit.length);
      core_->dispatch(Event::ime_preedit(window->id(), sdl_time(event.edit.timestamp), e));
      return;
    }
    case SDL_EVENT_TEXT_INPUT: {
      SdlWindow* window = find_by_sdl_id(event.text.windowID);
      if (window == nullptr || event.text.text == nullptr) return;
      const Timestamp time = sdl_time(event.text.timestamp);
      // Text arriving while a composition was open is its commit.
      if (window->composing_) {
        window->composing_ = false;
        core_->dispatch(Event::ime_commit(window->id(), time, event.text.text));
      } else {
        core_->dispatch(Event::text_input(window->id(), time, event.text.text));
      }
      return;
    }
    default:
      return;
  }
}

}  // namespace ui::sdl3
