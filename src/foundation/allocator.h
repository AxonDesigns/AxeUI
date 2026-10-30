#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "foundation/assertions.h"
#include "foundation/result.h"

namespace ui::foundation {

// Memory model
// ------------
// Every engine allocation goes through a MemoryResource passed in explicitly;
// there is no default or global resource. The engine root creates a
// TrackingResource on top of a HeapResource and hands it down, so the tracker
// sees everything the engine allocates.
//
// Why not std::pmr::memory_resource: on Apple platforms libc++ ships pmr in
// the system dylib and marks it available only from macOS 14 / iOS 17. Using
// it would make those the minimum deployment targets. MemoryResource has the
// same shape, so adapting to pmr later is a thin wrapper.

inline constexpr std::size_t kDefaultAlignment = alignof(std::max_align_t);

[[nodiscard]] constexpr bool is_power_of_two(std::size_t value) noexcept {
  return value != 0 && (value & (value - 1)) == 0;
}

// Rounds `value` up to a multiple of `alignment` (a power of two).
[[nodiscard]] constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
  return (value + alignment - 1) & ~(alignment - 1);
}

// Abstract source of raw memory.
//
// allocate() returns nullptr when memory is exhausted; it never aborts.
// Callers that can report failure turn that into ErrorCode::OutOfMemory.
// `alignment` must be a power of two. deallocate() must receive the same
// size and alignment that were passed to allocate().
class MemoryResource {
 public:
  MemoryResource() = default;
  MemoryResource(const MemoryResource&) = delete;
  MemoryResource& operator=(const MemoryResource&) = delete;
  virtual ~MemoryResource() = default;

  [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment = kDefaultAlignment) {
    UI_ASSERT(is_power_of_two(alignment), "alignment must be a power of two");
    return do_allocate(bytes, alignment);
  }

  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment = kDefaultAlignment) noexcept {
    if (pointer != nullptr) {
      do_deallocate(pointer, bytes, alignment);
    }
  }

  // Whether memory from `other` may be released through this resource.
  [[nodiscard]] bool is_equal(const MemoryResource& other) const noexcept {
    return this == &other || do_is_equal(other);
  }

 protected:
  virtual void* do_allocate(std::size_t bytes, std::size_t alignment) = 0;
  virtual void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept = 0;
  virtual bool do_is_equal(const MemoryResource& /*other*/) const noexcept { return false; }
};

// The system heap (malloc / aligned operator new). Holds no state, so any
// number of instances can coexist; each compares equal only to itself.
class HeapResource final : public MemoryResource {
 protected:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept override;
};

// Standard-library allocator that draws from a MemoryResource, so standard
// containers can use arenas and tracking. It has no default constructor:
// a container must be told where its memory comes from.
//
// Containers cannot report allocation failure without exceptions, so running
// out of memory here aborts via UI_ASSERT.
//
// Like std::pmr::polymorphic_allocator, the allocator does not propagate on
// container copy, move or swap: a container keeps its resource for life.
template <class T>
class Allocator {
 public:
  using value_type = T;
  using propagate_on_container_copy_assignment = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_swap = std::false_type;
  using is_always_equal = std::false_type;

  explicit Allocator(MemoryResource& resource) noexcept : resource_(&resource) {}

  template <class U>
  Allocator(const Allocator<U>& other) noexcept  // NOLINT(google-explicit-constructor)
      : resource_(&other.resource()) {}

  [[nodiscard]] T* allocate(std::size_t count) {
    UI_ASSERT(count <= std::numeric_limits<std::size_t>::max() / sizeof(T),
              "allocation size overflow");
    void* memory = resource_->allocate(count * sizeof(T), alignof(T));
    UI_ASSERT(memory != nullptr, "out of memory in container allocation");
    return static_cast<T*>(memory);
  }

  void deallocate(T* pointer, std::size_t count) noexcept {
    resource_->deallocate(pointer, count * sizeof(T), alignof(T));
  }

  [[nodiscard]] Allocator select_on_container_copy_construction() const noexcept { return *this; }

  [[nodiscard]] MemoryResource& resource() const noexcept { return *resource_; }

  template <class U>
  friend bool operator==(const Allocator& lhs, const Allocator<U>& rhs) noexcept {
    return lhs.resource().is_equal(rhs.resource());
  }

 private:
  MemoryResource* resource_;
};

template <class T>
using Vector = std::vector<T, Allocator<T>>;

using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

// Destroys and frees an object created by allocate_unique().
template <class T>
class ObjectDeleter {
 public:
  explicit ObjectDeleter(MemoryResource& resource) noexcept : resource_(&resource) {}

  void operator()(T* object) const noexcept {
    std::destroy_at(object);
    resource_->deallocate(object, sizeof(T), alignof(T));
  }

 private:
  MemoryResource* resource_;
};

template <class T>
using UniquePtr = std::unique_ptr<T, ObjectDeleter<T>>;

// Allocates and constructs a T from `resource`. Fails with OutOfMemory.
// Polymorphic types are rejected: the deleter frees sizeof(T) bytes, which
// would be wrong when destroying a derived object through a base pointer.
template <class T, class... Args>
[[nodiscard]] Result<UniquePtr<T>> allocate_unique(MemoryResource& resource, Args&&... args) {
  static_assert(!std::is_polymorphic_v<T> || std::is_final_v<T>,
                "allocate_unique requires a non-polymorphic or final type");
  void* memory = resource.allocate(sizeof(T), alignof(T));
  if (memory == nullptr) {
    return fail(Error{ErrorCode::OutOfMemory, "out of memory"});
  }
  T* object = std::construct_at(static_cast<T*>(memory), std::forward<Args>(args)...);
  return UniquePtr<T>(object, ObjectDeleter<T>(resource));
}

}  // namespace ui::foundation
