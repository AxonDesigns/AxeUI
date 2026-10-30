#include "foundation/arena.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "support/test_support.h"

using namespace ui::foundation;

namespace {

bool all_bytes_equal(const void* memory, std::size_t size, std::uint8_t value) {
  const auto* bytes = static_cast<const std::uint8_t*>(memory);
  for (std::size_t i = 0; i < size; ++i) {
    if (bytes[i] != value) {
      return false;
    }
  }
  return true;
}

bool is_aligned(const void* pointer, std::size_t alignment) {
  return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

ArenaOptions bump_options(std::size_t block_size = 4096) {
  ArenaOptions options;
  options.block_size = block_size;
  options.passthrough = false;
  return options;
}

ArenaOptions passthrough_options() {
  ArenaOptions options;
  options.passthrough = true;
  return options;
}

}  // namespace

TEST_CASE("arena passthrough defaults to on exactly under AddressSanitizer") {
  CHECK(ArenaOptions{}.passthrough == kAddressSanitizer);
  CHECK(ArenaOptions{}.debug_fill == kDebugBuild);
}

UI_TEST_CASE("arena honors every power-of-two alignment") {
  for (const bool passthrough : {false, true}) {
    CAPTURE(passthrough);
    ArenaOptions options = bump_options();
    options.passthrough = passthrough;
    Arena arena(memory(), options);
    for (std::size_t alignment = 1; alignment <= 4096; alignment *= 2) {
      CAPTURE(alignment);
      void* odd = arena.allocate(3, 1);  // Misalign the cursor.
      void* p = arena.allocate(24, alignment);
      CHECK(odd != nullptr);
      CHECK(p != nullptr);
      CHECK(is_aligned(p, alignment));
    }
  }
}

UI_TEST_CASE("arena allocations do not overlap and zero-size requests are distinct") {
  Arena arena(memory(), bump_options());
  auto* a = static_cast<std::byte*>(arena.allocate(16, 8));
  auto* b = static_cast<std::byte*>(arena.allocate(16, 8));
  CHECK((b >= a + 16 || a >= b + 16));
  void* z1 = arena.allocate(0, 1);
  void* z2 = arena.allocate(0, 1);
  CHECK(z1 != nullptr);
  CHECK(z1 != z2);
}

UI_TEST_CASE("arena reset reuses standard blocks") {
  Arena arena(memory(), bump_options(1024));
  void* first = arena.allocate(100, 16);
  for (int i = 0; i < 50; ++i) {
    (void)arena.allocate(100, 16);  // Spill into several blocks.
  }
  const std::size_t reserved = arena.bytes_reserved();
  CHECK(reserved > 1024);
  CHECK(arena.bytes_allocated() == 51 * 100);

  arena.reset();
  CHECK(arena.bytes_allocated() == 0);
  CHECK(arena.bytes_reserved() == reserved);  // Blocks kept for the next frame.
  CHECK(arena.allocate(100, 16) == first);    // Same memory reused.

  for (int i = 0; i < 50; ++i) {
    (void)arena.allocate(100, 16);
  }
  CHECK(arena.bytes_reserved() == reserved);  // Steady state: no new blocks.
}

UI_TEST_CASE("arena oversize requests get dedicated blocks freed on reset") {
  Arena arena(memory(), bump_options(1024));
  (void)arena.allocate(64, 8);
  const std::size_t before = arena.bytes_reserved();
  void* big = arena.allocate(10000, 64);
  CHECK(big != nullptr);
  CHECK(is_aligned(big, 64));
  CHECK(arena.bytes_reserved() >= before + 10000);
  // The standard block stays current: small requests continue there.
  CHECK(arena.allocate(64, 8) != nullptr);

  arena.reset();
  CHECK(arena.bytes_reserved() == before);
}

UI_TEST_CASE("arena release returns everything upstream") {
  Arena arena(memory(), bump_options(1024));
  for (int i = 0; i < 20; ++i) {
    (void)arena.allocate(200, 8);
  }
  (void)arena.allocate(5000, 8);
  CHECK(tracker().stats().live_allocations > 0);
  arena.release();
  CHECK(arena.bytes_reserved() == 0);
  CHECK(tracker().stats().live_allocations == 0);
  CHECK(arena.allocate(8, 8) != nullptr);  // Still usable afterwards.
}

UI_TEST_CASE("arena debug fill marks fresh and reset memory with distinct patterns") {
  ArenaOptions options = bump_options(1024);
  options.debug_fill = true;
  Arena arena(memory(), options);

  void* p = arena.allocate(64, 8);
  CHECK(all_bytes_equal(p, 64, Arena::kFreshFill));
  std::memset(p, 0x11, 64);

  arena.reset();
  // The block is still owned by the arena; its old contents are now stale.
  CHECK(all_bytes_equal(p, 64, Arena::kResetFill));

  void* q = arena.allocate(64, 8);
  CHECK(q == p);
  CHECK(all_bytes_equal(q, 64, Arena::kFreshFill));
  CHECK(Arena::kFreshFill != Arena::kResetFill);
}

UI_TEST_CASE("arena without debug fill leaves memory alone") {
  ArenaOptions options = bump_options(1024);
  options.debug_fill = false;
  Arena arena(memory(), options);
  void* p = arena.allocate(16, 8);
  std::memset(p, 0x11, 16);
  arena.reset();
  CHECK(all_bytes_equal(arena.allocate(16, 8), 16, 0x11));
}

UI_TEST_CASE("arena passthrough gives each allocation its own upstream block") {
  Arena arena(memory(), passthrough_options());
  const std::size_t base = tracker().stats().live_allocations;
  void* a = arena.allocate(32, 8);
  void* b = arena.allocate(32, 256);
  void* c = arena.allocate(32, 8);
  CHECK(tracker().stats().live_allocations == base + 3);
  CHECK(is_aligned(b, 256));

  // Deallocation frees immediately, so a sanitizer sees exact lifetimes.
  arena.deallocate(b, 32, 256);
  CHECK(tracker().stats().live_allocations == base + 2);
  arena.deallocate(a, 32, 8);
  arena.deallocate(c, 32, 8);
  CHECK(tracker().stats().live_allocations == base);

  (void)arena.allocate(16, 8);
  (void)arena.allocate(16, 8);
  arena.reset();
  CHECK(tracker().stats().live_allocations == base);
  CHECK(arena.bytes_reserved() == 0);
}

UI_TEST_CASE("arena works as a standard container allocator") {
  for (const bool passthrough : {false, true}) {
    ArenaOptions options = bump_options(512);
    options.passthrough = passthrough;
    Arena arena(memory(), options);
    Vector<std::uint64_t> values{Allocator<std::uint64_t>(arena)};
    for (std::uint64_t i = 0; i < 1000; ++i) {
      values.push_back(i * i);
    }
    CHECK(values.size() == 1000);
    CHECK(values[999] == 999u * 999u);
    CHECK(is_aligned(values.data(), alignof(std::uint64_t)));

    String text{Allocator<char>(arena)};
    text.assign(200, 'x');
    CHECK(text.size() == 200);
  }
}

UI_TEST_CASE("arena reports upstream exhaustion as nullptr") {
  ui::test::FailingResource failing;
  Arena arena(failing, bump_options());
  CHECK(arena.allocate(16, 8) == nullptr);
  CHECK(arena.allocate(1 << 20, 8) == nullptr);
}
