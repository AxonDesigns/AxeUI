#pragma once

// Shared test harness.
//
// Every engine test that allocates is written with UI_TEST_CASE instead of TEST_CASE. It
// runs the body in a LeakCheckedFixture, which owns a TrackingResource; tests
// pass memory() to whatever they construct. When the test case ends, the
// fixture fails the test if any allocation made through memory() is still
// live.

#include <cstddef>
#include <string>
#include <vector>

#include <doctest.h>

#include "foundation/allocator.h"
#include "foundation/logging.h"
#include "foundation/tracking_resource.h"
#include "support/test_process.h"

namespace ui::test {

class LeakCheckedFixture {
 public:
  LeakCheckedFixture() noexcept : tracker_(heap_) {}
  ~LeakCheckedFixture();

  LeakCheckedFixture(const LeakCheckedFixture&) = delete;
  LeakCheckedFixture& operator=(const LeakCheckedFixture&) = delete;

  [[nodiscard]] foundation::MemoryResource& memory() noexcept { return tracker_; }
  [[nodiscard]] const foundation::TrackingResource& tracker() const noexcept { return tracker_; }

  // Frees a block only after the leak check has run. Lets the harness
  // self-test leak on purpose without leaving a real leak behind for
  // LeakSanitizer to report.
  void free_after_leak_check(void* pointer, std::size_t bytes, std::size_t alignment);

 private:
  struct DeferredFree {
    void* pointer;
    std::size_t bytes;
    std::size_t alignment;
  };

  foundation::HeapResource heap_;
  foundation::TrackingResource tracker_;
  std::vector<DeferredFree> deferred_;
};

// A record captured by LogCapture, with the message copied out.
struct CapturedLog {
  foundation::LogLevel level;
  foundation::ErrorCode code;
  std::string message;
};

// Collects log records so tests can assert on them.
class LogCapture {
 public:
  explicit LogCapture(foundation::LogLevel min_level = foundation::LogLevel::Trace) noexcept
      : logger_(&LogCapture::callback, this, min_level) {}

  LogCapture(const LogCapture&) = delete;
  LogCapture& operator=(const LogCapture&) = delete;

  [[nodiscard]] foundation::Logger& logger() noexcept { return logger_; }
  [[nodiscard]] const std::vector<CapturedLog>& records() const noexcept { return records_; }
  void clear() noexcept { records_.clear(); }

 private:
  static void callback(void* user_data, const foundation::LogRecord& record);

  foundation::Logger logger_;
  std::vector<CapturedLog> records_;
};

// A resource whose allocations always fail, for out-of-memory paths.
class FailingResource final : public foundation::MemoryResource {
 protected:
  void* do_allocate(std::size_t, std::size_t) override { return nullptr; }
  void do_deallocate(void*, std::size_t, std::size_t) noexcept override {}
};

}  // namespace ui::test

#define UI_TEST_CASE(...) TEST_CASE_FIXTURE(::ui::test::LeakCheckedFixture, __VA_ARGS__)

// doctest's REQUIRE needs exceptions to abandon a test case. UI_REQUIRE
// records a failed check and returns from the test body instead. Use it
// where continuing would dereference something invalid.
#define UI_REQUIRE(...)                                                   \
  do {                                                                    \
    const bool ui_require_ok_ = static_cast<bool>(__VA_ARGS__);           \
    CHECK_MESSAGE(ui_require_ok_, "UI_REQUIRE(" #__VA_ARGS__ ") failed"); \
    if (!ui_require_ok_) {                                                \
      return;                                                             \
    }                                                                     \
  } while (false)
