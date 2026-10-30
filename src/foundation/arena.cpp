#include "foundation/arena.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace ui::foundation {

namespace {

constexpr std::size_t kBlockAlignment = std::max<std::size_t>(kDefaultAlignment, 16);

}  // namespace

// Header placed at the start of every upstream block. The usable range
// starts at the header size rounded up to the block's alignment, so for a
// given alignment the header can be found again from a data pointer.
struct Arena::Block {
  Block* next = nullptr;
  Block* prev = nullptr;       // Only maintained for dedicated blocks.
  std::size_t total_size = 0;  // Bytes requested from upstream.
  std::size_t alignment = 0;   // Alignment requested from upstream.
  std::size_t capacity = 0;    // Usable bytes after the header.

  static constexpr std::size_t data_offset(std::size_t block_alignment) noexcept {
    return align_up(sizeof(Block), block_alignment);
  }

  std::byte* data() noexcept { return reinterpret_cast<std::byte*>(this) + data_offset(alignment); }
};

Arena::Arena(MemoryResource& upstream, ArenaOptions options)
    : upstream_(&upstream), options_(options) {
  UI_ASSERT(options_.block_size > 0, "arena block size must be positive");
}

Arena::~Arena() { release(); }

Arena::Block* Arena::create_block(std::size_t capacity, std::size_t alignment) noexcept {
  const std::size_t block_alignment = std::max(alignment, kBlockAlignment);
  const std::size_t offset = Block::data_offset(block_alignment);
  if (capacity > std::numeric_limits<std::size_t>::max() - offset) {
    return nullptr;
  }
  const std::size_t total = offset + capacity;
  void* memory = upstream_->allocate(total, block_alignment);
  if (memory == nullptr) {
    return nullptr;
  }
  Block* block = std::construct_at(static_cast<Block*>(memory));
  block->total_size = total;
  block->alignment = block_alignment;
  block->capacity = capacity;
  bytes_reserved_ += total;
  return block;
}

void Arena::destroy_block(Block* block) noexcept {
  const std::size_t total = block->total_size;
  const std::size_t alignment = block->alignment;
  bytes_reserved_ -= total;
  std::destroy_at(block);
  upstream_->deallocate(block, total, alignment);
}

void Arena::activate(Block* block) noexcept {
  current_ = block;
  cursor_ = block->data();
  limit_ = cursor_ + block->capacity;
}

void* Arena::bump(std::size_t bytes, std::size_t alignment) noexcept {
  if (cursor_ == nullptr) {
    return nullptr;
  }
  const auto address = static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(cursor_));
  const std::size_t padding = align_up(address, alignment) - address;
  const auto available = static_cast<std::size_t>(limit_ - cursor_);
  if (padding > available || bytes > available - padding) {
    return nullptr;
  }
  std::byte* result = cursor_ + padding;
  cursor_ = result + bytes;
  return result;
}

void* Arena::allocate_dedicated(std::size_t bytes, std::size_t alignment) noexcept {
  Block* block = create_block(bytes, alignment);
  if (block == nullptr) {
    return nullptr;
  }
  block->next = dedicated_;
  if (dedicated_ != nullptr) {
    dedicated_->prev = block;
  }
  dedicated_ = block;
  return block->data();
}

void* Arena::do_allocate(std::size_t bytes, std::size_t alignment) {
  if (bytes == 0) {
    bytes = 1;  // Distinct non-null pointers, like operator new.
  }

  void* result = nullptr;
  if (options_.passthrough) {
    result = allocate_dedicated(bytes, alignment);
  } else {
    result = bump(bytes, alignment);
    if (result == nullptr) {
      // A fresh standard block starts kBlockAlignment-aligned, so this is the
      // most padding the request can need there.
      const std::size_t worst_padding =
          alignment > kBlockAlignment ? alignment - kBlockAlignment : 0;
      const bool fits_standard =
          bytes <= options_.block_size && worst_padding <= options_.block_size - bytes;
      if (!fits_standard) {
        result = allocate_dedicated(bytes, alignment);
      } else {
        Block* next = current_ != nullptr ? current_->next : first_standard_;
        if (next == nullptr) {
          next = create_block(options_.block_size, kBlockAlignment);
          if (next == nullptr) {
            return nullptr;
          }
          if (current_ != nullptr) {
            current_->next = next;
          } else {
            first_standard_ = next;
          }
        }
        activate(next);
        result = bump(bytes, alignment);
        UI_ASSERT(result != nullptr, "arena block cannot hold a request that fits by size");
      }
    }
  }

  if (result == nullptr) {
    return nullptr;
  }
  bytes_allocated_ += bytes;
  if (options_.debug_fill) {
    std::memset(result, kFreshFill, bytes);
  }
  return result;
}

void Arena::do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) noexcept {
  if (!options_.passthrough) {
    return;  // Reclaimed by reset() / release().
  }
  // Passthrough: free the block now so the sanitizer catches later use.
  const std::size_t block_alignment = std::max(alignment, kBlockAlignment);
  auto* block = reinterpret_cast<Block*>(static_cast<std::byte*>(pointer) -
                                         Block::data_offset(block_alignment));
  UI_ASSERT(block->alignment == block_alignment && block->capacity >= bytes,
            "arena deallocation does not match an allocation");
  if (block->prev != nullptr) {
    block->prev->next = block->next;
  } else {
    dedicated_ = block->next;
  }
  if (block->next != nullptr) {
    block->next->prev = block->prev;
  }
  if (options_.debug_fill) {
    std::memset(block->data(), kResetFill, block->capacity);
  }
  destroy_block(block);
}

void Arena::free_dedicated_blocks() noexcept {
  Block* block = dedicated_;
  while (block != nullptr) {
    Block* next = block->next;
    if (options_.debug_fill) {
      std::memset(block->data(), kResetFill, block->capacity);
    }
    destroy_block(block);
    block = next;
  }
  dedicated_ = nullptr;
}

void Arena::reset() noexcept {
  free_dedicated_blocks();
  if (options_.debug_fill && current_ != nullptr) {
    // Blocks after current_ were not touched since they were last filled.
    for (Block* block = first_standard_; block != nullptr; block = block->next) {
      std::memset(block->data(), kResetFill, block->capacity);
      if (block == current_) {
        break;
      }
    }
  }
  current_ = nullptr;
  cursor_ = nullptr;
  limit_ = nullptr;
  bytes_allocated_ = 0;
}

void Arena::release() noexcept {
  reset();
  Block* block = first_standard_;
  while (block != nullptr) {
    Block* next = block->next;
    destroy_block(block);
    block = next;
  }
  first_standard_ = nullptr;
}

}  // namespace ui::foundation
