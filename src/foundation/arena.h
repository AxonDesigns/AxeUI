#pragma once

#include <cstddef>
#include <cstdint>

#include "foundation/allocator.h"
#include "foundation/build_config.h"

namespace ui::foundation {

struct ArenaOptions {
  // Capacity of each standard block. Requests that do not fit in one
  // (including alignment padding) get a dedicated block.
  std::size_t block_size = std::size_t{64} * 1024;

  // Fill allocated memory with Arena::kFreshFill and memory released by
  // reset() with Arena::kResetFill, so reads of uninitialized or stale arena
  // memory stand out in a debugger. On by default in debug builds.
  bool debug_fill = kDebugBuild;

  // Bypass bump allocation: every request becomes its own upstream block,
  // freed on deallocate() or reset(). On by default under AddressSanitizer,
  // so the sanitizer sees individual blocks and reports overflows and
  // use-after-reset precisely.
  bool passthrough = kAddressSanitizer;
};

// Bump allocator for short-lived data, typically reset once per frame.
//
// allocate() carves aligned ranges out of large upstream blocks.
// deallocate() is a no-op (except in passthrough mode); memory is reclaimed
// all at once by reset(), which keeps standard blocks for reuse, or by
// release(), which returns everything upstream. Objects in an arena are not
// destroyed by reset(): only put trivially destructible data in it, or
// destroy objects before resetting.
class Arena final : public MemoryResource {
 public:
  static constexpr std::uint8_t kFreshFill = 0xCD;  // Allocated, not yet written.
  static constexpr std::uint8_t kResetFill = 0xDD;  // Released by reset(): stale.

  explicit Arena(MemoryResource& upstream, ArenaOptions options = {});
  ~Arena() override;

  // Invalidates every allocation. Standard blocks are kept for reuse,
  // dedicated blocks are freed.
  void reset() noexcept;

  // Invalidates every allocation and returns all memory upstream.
  void release() noexcept;

  // Bytes handed out since the last reset (excluding alignment padding).
  [[nodiscard]] std::size_t bytes_allocated() const noexcept { return bytes_allocated_; }
  // Bytes currently held from the upstream resource.
  [[nodiscard]] std::size_t bytes_reserved() const noexcept { return bytes_reserved_; }
  [[nodiscard]] const ArenaOptions& options() const noexcept { return options_; }

 protected:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept override;

 private:
  struct Block;

  Block* create_block(std::size_t capacity, std::size_t alignment) noexcept;
  void destroy_block(Block* block) noexcept;
  void* bump(std::size_t bytes, std::size_t alignment) noexcept;
  void activate(Block* block) noexcept;
  void* allocate_dedicated(std::size_t bytes, std::size_t alignment) noexcept;
  void free_dedicated_blocks() noexcept;

  MemoryResource* upstream_;
  ArenaOptions options_;

  Block* first_standard_ = nullptr;  // Singly linked chain of standard blocks.
  Block* current_ = nullptr;         // Block being bumped; later ones are spares.
  std::byte* cursor_ = nullptr;
  std::byte* limit_ = nullptr;

  Block* dedicated_ = nullptr;  // Doubly linked: oversize and passthrough blocks.

  std::size_t bytes_allocated_ = 0;
  std::size_t bytes_reserved_ = 0;
};

}  // namespace ui::foundation
