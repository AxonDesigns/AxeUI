#include <cstdint>

#include "foundation/slot_map.h"
#include "support/test_support.h"

using namespace ui::foundation;

TEST_CASE("handle bit layout is [0:11 | type:6 | generation:20 | index:27]") {
  const Handle h = Handle::make(HandleType::Texture, 0xABCDEu, 0x5432101u);
  CHECK(h.bits() == ((2ull << 47) | (0xABCDEull << 27) | 0x5432101ull));
  CHECK(h.type() == HandleType::Texture);
  CHECK(h.generation() == 0xABCDEu);
  CHECK(h.index() == 0x5432101u);
  CHECK(Handle::from_bits(h.bits()) == h);

  // Every handle fits in 53 bits: exact as a JavaScript number.
  const Handle max = Handle::make(static_cast<HandleType>(Handle::kMaxType),
                                  Handle::kMaxGeneration, Handle::kIndexMask);
  CHECK(max.bits() == (1ull << 53) - 1);
  CHECK(max.is_well_formed());
  CHECK_FALSE(Handle::from_bits(1ull << 53).is_well_formed());

  CHECK(Handle{}.is_null());
  CHECK(Handle{}.bits() == 0);
  CHECK_FALSE(h.is_null());
}

UI_TEST_CASE("slot map stores and returns objects") {
  SlotMap<int> map(memory(), HandleType::Node);
  auto a = map.emplace(10);
  auto b = map.emplace(20);
  UI_REQUIRE(a.has_value());
  UI_REQUIRE(b.has_value());
  CHECK(map.size() == 2);
  CHECK(a->type() == HandleType::Node);
  CHECK(a->generation() != 0);  // Generation 0 is never issued...
  CHECK(a->bits() != 0);        // ...so no issued handle is zero.
  CHECK(*map.get(*a).value() == 10);
  CHECK(*map.get(*b).value() == 20);

  *map.get(*a).value() = 11;
  const SlotMap<int>& const_map = map;
  CHECK(*const_map.get(*a).value() == 11);
  CHECK(map.contains(*b));

  auto removed = map.remove(*a);
  UI_REQUIRE(removed.has_value());
  CHECK(*removed == 11);
  CHECK(map.size() == 1);
}

UI_TEST_CASE("stale handles are rejected after removal and reuse") {
  SlotMap<int> map(memory(), HandleType::Node);
  const Handle old = *map.emplace(1);
  CHECK(map.remove(old).has_value());

  auto lookup = map.get(old);
  CHECK_FALSE(lookup.has_value());
  CHECK(lookup.error().code == ErrorCode::StaleHandle);
  CHECK(map.remove(old).error().code == ErrorCode::StaleHandle);

  const Handle reused = *map.emplace(2);
  CHECK(reused.index() == old.index());            // Same slot...
  CHECK(reused.generation() != old.generation());  // ...new generation.
  CHECK(map.get(old).error().code == ErrorCode::StaleHandle);
  CHECK(*map.get(reused).value() == 2);
}

UI_TEST_CASE("wrong-type, zero and forged handles are errors, not UB") {
  SlotMap<int> nodes(memory(), HandleType::Node);
  SlotMap<int> textures(memory(), HandleType::Texture);
  const Handle node = *nodes.emplace(1);
  const Handle texture = *textures.emplace(2);
  CHECK(node.index() == texture.index());
  CHECK(node.generation() == texture.generation());

  CHECK(nodes.get(texture).error().code == ErrorCode::WrongHandleType);
  CHECK(textures.get(node).error().code == ErrorCode::WrongHandleType);
  CHECK(nodes.get(Handle{}).error().code == ErrorCode::InvalidHandle);
  CHECK(nodes.get(Handle::make(HandleType::Node, 1, 9999)).error().code ==
        ErrorCode::InvalidHandle);
  // Right index, a generation that was never issued for it.
  CHECK(nodes.get(Handle::make(HandleType::Node, 7, node.index())).error().code ==
        ErrorCode::StaleHandle);
  CHECK(nodes.get(Handle::make(HandleType::Node, 0, node.index())).error().code ==
        ErrorCode::StaleHandle);
  // Bits above the 53-bit layout: rejected even if the low bits match.
  CHECK(nodes.get(Handle::from_bits(node.bits() | (1ull << 60))).error().code ==
        ErrorCode::InvalidHandle);
}

UI_TEST_CASE("freed slots are reused in FIFO order") {
  SlotMap<int> map(memory(), HandleType::Node);
  const Handle a = *map.emplace(0);
  const Handle b = *map.emplace(1);
  const Handle c = *map.emplace(2);
  CHECK(map.remove(b).has_value());
  CHECK(map.remove(a).has_value());
  CHECK(map.emplace(3)->index() == b.index());
  CHECK(map.emplace(4)->index() == a.index());
  CHECK(map.emplace(5)->index() == 3u);
  CHECK(*map.get(c).value() == 2);
}

UI_TEST_CASE("generation exhaustion retires the slot permanently") {
  SlotMap<int> map(memory(), HandleType::Node);
  Handle last;
  bool ok = true;
  // Cycle a single slot through every generation it can issue.
  for (std::uint32_t generation = 1; generation <= Handle::kMaxGeneration; ++generation) {
    auto handle = map.emplace(0);
    ok = ok && handle.has_value() && handle->index() == 0 && handle->generation() == generation;
    last = *handle;
    ok = ok && map.remove(last).has_value();
  }
  CHECK(ok);
  CHECK(last.generation() == Handle::kMaxGeneration);
  CHECK(map.retired_slots() == 1);

  // The retired slot is never handed out again.
  const Handle next = *map.emplace(1);
  CHECK(next.index() == 1u);
  CHECK(next.generation() == 1u);

  // No handle for index 0, of any generation, resolves any more.
  CHECK(map.get(last).error().code == ErrorCode::StaleHandle);
  CHECK(map.get(Handle::make(HandleType::Node, 1, 0)).error().code == ErrorCode::StaleHandle);
  CHECK(*map.get(next).value() == 1);
}

UI_TEST_CASE("slot map clear invalidates handles and destroys objects") {
  struct Counted {
    int* live;
    explicit Counted(int* l) : live(l) { ++*live; }
    Counted(Counted&& other) noexcept : live(other.live) { ++*live; }
    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    Counted& operator=(Counted&&) = delete;
    ~Counted() { --*live; }
  };
  int live = 0;
  {
    SlotMap<Counted> map(memory(), HandleType::Font);
    const Handle a = *map.emplace(&live);
    const Handle b = *map.emplace(&live);
    CHECK(live == 2);

    int visited = 0;
    map.for_each([&](Handle h, Counted&) {
      CHECK(h.type() == HandleType::Font);
      ++visited;
    });
    CHECK(visited == 2);

    map.clear();
    CHECK(live == 0);
    CHECK(map.empty());
    CHECK(map.get(a).error().code == ErrorCode::StaleHandle);
    CHECK(map.get(b).error().code == ErrorCode::StaleHandle);
    (void)map.emplace(&live);
  }
  CHECK(live == 0);  // Destructor destroys remaining objects.
}
