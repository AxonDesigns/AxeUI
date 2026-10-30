#include "foundation/tracking_resource.h"

#include <algorithm>

namespace ui::foundation {

void* TrackingResource::do_allocate(std::size_t bytes, std::size_t alignment) {
  void* pointer = upstream_->allocate(bytes, alignment);
  if (pointer == nullptr) {
    ++stats_.failed_allocations;
    return nullptr;
  }
  ++stats_.live_allocations;
  ++stats_.total_allocations;
  stats_.live_bytes += bytes;
  stats_.peak_allocations = std::max(stats_.peak_allocations, stats_.live_allocations);
  stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.live_bytes);
  return pointer;
}

void TrackingResource::do_deallocate(void* pointer, std::size_t bytes,
                                     std::size_t alignment) noexcept {
  UI_ASSERT(stats_.live_allocations > 0 && stats_.live_bytes >= bytes,
            "deallocation does not match any live allocation of this resource");
  --stats_.live_allocations;
  stats_.live_bytes -= bytes;
  upstream_->deallocate(pointer, bytes, alignment);
}

}  // namespace ui::foundation
