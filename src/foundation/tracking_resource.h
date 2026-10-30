#pragma once

#include <cstddef>

#include "foundation/allocator.h"

namespace ui::foundation {

struct AllocationStats {
  std::size_t live_allocations = 0;    // Currently outstanding blocks.
  std::size_t live_bytes = 0;          // Bytes in outstanding blocks.
  std::size_t peak_allocations = 0;    // High-water mark of live_allocations.
  std::size_t peak_bytes = 0;          // High-water mark of live_bytes.
  std::size_t total_allocations = 0;   // Successful allocations since creation.
  std::size_t failed_allocations = 0;  // Requests the upstream could not satisfy.
};

// Forwards to an upstream resource and counts live and peak allocations and
// bytes. Install one at the engine root so stats() covers every engine
// allocation; the test harness uses it to fail tests that leak.
//
// Not thread-safe, like the rest of the single-threaded core. A parallel
// subsystem gets its own resources.
class TrackingResource final : public MemoryResource {
 public:
  explicit TrackingResource(MemoryResource& upstream) noexcept : upstream_(&upstream) {}

  [[nodiscard]] const AllocationStats& stats() const noexcept { return stats_; }
  [[nodiscard]] MemoryResource& upstream() const noexcept { return *upstream_; }

 protected:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept override;

 private:
  MemoryResource* upstream_;
  AllocationStats stats_;
};

}  // namespace ui::foundation
