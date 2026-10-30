#include "foundation/logging.h"

#include <cstdarg>
#include <cstdio>

namespace ui::foundation {

void Logger::log(LogLevel level, ErrorCode code, std::string_view message,
                 std::source_location location) const {
  if (!enabled(level)) {
    return;
  }
  const LogRecord record{level, code, message, location.file_name(), location.line()};
  callback_(user_data_, record);
}

void Logger::logf(std::source_location location, LogLevel level, ErrorCode code, const char* format,
                  ...) const {
  if (!enabled(level)) {
    return;
  }
  char buffer[kMaxFormattedLength];
  va_list args;
  va_start(args, format);
  // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized): false positive, va_start above.
  const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (written < 0) {
    return;
  }
  const std::size_t length = static_cast<std::size_t>(written) < sizeof(buffer)
                                 ? static_cast<std::size_t>(written)
                                 : sizeof(buffer) - 1;
  log(level, code, std::string_view(buffer, length), location);
}

void Logger::report(const Error& error, std::source_location location) const {
  log(LogLevel::Error, error.code, error.message, location);
}

}  // namespace ui::foundation
