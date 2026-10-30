#pragma once

#include <cstddef>
#include <cstdint>
#include <source_location>
#include <string_view>

#include "foundation/build_config.h"
#include "foundation/error_code.h"

namespace ui::foundation {

enum class LogLevel : std::uint8_t {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warning = 3,
  Error = 4,
};

struct LogRecord {
  LogLevel level;
  ErrorCode code;            // ErrorCode::Ok for purely informational messages.
  std::string_view message;  // Valid only for the duration of the callback.
  const char* file;
  std::uint32_t line;
};

using LogCallback = void (*)(void* user_data, const LogRecord& record);

// Routes engine diagnostics to an application-supplied callback. Without a
// callback, records are dropped: library code never writes to stdout/stderr
// on its own (assertion failures excepted).
//
// A Logger is a plain value owned by the engine root and passed by reference;
// there is no global logger.
class Logger {
 public:
  static constexpr std::size_t kMaxFormattedLength = 512;

  constexpr Logger() noexcept = default;
  constexpr Logger(LogCallback callback, void* user_data,
                   LogLevel min_level = LogLevel::Info) noexcept
      : callback_(callback), user_data_(user_data), min_level_(min_level) {}

  void set_callback(LogCallback callback, void* user_data) noexcept {
    callback_ = callback;
    user_data_ = user_data;
  }
  void set_min_level(LogLevel level) noexcept { min_level_ = level; }
  [[nodiscard]] LogLevel min_level() const noexcept { return min_level_; }

  [[nodiscard]] bool enabled(LogLevel level) const noexcept {
    return callback_ != nullptr && level >= min_level_;
  }

  void log(LogLevel level, ErrorCode code, std::string_view message,
           std::source_location location = std::source_location::current()) const;

  // printf-style; output longer than kMaxFormattedLength is truncated.
  void logf(std::source_location location, LogLevel level, ErrorCode code, const char* format,
            ...) const UI_PRINTF_FORMAT(5, 6);

  // Reports an Error at LogLevel::Error.
  void report(const Error& error,
              std::source_location location = std::source_location::current()) const;

 private:
  LogCallback callback_ = nullptr;
  void* user_data_ = nullptr;
  LogLevel min_level_ = LogLevel::Info;
};

}  // namespace ui::foundation

// Formats a message with the caller's source location.
#define UI_LOGF(logger, level, code, ...) \
  (logger).logf(std::source_location::current(), (level), (code), __VA_ARGS__)
