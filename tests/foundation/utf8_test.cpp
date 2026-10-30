#include "foundation/utf8.h"

#include <string_view>

#include "support/test_support.h"

using ui::foundation::is_valid_utf8;

namespace {
std::string_view bytes(const char* s, std::size_t n) { return std::string_view(s, n); }
}  // namespace

TEST_CASE("valid UTF-8 is accepted") {
  CHECK(is_valid_utf8(""));
  CHECK(is_valid_utf8("plain ascii"));
  CHECK(is_valid_utf8("é"));                           // 2 bytes
  CHECK(is_valid_utf8("日本語"));                      // 3 bytes
  CHECK(is_valid_utf8("😀"));                          // 4 bytes
  CHECK(is_valid_utf8(bytes("\xEF\xBF\xBF", 3)));      // U+FFFF
  CHECK(is_valid_utf8(bytes("\xF4\x8F\xBF\xBF", 4)));  // U+10FFFF
  CHECK(is_valid_utf8(bytes("a\0b", 3)));              // Embedded NUL is valid.
}

TEST_CASE("malformed UTF-8 is rejected") {
  CHECK_FALSE(is_valid_utf8(bytes("\x80", 1)));              // Lone continuation.
  CHECK_FALSE(is_valid_utf8(bytes("\xC3", 1)));              // Truncated.
  CHECK_FALSE(is_valid_utf8(bytes("\xC3\x28", 2)));          // Bad continuation.
  CHECK_FALSE(is_valid_utf8(bytes("\xC0\x80", 2)));          // Overlong NUL.
  CHECK_FALSE(is_valid_utf8(bytes("\xE0\x80\xAF", 3)));      // Overlong.
  CHECK_FALSE(is_valid_utf8(bytes("\xED\xA0\x80", 3)));      // Surrogate U+D800.
  CHECK_FALSE(is_valid_utf8(bytes("\xF0\x80\x80\xAF", 4)));  // Overlong.
  CHECK_FALSE(is_valid_utf8(bytes("\xF4\x90\x80\x80", 4)));  // Above U+10FFFF.
  CHECK_FALSE(is_valid_utf8(bytes("\xF5\x80\x80\x80", 4)));  // Invalid lead.
  CHECK_FALSE(is_valid_utf8(bytes("\xE6\x97", 2)));          // Truncated 3-byte.
  CHECK_FALSE(is_valid_utf8(bytes("\xE6\x97\x28", 3)));      // Bad third byte.
}

TEST_CASE("UTF-8 and UTF-16 offsets convert both ways") {
  using ui::foundation::utf16_to_utf8_offset;
  using ui::foundation::utf8_to_utf16_offset;
  // "aé日😀b": 1 + 2 + 3 + 4 + 1 bytes; 1 + 1 + 1 + 2 + 1 UTF-16 units.
  const std::string_view text =
      "a\xC3\xA9\xE6\x97\xA5\xF0\x9F\x98\x80"
      "b";
  const std::size_t utf8[] = {0, 1, 3, 6, 10, 11};
  const std::size_t utf16[] = {0, 1, 2, 3, 5, 6};
  for (int i = 0; i < 6; ++i) {
    CAPTURE(i);
    CHECK(*utf8_to_utf16_offset(text, utf8[i]) == utf16[i]);
    CHECK(*utf16_to_utf8_offset(text, utf16[i]) == utf8[i]);
  }
  CHECK_FALSE(utf8_to_utf16_offset(text, 2).has_value());   // Inside "é".
  CHECK_FALSE(utf8_to_utf16_offset(text, 12).has_value());  // Past the end.
  CHECK_FALSE(utf16_to_utf8_offset(text, 4).has_value());   // Between surrogates.
  CHECK_FALSE(utf16_to_utf8_offset(text, 7).has_value());   // Past the end.
  CHECK_FALSE(utf8_to_utf16_offset(bytes("\xFF", 1), 0).has_value());
}
