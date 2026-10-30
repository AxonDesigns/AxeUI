#pragma once

#include <cstdint>

#include "foundation/flags.h"

namespace ui::platform {

// Physical key position, independent of keyboard layout. Values are USB HID
// usage IDs (Keyboard/Keypad page 0x07), the same numbering SDL3 scancodes
// use; the W3C KeyboardEvent.code strings map onto them one to one. Keys not
// listed here can still be represented by their HID usage value.
// clang-format off
enum class PhysicalKey : std::uint16_t {
  Unidentified = 0x00,
  A = 0x04, B = 0x05, C = 0x06, D = 0x07, E = 0x08, F = 0x09, G = 0x0A, H = 0x0B, I = 0x0C,
  J = 0x0D, K = 0x0E, L = 0x0F, M = 0x10, N = 0x11, O = 0x12, P = 0x13, Q = 0x14, R = 0x15,
  S = 0x16, T = 0x17, U = 0x18, V = 0x19, W = 0x1A, X = 0x1B, Y = 0x1C, Z = 0x1D,
  Digit1 = 0x1E, Digit2 = 0x1F, Digit3 = 0x20, Digit4 = 0x21, Digit5 = 0x22,
  Digit6 = 0x23, Digit7 = 0x24, Digit8 = 0x25, Digit9 = 0x26, Digit0 = 0x27,
  Enter = 0x28, Escape = 0x29, Backspace = 0x2A, Tab = 0x2B, Space = 0x2C,
  Minus = 0x2D, Equal = 0x2E, BracketLeft = 0x2F, BracketRight = 0x30, Backslash = 0x31,
  IntlHash = 0x32, Semicolon = 0x33, Quote = 0x34, Backquote = 0x35, Comma = 0x36,
  Period = 0x37, Slash = 0x38, CapsLock = 0x39,
  F1 = 0x3A, F2 = 0x3B, F3 = 0x3C, F4 = 0x3D, F5 = 0x3E, F6 = 0x3F,
  F7 = 0x40, F8 = 0x41, F9 = 0x42, F10 = 0x43, F11 = 0x44, F12 = 0x45,
  PrintScreen = 0x46, ScrollLock = 0x47, Pause = 0x48, Insert = 0x49, Home = 0x4A,
  PageUp = 0x4B, Delete = 0x4C, End = 0x4D, PageDown = 0x4E,
  ArrowRight = 0x4F, ArrowLeft = 0x50, ArrowDown = 0x51, ArrowUp = 0x52,
  NumLock = 0x53, NumpadDivide = 0x54, NumpadMultiply = 0x55, NumpadSubtract = 0x56,
  NumpadAdd = 0x57, NumpadEnter = 0x58, Numpad1 = 0x59, Numpad2 = 0x5A, Numpad3 = 0x5B,
  Numpad4 = 0x5C, Numpad5 = 0x5D, Numpad6 = 0x5E, Numpad7 = 0x5F, Numpad8 = 0x60,
  Numpad9 = 0x61, Numpad0 = 0x62, NumpadDecimal = 0x63, IntlBackslash = 0x64,
  ContextMenu = 0x65, NumpadEqual = 0x67,
  F13 = 0x68, F14 = 0x69, F15 = 0x6A, F16 = 0x6B, F17 = 0x6C, F18 = 0x6D,
  F19 = 0x6E, F20 = 0x6F, F21 = 0x70, F22 = 0x71, F23 = 0x72, F24 = 0x73,
  ControlLeft = 0xE0, ShiftLeft = 0xE1, AltLeft = 0xE2, MetaLeft = 0xE3,
  ControlRight = 0xE4, ShiftRight = 0xE5, AltRight = 0xE6, MetaRight = 0xE7,
};
// clang-format on

// Layout-dependent meaning of a key press for keys that do not produce a
// character (W3C KeyboardEvent.key "named key" values).
// clang-format off
enum class NamedKey : std::uint16_t {
  Unidentified = 0,
  Character,  // Not a named key: see LogicalKey::character.
  Enter, Tab, Space, Backspace, Escape, Delete, Insert,
  Home, End, PageUp, PageDown,
  ArrowLeft, ArrowRight, ArrowUp, ArrowDown,
  Shift, Control, Alt, AltGraph, Meta, CapsLock, NumLock, ScrollLock,
  ContextMenu, PrintScreen, Pause,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,
  Dead,  // A dead key that composes with the next key.
};
// clang-format on

// What the key means under the current layout. Either a named key, or
// (named == NamedKey::Character) the Unicode scalar value it produces.
// Text entry must use text-input events, not this: one key press can produce
// zero or several characters.
struct LogicalKey {
  NamedKey named = NamedKey::Unidentified;
  char32_t character = 0;

  [[nodiscard]] static constexpr LogicalKey named_key(NamedKey key) noexcept { return {key, 0}; }
  [[nodiscard]] static constexpr LogicalKey from_character(char32_t c) noexcept {
    return {NamedKey::Character, c};
  }

  friend constexpr bool operator==(LogicalKey, LogicalKey) = default;
};

enum class Modifier : std::uint16_t {
  Shift = 1u << 0,
  Control = 1u << 1,
  Alt = 1u << 2,
  Meta = 1u << 3,  // Command on Apple platforms, Windows/Super key elsewhere.
  AltGraph = 1u << 4,
  CapsLock = 1u << 5,
  NumLock = 1u << 6,
};

using Modifiers = foundation::Flags<Modifier>;

}  // namespace ui::platform
