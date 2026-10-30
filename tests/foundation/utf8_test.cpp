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
