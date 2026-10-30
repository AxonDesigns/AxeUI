#pragma once

#include <cstdint>

#include "foundation/assertions.h"

namespace ui::platform {

// Platform-neutral description of the OS object a GPU surface is created
// from. The platform layer fills it in; the renderer turns it into a
// graphics-API surface. Neither side needs the other's headers: handles are
// stored as opaque pointers / integers.
enum class NativeSurfaceKind : std::uint8_t {
  None = 0,    // No native surface (headless shells, offscreen rendering).
  Win32,       // HWND + HINSTANCE.
  MetalLayer,  // A CAMetalLayer (macOS view backing layer).
  Xlib,        // X11 Display* + Window.
  Wayland,     // wl_display* + wl_surface*.
  // Reserved for later shells; the renderer reports them as Unsupported
  // until they are wired up.
  AndroidWindow,  // ANativeWindow*.
  IosMetalLayer,  // A CAMetalLayer from a UIView.
  WebCanvas,      // CSS selector of an HTML canvas, e.g. "#canvas".
};

class NativeSurfaceHandle {
 public:
  struct Win32 {
    void* hwnd;
    void* hinstance;
  };
  struct MetalLayer {
    void* layer;  // CAMetalLayer*
  };
  struct Xlib {
    void* display;         // Display*
    std::uint64_t window;  // Window (an XID)
  };
  struct Wayland {
    void* display;  // wl_display*
    void* surface;  // wl_surface*
  };
  struct AndroidWindow {
    void* window;  // ANativeWindow*
  };
  struct WebCanvas {
    const char* selector;  // NUL-terminated; must outlive the surface.
  };

  constexpr NativeSurfaceHandle() noexcept = default;

  [[nodiscard]] static constexpr NativeSurfaceHandle none() noexcept { return {}; }
  [[nodiscard]] static NativeSurfaceHandle win32(void* hwnd, void* hinstance) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::Win32);
    h.data_.win32 = {hwnd, hinstance};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle metal_layer(void* layer) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::MetalLayer);
    h.data_.metal = {layer};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle ios_metal_layer(void* layer) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::IosMetalLayer);
    h.data_.metal = {layer};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle xlib(void* display, std::uint64_t window) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::Xlib);
    h.data_.xlib = {display, window};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle wayland(void* display, void* surface) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::Wayland);
    h.data_.wayland = {display, surface};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle android_window(void* window) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::AndroidWindow);
    h.data_.android = {window};
    return h;
  }
  [[nodiscard]] static NativeSurfaceHandle web_canvas(const char* selector) noexcept {
    NativeSurfaceHandle h(NativeSurfaceKind::WebCanvas);
    h.data_.web = {selector};
    return h;
  }

  [[nodiscard]] constexpr NativeSurfaceKind kind() const noexcept { return kind_; }
  [[nodiscard]] constexpr bool is_none() const noexcept { return kind_ == NativeSurfaceKind::None; }

  // Accessors abort if the kind does not match, instead of reading an
  // inactive union member.
  [[nodiscard]] const Win32& as_win32() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::Win32, "not a Win32 surface");
    return data_.win32;
  }
  [[nodiscard]] const MetalLayer& as_metal_layer() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::MetalLayer || kind_ == NativeSurfaceKind::IosMetalLayer,
              "not a Metal layer surface");
    return data_.metal;
  }
  [[nodiscard]] const Xlib& as_xlib() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::Xlib, "not an Xlib surface");
    return data_.xlib;
  }
  [[nodiscard]] const Wayland& as_wayland() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::Wayland, "not a Wayland surface");
    return data_.wayland;
  }
  [[nodiscard]] const AndroidWindow& as_android_window() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::AndroidWindow, "not an Android surface");
    return data_.android;
  }
  [[nodiscard]] const WebCanvas& as_web_canvas() const noexcept {
    UI_ASSERT(kind_ == NativeSurfaceKind::WebCanvas, "not a web canvas surface");
    return data_.web;
  }

 private:
  struct Empty {};
  union Data {
    Empty none{};
    Win32 win32;
    MetalLayer metal;
    Xlib xlib;
    Wayland wayland;
    AndroidWindow android;
    WebCanvas web;
  };

  explicit constexpr NativeSurfaceHandle(NativeSurfaceKind kind) noexcept : kind_(kind) {}

  NativeSurfaceKind kind_ = NativeSurfaceKind::None;
  Data data_;
};

}  // namespace ui::platform
