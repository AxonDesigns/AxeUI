#pragma once

#include <type_traits>

namespace ui::foundation {

// Type-safe set of bit flags over an enum whose enumerators are single bits.
//
//   enum class Modifier : std::uint16_t { Shift = 1 << 0, Control = 1 << 1 };
//   Flags<Modifier> mods = Flags<Modifier>(Modifier::Shift) | Modifier::Control;
//   if (mods.has(Modifier::Shift)) { ... }
template <class E>
class Flags {
  static_assert(std::is_enum_v<E>, "Flags requires an enum type");

 public:
  using Bits = std::underlying_type_t<E>;

  constexpr Flags() noexcept = default;
  constexpr Flags(E flag) noexcept : bits_(static_cast<Bits>(flag)) {}  // NOLINT

  [[nodiscard]] static constexpr Flags from_bits(Bits raw) noexcept {
    Flags f;
    f.bits_ = raw;
    return f;
  }

  [[nodiscard]] constexpr Bits bits() const noexcept { return bits_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
  [[nodiscard]] constexpr bool has(E flag) const noexcept {
    return (bits_ & static_cast<Bits>(flag)) == static_cast<Bits>(flag);
  }

  constexpr Flags& set(E flag) noexcept {
    bits_ = static_cast<Bits>(bits_ | static_cast<Bits>(flag));
    return *this;
  }
  constexpr Flags& clear(E flag) noexcept {
    bits_ = static_cast<Bits>(bits_ & static_cast<Bits>(~static_cast<Bits>(flag)));
    return *this;
  }

  friend constexpr Flags operator|(Flags a, Flags b) noexcept {
    return from_bits(static_cast<Bits>(a.bits_ | b.bits_));
  }
  friend constexpr Flags operator&(Flags a, Flags b) noexcept {
    return from_bits(static_cast<Bits>(a.bits_ & b.bits_));
  }
  friend constexpr bool operator==(Flags, Flags) = default;

 private:
  Bits bits_ = 0;
};

}  // namespace ui::foundation
