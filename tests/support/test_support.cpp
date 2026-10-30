#include "support/test_support.h"

namespace ui::test {

LeakCheckedFixture::~LeakCheckedFixture() {
  const foundation::AllocationStats& stats = tracker_.stats();
  if (stats.live_allocations != 0 || stats.live_bytes != 0) {
    FAIL_CHECK("leak check: " << stats.live_allocations << " engine allocation(s), "
                              << stats.live_bytes << " byte(s) still live at end of test case");
  }
  for (const DeferredFree& entry : deferred_) {
    tracker_.deallocate(entry.pointer, entry.bytes, entry.alignment);
  }
}

void LeakCheckedFixture::free_after_leak_check(void* pointer, std::size_t bytes,
                                               std::size_t alignment) {
  deferred_.push_back({pointer, bytes, alignment});
}

void LogCapture::callback(void* user_data, const foundation::LogRecord& record) {
  auto* self = static_cast<LogCapture*>(user_data);
  self->records_.push_back({record.level, record.code, std::string(record.message)});
}

}  // namespace ui::test
