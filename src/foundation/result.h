#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "foundation/assertions.h"
#include "foundation/error_code.h"

namespace ui::foundation {

// Wrapper that marks a value as the error alternative when constructing a
// Result. Create it with fail().
template <class E>
struct Failure {
  E error;
};

template <class E>
[[nodiscard]] constexpr Failure<std::remove_cvref_t<E>> fail(E&& error) {
  return Failure<std::remove_cvref_t<E>>{std::forward<E>(error)};
}

namespace detail {
template <class T>
inline constexpr bool kIsFailure = false;
template <class E>
inline constexpr bool kIsFailure<Failure<E>> = true;
}  // namespace detail

// Result<T, E>: either a value of type T or an error of type E. The return
// type of every fallible function in the engine (exceptions are disabled).
//
//   Result<int> parse(std::string_view text);
//   ...
//   return 42;                                                  // success
//   return fail(Error{ErrorCode::InvalidArgument, "not a number"});  // failure
//
// Accessing value() on an error, or error() on a value, is a programming
// error and aborts through UI_ASSERT.
template <class T, class E = Error>
class [[nodiscard]] Result {
  static_assert(!std::is_reference_v<T>, "Result<T&> is not supported; use Result<T*>");
  static_assert(!std::is_void_v<E> && !std::is_reference_v<E>, "E must be an object type");
  static_assert(!detail::kIsFailure<std::remove_cv_t<T>>, "T must not be a Failure");

 public:
  using value_type = T;
  using error_type = E;

  template <class U = T>
    requires(std::is_convertible_v<U &&, T> && !std::is_same_v<std::remove_cvref_t<U>, Result> &&
             !detail::kIsFailure<std::remove_cvref_t<U>>)
  constexpr Result(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>)  // NOLINT
      : value_(std::forward<U>(value)), has_value_(true) {}

  template <class G>
    requires std::is_constructible_v<E, G&&>
  constexpr Result(Failure<G>&& failure)  // NOLINT(google-explicit-constructor)
      : error_(std::move(failure.error)), has_value_(false) {}

  template <class G>
    requires std::is_constructible_v<E, const G&>
  constexpr Result(const Failure<G>& failure)  // NOLINT(google-explicit-constructor)
      : error_(failure.error), has_value_(false) {}

  Result(const Result& other)
    requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>)
      : has_value_(other.has_value_) {
    if (has_value_) {
      std::construct_at(&value_, other.value_);
    } else {
      std::construct_at(&error_, other.error_);
    }
  }

  Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                  std::is_nothrow_move_constructible_v<E>)
    requires(std::is_move_constructible_v<T> && std::is_move_constructible_v<E>)
      : has_value_(other.has_value_) {
    if (has_value_) {
      std::construct_at(&value_, std::move(other.value_));
    } else {
      std::construct_at(&error_, std::move(other.error_));
    }
  }

  Result& operator=(const Result& other)
    requires(std::is_copy_constructible_v<T> && std::is_copy_constructible_v<E>)
  {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (has_value_) {
        std::construct_at(&value_, other.value_);
      } else {
        std::construct_at(&error_, other.error_);
      }
    }
    return *this;
  }

  Result& operator=(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                             std::is_nothrow_move_constructible_v<E>)
    requires(std::is_move_constructible_v<T> && std::is_move_constructible_v<E>)
  {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (has_value_) {
        std::construct_at(&value_, std::move(other.value_));
      } else {
        std::construct_at(&error_, std::move(other.error_));
      }
    }
    return *this;
  }

  ~Result() { destroy(); }

  [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }

  [[nodiscard]] T& value() & {
    UI_ASSERT(has_value_, "Result::value() called on an error");
    return value_;
  }
  [[nodiscard]] const T& value() const& {
    UI_ASSERT(has_value_, "Result::value() called on an error");
    return value_;
  }
  [[nodiscard]] T&& value() && {
    UI_ASSERT(has_value_, "Result::value() called on an error");
    return std::move(value_);
  }

  [[nodiscard]] E& error() & {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return error_;
  }
  [[nodiscard]] const E& error() const& {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return error_;
  }
  [[nodiscard]] E&& error() && {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return std::move(error_);
  }

  template <class U>
  [[nodiscard]] T value_or(U&& fallback) const& {
    return has_value_ ? value_ : static_cast<T>(std::forward<U>(fallback));
  }
  template <class U>
  [[nodiscard]] T value_or(U&& fallback) && {
    return has_value_ ? std::move(value_) : static_cast<T>(std::forward<U>(fallback));
  }

  [[nodiscard]] T& operator*() & { return value(); }
  [[nodiscard]] const T& operator*() const& { return value(); }
  [[nodiscard]] T&& operator*() && { return std::move(*this).value(); }
  [[nodiscard]] T* operator->() { return &value(); }
  [[nodiscard]] const T* operator->() const { return &value(); }

 private:
  void destroy() noexcept {
    if (has_value_) {
      std::destroy_at(&value_);
    } else {
      std::destroy_at(&error_);
    }
  }

  union {
    T value_;
    E error_;
  };
  bool has_value_;
};

// Result<void, E>: success carries no value. A default-constructed Result is a
// success, so `return {};` reports success.
template <class E>
class [[nodiscard]] Result<void, E> {
  static_assert(!std::is_void_v<E> && !std::is_reference_v<E>, "E must be an object type");

 public:
  using value_type = void;
  using error_type = E;

  constexpr Result() noexcept : empty_(), has_value_(true) {}

  template <class G>
    requires std::is_constructible_v<E, G&&>
  constexpr Result(Failure<G>&& failure)  // NOLINT(google-explicit-constructor)
      : error_(std::move(failure.error)), has_value_(false) {}

  template <class G>
    requires std::is_constructible_v<E, const G&>
  constexpr Result(const Failure<G>& failure)  // NOLINT(google-explicit-constructor)
      : error_(failure.error), has_value_(false) {}

  Result(const Result& other)
    requires std::is_copy_constructible_v<E>
      : empty_(), has_value_(other.has_value_) {
    if (!has_value_) {
      std::construct_at(&error_, other.error_);
    }
  }

  Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<E>)
    requires std::is_move_constructible_v<E>
      : empty_(), has_value_(other.has_value_) {
    if (!has_value_) {
      std::construct_at(&error_, std::move(other.error_));
    }
  }

  Result& operator=(const Result& other)
    requires std::is_copy_constructible_v<E>
  {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (!has_value_) {
        std::construct_at(&error_, other.error_);
      }
    }
    return *this;
  }

  Result& operator=(Result&& other) noexcept(std::is_nothrow_move_constructible_v<E>)
    requires std::is_move_constructible_v<E>
  {
    if (this != &other) {
      destroy();
      has_value_ = other.has_value_;
      if (!has_value_) {
        std::construct_at(&error_, std::move(other.error_));
      }
    }
    return *this;
  }

  ~Result() { destroy(); }

  [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }
  [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value_; }

  // Asserts success. Useful where an error is a programming bug.
  void value() const { UI_ASSERT(has_value_, "Result::value() called on an error"); }

  [[nodiscard]] E& error() & {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return error_;
  }
  [[nodiscard]] const E& error() const& {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return error_;
  }
  [[nodiscard]] E&& error() && {
    UI_ASSERT(!has_value_, "Result::error() called on a value");
    return std::move(error_);
  }

 private:
  struct Empty {};

  void destroy() noexcept {
    if (!has_value_) {
      std::destroy_at(&error_);
    }
  }

  union {
    Empty empty_;
    E error_;
  };
  bool has_value_;
};

}  // namespace ui::foundation

// Propagates the error of a Result-returning expression from the enclosing
// function (which must itself return a Result with a compatible error type).
// The success value, if any, is discarded.
#define UI_TRY(expression)                                              \
  do {                                                                  \
    auto&& ui_try_result_ = (expression);                               \
    if (!ui_try_result_.has_value()) {                                  \
      return ::ui::foundation::fail(std::move(ui_try_result_).error()); \
    }                                                                   \
  } while (false)
