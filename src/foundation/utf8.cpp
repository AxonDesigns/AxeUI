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

namespace {

// Byte length of the code point starting with `lead` (text already valid).
std::size_t sequence_length(std::uint8_t lead) noexcept {
  if (lead < 0x80u) return 1;
  if (lead < 0xE0u) return 2;
  if (lead < 0xF0u) return 3;
  return 4;
}

}  // namespace

Result<std::size_t> utf8_to_utf16_offset(std::string_view text, std::size_t utf8_offset) noexcept {
  if (!is_valid_utf8(text) || utf8_offset > text.size()) {
    return fail(Error{ErrorCode::InvalidArgument, "invalid UTF-8 text or offset"});
  }
  std::size_t bytes = 0;
  std::size_t units = 0;
  while (bytes < utf8_offset) {
    const std::size_t length = sequence_length(static_cast<std::uint8_t>(text[bytes]));
    bytes += length;
    units += length == 4 ? 2u : 1u;  // Supplementary planes need a surrogate pair.
  }
  if (bytes != utf8_offset) {
    return fail(Error{ErrorCode::InvalidArgument, "UTF-8 offset is inside a code point"});
  }
  return units;
}

Result<std::size_t> utf16_to_utf8_offset(std::string_view text, std::size_t utf16_offset) noexcept {
  if (!is_valid_utf8(text)) {
    return fail(Error{ErrorCode::InvalidArgument, "invalid UTF-8 text"});
  }
  std::size_t bytes = 0;
  std::size_t units = 0;
  while (units < utf16_offset && bytes < text.size()) {
    const std::size_t length = sequence_length(static_cast<std::uint8_t>(text[bytes]));
    bytes += length;
    units += length == 4 ? 2u : 1u;
  }
  if (units != utf16_offset) {
    return fail(Error{ErrorCode::InvalidArgument,
                      "UTF-16 offset is past the end or inside a surrogate pair"});
  }
  return bytes;
}

}  // namespace ui::foundation
