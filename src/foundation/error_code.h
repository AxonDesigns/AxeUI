#pragma once

#include <cstddef>
#include <cstdint>

#include "foundation/build_config.h"

namespace ui::foundation {

// Engine-wide error codes. Values are part of the C ABI: ui_error_code in
// include/ui/ui.h mirrors them one to one (checked by static_assert in the C
// API implementation). Append new codes; never renumber.
enum class ErrorCode : std::uint32_t {
  Ok = 0,
  InvalidArgument = 1,
  OutOfMemory = 2,
  InvalidHandle = 3,    // Null handle, or an index that was never issued.
  StaleHandle = 4,      // The object the handle referred to no longer exists.
  WrongHandleType = 5,  // The handle refers to a different kind of object.
  CapacityExceeded = 6,
  Unsupported = 7,
  Unavailable = 8,  // Supported in general, but not right now / on this platform.
  InvalidState = 9,
  Internal = 10,
  GpuError = 11,     // The graphics API reported an error (validation, out of memory).
  DeviceLost = 12,   // The GPU device was lost; the renderer recreates it.
  WrongThread = 13,  // Called from a thread that does not own the core.
};

inline constexpr std::uint32_t kErrorCodeCount = 14;

// Stable, static name for a code ("InvalidArgument"). Unknown values yield
// "Unknown".
[[nodiscard]] const char* error_code_name(ErrorCode code) noexcept;

// An error value: a code plus a static, human-readable message.
//
// The message must have static storage duration (in practice: a string
// literal). Taking it as a reference to an array rejects plain `const char*`
// variables, which catches the common mistake of passing a temporary buffer.
struct Error {
  ErrorCode code;
  const char* message;

  template <std::size_t N>
  constexpr Error(ErrorCode error_code, const char (&static_message)[N]) noexcept
      : code(error_code), message(static_message) {}
};

}  // namespace ui::foundation
