#pragma once

#include <cstddef>
#include <string_view>

#include "foundation/result.h"

namespace ui::foundation {

// True if `text` is well-formed UTF-8 (RFC 3629): no overlong encodings, no
// surrogates (U+D800..U+DFFF), nothing above U+10FFFF, no truncated
// sequences.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

// Text offsets: the engine stores UTF-8 and counts in bytes. Hosts that use
// UTF-16 strings (JavaScript, Dart, Windows, Java) count in code units;
// these convert between the two for the same position.
//
// Both fail with InvalidArgument for invalid UTF-8, for an offset past the
// end, and for an offset that is not on a code-point boundary (inside a
// multi-byte sequence, or between the two halves of a surrogate pair).
[[nodiscard]] Result<std::size_t> utf8_to_utf16_offset(std::string_view text,
                                                       std::size_t utf8_offset) noexcept;
[[nodiscard]] Result<std::size_t> utf16_to_utf8_offset(std::string_view text,
                                                       std::size_t utf16_offset) noexcept;

}  // namespace ui::foundation
