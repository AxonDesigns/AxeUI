#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "foundation/allocator.h"
#include "foundation/handle.h"
#include "foundation/result.h"

namespace ui::foundation {

// Generational slot map: stores objects of type T and hands out Handles.
//
// - Every handle carries the map's HandleType; a handle of another type is
//   rejected with WrongHandleType.
// - Removing an object bumps its slot's generation, so existing handles to it
//   become stale (StaleHandle) rather than aliasing a later object.
// - A slot whose generation would wrap past Handle::kMaxGeneration is retired
//   permanently instead of being reused, so an old handle can never match a
//   new object. Retiring costs one slot per 2^20 - 1 reuses.
// - Freed slots are reused in FIFO order, spreading generation use across
//   slots.
//
// Every lookup returns a Result; invalid handles are errors, never UB.
template <class T>
class SlotMap {
 public:
  SlotMap(MemoryResource& memory, HandleType type) : slots_(Allocator<Slot>(memory)), type_(type) {
    UI_ASSERT(type != HandleType::None, "slot map needs a real handle type");
  }

  SlotMap(const SlotMap&) = delete;
  SlotMap& operator=(const SlotMap&) = delete;

  [[nodiscard]] HandleType type() const noexcept { return type_; }
  // Number of live objects.
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  // Number of slots retired after generation exhaustion.
  [[nodiscard]] std::size_t retired_slots() const noexcept { return retired_; }

  template <class... Args>
  [[nodiscard]] Result<Handle> emplace(Args&&... args) {
    std::uint32_t index = 0;
    if (free_head_ != kNoSlot) {
      index = free_head_;
      Slot& slot = slots_[index];
      free_head_ = slot.next_free;
      if (free_head_ == kNoSlot) {
        free_tail_ = kNoSlot;
      }
      slot.next_free = kNoSlot;
    } else {
      if (slots_.size() > Handle::kMaxIndex) {
        return fail(Error{ErrorCode::CapacityExceeded, "slot map index space exhausted"});
      }
      index = static_cast<std::uint32_t>(slots_.size());
      slots_.emplace_back();
    }
    Slot& slot = slots_[index];
    slot.value.emplace(std::forward<Args>(args)...);
    ++size_;
    return Handle::make(type_, slot.generation, index);
  }

  [[nodiscard]] Result<T*> get(Handle handle) noexcept {
    auto slot = find(*this, handle);
    if (!slot) {
      return fail(std::move(slot).error());
    }
    return &*(*slot)->value;  // NOLINT(bugprone-unchecked-optional-access): find() checked.
  }

  [[nodiscard]] Result<const T*> get(Handle handle) const noexcept {
    auto slot = find(*this, handle);
    if (!slot) {
      return fail(std::move(slot).error());
    }
    return &*(*slot)->value;  // NOLINT(bugprone-unchecked-optional-access): find() checked.
  }

  [[nodiscard]] bool contains(Handle handle) const noexcept { return get(handle).has_value(); }

  // Removes the object and returns it.
  [[nodiscard]] Result<T> remove(Handle handle) {
    auto found = find(*this, handle);
    if (!found) {
      return fail(std::move(found).error());
    }
    Slot& slot = **found;
    T value = std::move(*slot.value);  // NOLINT(bugprone-unchecked-optional-access)
    release_slot(slot, handle.index());
    return value;
  }

  // Removes every object. All outstanding handles become stale.
  void clear() {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      if (slots_[i].value.has_value()) {
        release_slot(slots_[i], static_cast<std::uint32_t>(i));
      }
    }
  }

  // Calls fn(Handle, T&) for every live object, in slot order. `fn` must
  // not insert or remove objects.
  template <class Fn>
  void for_each(Fn fn) {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      Slot& slot = slots_[i];
      if (slot.value.has_value()) {
        fn(Handle::make(type_, slot.generation, static_cast<std::uint32_t>(i)), *slot.value);
      }
    }
  }

  template <class Fn>
  void for_each(Fn fn) const {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
      const Slot& slot = slots_[i];
      if (slot.value.has_value()) {
        fn(Handle::make(type_, slot.generation, static_cast<std::uint32_t>(i)), *slot.value);
      }
    }
  }

 private:
  static constexpr std::uint32_t kNoSlot = 0xFFFFFFFFu;
  static constexpr std::uint32_t kRetiredGeneration = 0;  // Never issued.

  struct Slot {
    std::optional<T> value;
    std::uint32_t generation = 1;  // Generation of the current or next occupant.
    std::uint32_t next_free = kNoSlot;
  };

  // Shared by the const and non-const lookups; Self is SlotMap or const SlotMap.
  template <class Self>
  static auto find(Self& self, Handle handle) noexcept -> Result<decltype(&self.slots_[0])> {
    if (handle.is_null()) {
      return fail(Error{ErrorCode::InvalidHandle, "null handle"});
    }
    if (!handle.is_well_formed()) {
      return fail(Error{ErrorCode::InvalidHandle, "value is not a handle (bits above 52 set)"});
    }
    if (handle.type() != self.type_) {
      return fail(Error{ErrorCode::WrongHandleType, "handle refers to a different object type"});
    }
    if (handle.index() >= self.slots_.size()) {
      return fail(Error{ErrorCode::InvalidHandle, "handle index was never issued"});
    }
    auto& slot = self.slots_[handle.index()];
    if (!slot.value.has_value() || slot.generation != handle.generation()) {
      return fail(Error{ErrorCode::StaleHandle, "handle refers to a destroyed object"});
    }
    return &slot;
  }

  void release_slot(Slot& slot, std::uint32_t index) {
    slot.value.reset();
    --size_;
    if (slot.generation >= Handle::kMaxGeneration) {
      slot.generation = kRetiredGeneration;  // Retired: never reused.
      ++retired_;
      return;
    }
    ++slot.generation;
    if (free_tail_ == kNoSlot) {
      free_head_ = index;
    } else {
      slots_[free_tail_].next_free = index;
    }
    free_tail_ = index;
  }

  Vector<Slot> slots_;
  HandleType type_;
  std::uint32_t free_head_ = kNoSlot;
  std::uint32_t free_tail_ = kNoSlot;
  std::size_t size_ = 0;
  std::size_t retired_ = 0;
};

}  // namespace ui::foundation
