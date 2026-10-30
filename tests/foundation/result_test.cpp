#include "foundation/result.h"

#include <string_view>
#include <type_traits>
#include <utility>

#include "support/test_support.h"

namespace {

using namespace ui::foundation;

// Counts live instances so tests can check construction/destruction balance.
struct Tracked {
  static inline int live = 0;
  int value;

  explicit Tracked(int v) : value(v) { ++live; }
  Tracked(const Tracked& other) : value(other.value) { ++live; }
  Tracked(Tracked&& other) noexcept : value(other.value) { ++live; }
  Tracked& operator=(const Tracked&) = default;
  Tracked& operator=(Tracked&&) = default;
  ~Tracked() { --live; }
};

struct MoveOnly {
  int value;
  explicit MoveOnly(int v) : value(v) {}
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;
};

static_assert(!std::is_copy_constructible_v<Result<MoveOnly>>);
static_assert(std::is_move_constructible_v<Result<MoveOnly>>);
static_assert(std::is_copy_constructible_v<Result<int>>);
static_assert(!std::is_default_constructible_v<Result<int>>);
static_assert(std::is_default_constructible_v<Result<void>>);

Result<int> parse_digit(char c) {
  if (c < '0' || c > '9') {
    return fail(Error{ErrorCode::InvalidArgument, "not a digit"});
  }
  return c - '0';
}

Result<void> check_digit(char c) {
  UI_TRY(parse_digit(c));
  return {};
}

Result<int> sum_digits(char a, char b) {
  auto x = parse_digit(a);
  if (!x) {
    return fail(x.error());
  }
  UI_TRY(check_digit(b));
  return *x + *parse_digit(b);
}

}  // namespace

TEST_CASE("Result holds a value or an error") {
  const Result<int> ok = parse_digit('7');
  CHECK(ok.has_value());
  CHECK(static_cast<bool>(ok));
  CHECK(ok.value() == 7);
  CHECK(*ok == 7);
  CHECK(ok.value_or(-1) == 7);

  const Result<int> bad = parse_digit('x');
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error().code == ErrorCode::InvalidArgument);
  CHECK(std::string_view(bad.error().message) == "not a digit");
  CHECK(bad.value_or(-1) == -1);
}

TEST_CASE("Result<void> and UI_TRY propagate errors") {
  CHECK(check_digit('3').has_value());
  const Result<void> bad = check_digit('?');
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error().code == ErrorCode::InvalidArgument);

  CHECK(*sum_digits('2', '5') == 7);
  CHECK_FALSE(sum_digits('2', 'z').has_value());
  CHECK_FALSE(sum_digits('z', '2').has_value());
}

TEST_CASE("Result supports move-only values") {
  Result<MoveOnly> r = MoveOnly{42};
  CHECK(r->value == 42);
  Result<MoveOnly> moved = std::move(r);
  CHECK(moved->value == 42);
  MoveOnly taken = std::move(moved).value();
  CHECK(taken.value == 42);
}

TEST_CASE("Result constructs and destroys its contents exactly once") {
  Tracked::live = 0;
  {
    Result<Tracked> a = Tracked{1};
    CHECK(Tracked::live == 1);
    Result<Tracked> b = a;  // Copy.
    CHECK(Tracked::live == 2);
    Result<Tracked> c = fail(Error{ErrorCode::Internal, "boom"});
    c = b;  // Error -> value.
    CHECK(Tracked::live == 3);
    CHECK(c->value == 1);
    b = fail(Error{ErrorCode::Internal, "boom"});  // Value -> error.
    CHECK(Tracked::live == 2);
    CHECK_FALSE(b.has_value());
    a = std::move(c);  // Value -> value.
    CHECK(Tracked::live == 2);
  }
  CHECK(Tracked::live == 0);
}
