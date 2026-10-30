#include "foundation/logging.h"

#include <string>

#include "support/test_support.h"

using namespace ui::foundation;

TEST_CASE("logger forwards records to the callback") {
  ui::test::LogCapture capture;
  capture.logger().log(LogLevel::Warning, ErrorCode::InvalidArgument, "bad size");
  UI_REQUIRE(capture.records().size() == 1);
  CHECK(capture.records()[0].level == LogLevel::Warning);
  CHECK(capture.records()[0].code == ErrorCode::InvalidArgument);
  CHECK(capture.records()[0].message == "bad size");

  capture.logger().report(Error{ErrorCode::StaleHandle, "stale"});
  UI_REQUIRE(capture.records().size() == 2);
  CHECK(capture.records()[1].level == LogLevel::Error);
  CHECK(capture.records()[1].code == ErrorCode::StaleHandle);
}

TEST_CASE("logger filters by minimum level") {
  ui::test::LogCapture capture(LogLevel::Warning);
  capture.logger().log(LogLevel::Info, ErrorCode::Ok, "ignored");
  capture.logger().log(LogLevel::Error, ErrorCode::Internal, "kept");
  CHECK(capture.records().size() == 1);
  CHECK_FALSE(capture.logger().enabled(LogLevel::Debug));
  CHECK(capture.logger().enabled(LogLevel::Error));
}

TEST_CASE("logger without a callback drops records") {
  const Logger logger;
  CHECK_FALSE(logger.enabled(LogLevel::Error));
  logger.log(LogLevel::Error, ErrorCode::Internal, "nobody listens");
}

TEST_CASE("logf formats and truncates") {
  ui::test::LogCapture capture;
  UI_LOGF(capture.logger(), LogLevel::Info, ErrorCode::Ok, "value=%d name=%s", 42, "x");
  UI_REQUIRE(capture.records().size() == 1);
  CHECK(capture.records()[0].message == "value=42 name=x");

  const std::string long_text(2000, 'a');
  UI_LOGF(capture.logger(), LogLevel::Info, ErrorCode::Ok, "%s", long_text.c_str());
  UI_REQUIRE(capture.records().size() == 2);
  CHECK(capture.records()[1].message.size() == Logger::kMaxFormattedLength - 1);
}

TEST_CASE("error codes have stable names") {
  CHECK(std::string(error_code_name(ErrorCode::StaleHandle)) == "StaleHandle");
  // Deliberately out of range: values from across the C ABI may be anything.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK(std::string(error_code_name(static_cast<ErrorCode>(9999))) == "Unknown");
}
