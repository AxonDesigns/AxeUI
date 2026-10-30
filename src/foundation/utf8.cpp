#include "foundation/utf8.h"

#include <cstddef>
#include <cstdint>

namespace ui::foundation {

bool is_valid_utf8(std::string_view text) noexcept {
  const std::size_t size = text.size();
  std::size_t i = 0;
  while (i < size) {
    const auto lead = static_cast<std::uint8_t>(text[i]);
    if (lead < 0x80u) {
      ++i;
      continue;
    }
    std::size_t length = 0;
    // Valid range of the second byte; later bytes are always 0x80..0xBF.
    std::uint8_t second_min = 0x80u;
    std::uint8_t second_max = 0xBFu;
    if (lead >= 0xC2u && lead <= 0xDFu) {
      length = 2;
    } else if (lead >= 0xE0u && lead <= 0xEFu) {
      length = 3;
      if (lead == 0xE0u) {
        second_min = 0xA0u;  // Reject overlong encodings.
      } else if (lead == 0xEDu) {
        second_max = 0x9Fu;  // Reject surrogates.
      }
    } else if (lead >= 0xF0u && lead <= 0xF4u) {
      length = 4;
      if (lead == 0xF0u) {
        second_min = 0x90u;  // Reject overlong encodings.
      } else if (lead == 0xF4u) {
        second_max = 0x8Fu;  // Reject code points above U+10FFFF.
      }
    } else {
      return false;  // Continuation byte, overlong 2-byte lead, or 0xF5..0xFF.
    }
    if (size - i < length) {
      return false;
    }
    const auto second = static_cast<std::uint8_t>(text[i + 1]);
    if (second < second_min || second > second_max) {
      return false;
    }
    for (std::size_t k = 2; k < length; ++k) {
      const auto continuation = static_cast<std::uint8_t>(text[i + k]);
      if (continuation < 0x80u || continuation > 0xBFu) {
        return false;
      }
    }
    i += length;
  }
  return true;
}

}  // namespace ui::foundation
