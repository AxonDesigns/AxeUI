#include "platform/event.h"

namespace ui::platform {

const char* event_kind_name(EventKind kind) noexcept {
  switch (kind) {
    case EventKind::PointerDown:
      return "PointerDown";
    case EventKind::PointerUp:
      return "PointerUp";
    case EventKind::PointerMove:
      return "PointerMove";
    case EventKind::PointerCancel:
      return "PointerCancel";
    case EventKind::Scroll:
      return "Scroll";
    case EventKind::KeyDown:
      return "KeyDown";
    case EventKind::KeyUp:
      return "KeyUp";
    case EventKind::TextInput:
      return "TextInput";
    case EventKind::ImePreedit:
      return "ImePreedit";
    case EventKind::ImeCommit:
      return "ImeCommit";
    case EventKind::Resize:
      return "Resize";
    case EventKind::ScaleFactorChanged:
      return "ScaleFactorChanged";
    case EventKind::FocusGained:
      return "FocusGained";
    case EventKind::FocusLost:
      return "FocusLost";
    case EventKind::Suspend:
      return "Suspend";
    case EventKind::Resume:
      return "Resume";
    case EventKind::SurfaceLost:
      return "SurfaceLost";
    case EventKind::CloseRequested:
      return "CloseRequested";
  }
  return "Unknown";
}

}  // namespace ui::platform
