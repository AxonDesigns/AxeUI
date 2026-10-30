#pragma once

// SDL3 keyboard -> engine key translation. Internal to the SDL3 shell.

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include "platform/keys.h"

namespace ui::sdl3 {

// SDL scancodes are USB HID usage IDs, the same numbering as PhysicalKey.
[[nodiscard]] inline platform::PhysicalKey to_physical_key(SDL_Scancode scancode) noexcept {
  const auto value = static_cast<unsigned>(scancode);
  return value <= 0xE7u ? static_cast<platform::PhysicalKey>(value)
                        : platform::PhysicalKey::Unidentified;
}

[[nodiscard]] inline platform::Modifiers to_modifiers(SDL_Keymod mod) noexcept {
  platform::Modifiers m;
  if ((mod & SDL_KMOD_SHIFT) != 0) m.set(platform::Modifier::Shift);
  if ((mod & SDL_KMOD_CTRL) != 0) m.set(platform::Modifier::Control);
  if ((mod & SDL_KMOD_ALT) != 0) m.set(platform::Modifier::Alt);
  if ((mod & SDL_KMOD_GUI) != 0) m.set(platform::Modifier::Meta);
  if ((mod & SDL_KMOD_MODE) != 0) m.set(platform::Modifier::AltGraph);
  if ((mod & SDL_KMOD_CAPS) != 0) m.set(platform::Modifier::CapsLock);
  if ((mod & SDL_KMOD_NUM) != 0) m.set(platform::Modifier::NumLock);
  return m;
}

[[nodiscard]] inline platform::LogicalKey to_logical_key(SDL_Keycode key) noexcept {
  using platform::LogicalKey;
  using platform::NamedKey;
  switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return LogicalKey::named_key(NamedKey::Enter);
    case SDLK_TAB:
      return LogicalKey::named_key(NamedKey::Tab);
    case SDLK_SPACE:
      return LogicalKey::from_character(U' ');
    case SDLK_BACKSPACE:
      return LogicalKey::named_key(NamedKey::Backspace);
    case SDLK_ESCAPE:
      return LogicalKey::named_key(NamedKey::Escape);
    case SDLK_DELETE:
      return LogicalKey::named_key(NamedKey::Delete);
    case SDLK_INSERT:
      return LogicalKey::named_key(NamedKey::Insert);
    case SDLK_HOME:
      return LogicalKey::named_key(NamedKey::Home);
    case SDLK_END:
      return LogicalKey::named_key(NamedKey::End);
    case SDLK_PAGEUP:
      return LogicalKey::named_key(NamedKey::PageUp);
    case SDLK_PAGEDOWN:
      return LogicalKey::named_key(NamedKey::PageDown);
    case SDLK_LEFT:
      return LogicalKey::named_key(NamedKey::ArrowLeft);
    case SDLK_RIGHT:
      return LogicalKey::named_key(NamedKey::ArrowRight);
    case SDLK_UP:
      return LogicalKey::named_key(NamedKey::ArrowUp);
    case SDLK_DOWN:
      return LogicalKey::named_key(NamedKey::ArrowDown);
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
      return LogicalKey::named_key(NamedKey::Shift);
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      return LogicalKey::named_key(NamedKey::Control);
    case SDLK_LALT:
    case SDLK_RALT:
      return LogicalKey::named_key(NamedKey::Alt);
    case SDLK_LGUI:
    case SDLK_RGUI:
      return LogicalKey::named_key(NamedKey::Meta);
    case SDLK_MODE:
      return LogicalKey::named_key(NamedKey::AltGraph);
    case SDLK_CAPSLOCK:
      return LogicalKey::named_key(NamedKey::CapsLock);
    case SDLK_NUMLOCKCLEAR:
      return LogicalKey::named_key(NamedKey::NumLock);
    case SDLK_SCROLLLOCK:
      return LogicalKey::named_key(NamedKey::ScrollLock);
    case SDLK_APPLICATION:
      return LogicalKey::named_key(NamedKey::ContextMenu);
    case SDLK_PRINTSCREEN:
      return LogicalKey::named_key(NamedKey::PrintScreen);
    case SDLK_PAUSE:
      return LogicalKey::named_key(NamedKey::Pause);
    default:
      break;
  }
  if (key >= SDLK_F1 && key <= SDLK_F12) {
    return LogicalKey::named_key(
        static_cast<NamedKey>(static_cast<unsigned>(NamedKey::F1) + (key - SDLK_F1)));
  }
  if (key >= SDLK_F13 && key <= SDLK_F24) {
    return LogicalKey::named_key(
        static_cast<NamedKey>(static_cast<unsigned>(NamedKey::F13) + (key - SDLK_F13)));
  }
  // Without the scancode/extended masks, an SDL keycode is the Unicode code
  // point the key produces.
  if ((key & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK)) == 0 && key >= 0x20 && key != 0x7F) {
    return LogicalKey::from_character(static_cast<char32_t>(key));
  }
  return LogicalKey::named_key(NamedKey::Unidentified);
}

}  // namespace ui::sdl3
