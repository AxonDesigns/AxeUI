#include "foundation/allocator.h"

#include <cstdlib>
#include <new>

namespace ui::foundation {

void* HeapResource::do_allocate(std::size_t bytes, std::size_t alignment) {
  if (bytes == 0) {
    bytes = 1;  // Never return nullptr for a successful zero-size request.
  }
  if (alignment <= kDefaultAlignment) {
    return std::malloc(bytes);
  }
  return ::operator new(bytes, std::align_val_t{alignment}, std::nothrow);
}

void HeapResource::do_deallocate(void* pointer, std::size_t /*bytes*/,
                                 std::size_t alignment) noexcept {
  if (alignment <= kDefaultAlignment) {
    std::free(pointer);
  } else {
    ::operator delete(pointer, std::align_val_t{alignment});
  }
}

}  // namespace ui::foundation
