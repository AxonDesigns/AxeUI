#pragma once

#include <cstdint>

namespace ui::foundation {

// Object type tags carried in every handle (6 bits, so at most 63 kinds).
// Tags are assigned centrally so two kinds of object can never share one.
// Values are ABI: append only.
enum class HandleType : std::uint8_t {
  None = 0,  // Never issued.
  Node = 1,
  Texture = 2,
  Image = 3,
  Font = 4,
  Surface = 5,
};

// A generational handle that fits in 53 bits, so it is an exact integer in
// JavaScript (Number.MAX_SAFE_INTEGER = 2^53 - 1) and in Dart compiled to
// JavaScript:
//
//   bit 63      53 52        47 46                  27 26                 0
//      [ 0 : 11   | type : 6   | generation : 20     | index : 27         ]
//
// The top 11 bits are always zero; a value with any of them set is not a
// handle. Generation 0 is never issued, so the all-zero value is never a
// valid handle and serves as the null handle. The same bits cross the C API
// as uint64_t.
class Handle {
 public:
  static constexpr unsigned kIndexBits = 27;
  static constexpr unsigned kGenerationBits = 20;
  static constexpr unsigned kTypeBits = 6;
  static constexpr unsigned kTotalBits = kIndexBits + kGenerationBits + kTypeBits;  // 53
  static constexpr std::uint32_t kMaxGeneration = (1u << kGenerationBits) - 1u;
  static constexpr std::uint32_t kIndexMask = (1u << kIndexBits) - 1u;
  // The all-ones index is reserved, so the largest usable index is one less.
  static constexpr std::uint32_t kMaxIndex = kIndexMask - 1u;
  static constexpr std::uint32_t kMaxType = (1u << kTypeBits) - 1u;

  constexpr Handle() noexcept = default;

  [[nodiscard]] static constexpr Handle from_bits(std::uint64_t raw) noexcept {
    Handle h;
    h.bits_ = raw;
    return h;
  }

  // `tag`, `gen` and `slot` are truncated to their field widths.
  [[nodiscard]] static constexpr Handle make(HandleType tag, std::uint32_t gen,
                                             std::uint32_t slot) noexcept {
    return from_bits(
        ((static_cast<std::uint64_t>(tag) & kMaxType) << (kIndexBits + kGenerationBits)) |
        (static_cast<std::uint64_t>(gen & kMaxGeneration) << kIndexBits) |
        static_cast<std::uint64_t>(slot & kIndexMask));
  }

  [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }
  [[nodiscard]] constexpr bool is_null() const noexcept { return bits_ == 0; }
  // False if any bit above bit 52 is set: such a value was never a handle.
  [[nodiscard]] constexpr bool is_well_formed() const noexcept {
    return (bits_ >> kTotalBits) == 0;
  }

  [[nodiscard]] constexpr HandleType type() const noexcept {
    return static_cast<HandleType>((bits_ >> (kIndexBits + kGenerationBits)) & kMaxType);
  }
  [[nodiscard]] constexpr std::uint32_t generation() const noexcept {
    return static_cast<std::uint32_t>(bits_ >> kIndexBits) & kMaxGeneration;
  }
  [[nodiscard]] constexpr std::uint32_t index() const noexcept {
    return static_cast<std::uint32_t>(bits_) & kIndexMask;
  }

  friend constexpr bool operator==(Handle, Handle) = default;

 private:
  std::uint64_t bits_ = 0;
};

static_assert(sizeof(Handle) == 8);
static_assert(Handle::kTotalBits == 53);

}  // namespace ui::foundation
