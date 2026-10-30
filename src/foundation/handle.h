#pragma once

#include <cstdint>

namespace ui::foundation {

// Object type tags carried in every handle. Tags are assigned centrally so
// two kinds of object can never share one. Values are ABI: append only.
enum class HandleType : std::uint8_t {
  None = 0,  // Never issued.
  Node = 1,
  Texture = 2,
  Image = 3,
  Font = 4,
};

// A 64-bit generational handle:
//
//   bit 63         56 55                 32 31                  0
//      [ type : 8     | generation : 24     | index : 32         ]
//
// Generation 0 is never issued, so the all-zero value is never a valid handle
// and serves as the null handle. The same bits cross the C API as uint64_t.
class Handle {
 public:
  static constexpr unsigned kIndexBits = 32;
  static constexpr unsigned kGenerationBits = 24;
  static constexpr unsigned kTypeBits = 8;
  static constexpr std::uint32_t kMaxGeneration = (1u << kGenerationBits) - 1u;
  static constexpr std::uint32_t kMaxIndex = 0xFFFFFFFEu;  // 0xFFFFFFFF is reserved.

  constexpr Handle() noexcept = default;

  [[nodiscard]] static constexpr Handle from_bits(std::uint64_t raw) noexcept {
    Handle h;
    h.bits_ = raw;
    return h;
  }

  [[nodiscard]] static constexpr Handle make(HandleType type, std::uint32_t generation,
                                             std::uint32_t index) noexcept {
    return from_bits((static_cast<std::uint64_t>(type) << (kIndexBits + kGenerationBits)) |
                     (static_cast<std::uint64_t>(generation & kMaxGeneration) << kIndexBits) |
                     static_cast<std::uint64_t>(index));
  }

  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return bits_ == 0; }

  [[nodiscard]] constexpr HandleType type() const noexcept {
    return static_cast<HandleType>(bits_ >> (kIndexBits + kGenerationBits));
  }
  [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
    return static_cast<std::uint32_t>(bits_ >> kIndexBits) & kMaxGeneration;
  }
  [[nodiscard]] constexpr std::uint32_t index() const noexcept {
    return static_cast<std::uint32_t>(bits_ & 0xFFFFFFFFu);
  }

  friend constexpr bool operator==(Handle, Handle) = default;

 private:
  std::uint64_t bits_ = 0;
};

static_assert(sizeof(Handle) == 8);

}  // namespace ui::foundation
