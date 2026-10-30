#pragma once

#include <string_view>

namespace ui::foundation {

// True if `text` is well-formed UTF-8 (RFC 3629): no overlong encodings, no
// surrogates (U+D800..U+DFFF), nothing above U+10FFFF, no truncated
// sequences.
[[nodiscard]] bool is_valid_utf8(std::string_view text) noexcept;

}  // namespace ui::foundation
