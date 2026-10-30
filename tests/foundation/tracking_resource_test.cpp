#include "foundation/tracking_resource.h"

#include "foundation/allocator.h"
#include "support/test_support.h"

using namespace ui::foundation;

UI_TEST_CASE("tracking resource counts live and peak allocations") {
  TrackingResource tracking(memory());
  void* a = tracking.allocate(100, 8);
  void* b = tracking.allocate(50, 64);
  CHECK(tracking.stats().live_allocations == 2);
  CHECK(tracking.stats().live_bytes == 150);

  tracking.deallocate(a, 100, 8);
  CHECK(tracking.stats().live_allocations == 1);
  CHECK(tracking.stats().live_bytes == 50);

  void* c = tracking.allocate(10, 8);
  tracking.deallocate(b, 50, 64);
  tracking.deallocate(c, 10, 8);

  const AllocationStats& stats = tracking.stats();
  CHECK(stats.live_allocations == 0);
  CHECK(stats.live_bytes == 0);
  CHECK(stats.peak_allocations == 2);
  CHECK(stats.peak_bytes == 150);
  CHECK(stats.total_allocations == 3);
  CHECK(stats.failed_allocations == 0);
}

UI_TEST_CASE("tracking resource sees container allocations") {
  TrackingResource tracking(memory());
  {
    Vector<int> values{Allocator<int>(tracking)};
    values.resize(1000);
    CHECK(tracking.stats().live_allocations == 1);
    CHECK(tracking.stats().live_bytes == values.capacity() * sizeof(int));
  }
  CHECK(tracking.stats().live_allocations == 0);
}

TEST_CASE("tracking resource counts upstream failures") {
  ui::test::FailingResource failing;
  TrackingResource tracking(failing);
  CHECK(tracking.allocate(16) == nullptr);
  CHECK(tracking.stats().failed_allocations == 1);
  CHECK(tracking.stats().live_allocations == 0);
}

UI_TEST_CASE("allocate_unique constructs, destroys and frees") {
  struct Widget {
    int* destroyed;
    int value;
    ~Widget() { ++*destroyed; }
  };
  int destroyed = 0;
  {
    auto widget = allocate_unique<Widget>(memory(), &destroyed, 7);
    UI_REQUIRE(widget.has_value());
    CHECK((*widget)->value == 7);
    CHECK(tracker().stats().live_allocations == 1);
  }
  CHECK(destroyed == 1);
  CHECK(tracker().stats().live_allocations == 0);

  ui::test::FailingResource failing;
  auto failed = allocate_unique<int>(failing, 1);
  CHECK_FALSE(failed.has_value());
  CHECK(failed.error().code == ErrorCode::OutOfMemory);
}

UI_TEST_CASE("heap resource honors over-alignment") {
  HeapResource heap;
  for (std::size_t alignment = 1; alignment <= 8192; alignment *= 2) {
    void* p = heap.allocate(33, alignment);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % alignment == 0);
    heap.deallocate(p, 33, alignment);
  }
}
