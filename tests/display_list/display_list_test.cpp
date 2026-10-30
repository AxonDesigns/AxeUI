#include "display_list/display_list.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>

#include "display_list/builder.h"
#include "support/test_support.h"

using namespace ui::foundation;
using namespace ui::display_list;

namespace {

constexpr LinearRgbaPremul kRed{1, 0, 0, 1};
constexpr LinearRgbaPremul kWhite{1, 1, 1, 1};
constexpr TextureId kTextureA{1};
constexpr TextureId kTextureB{2};

Instance make_instance(PrimitiveKind kind) {
  Instance instance{};
  instance.kind = static_cast<std::uint32_t>(kind);
  return instance;
}

PhysicalRect bounds_of(const Instance& instance) {
  return {instance.bounds.left, instance.bounds.top, instance.bounds.right, instance.bounds.bottom};
}

PhysicalRect clip_of(const Instance& instance) {
  return {instance.clip.left, instance.clip.top, instance.clip.right, instance.clip.bottom};
}

bool is_integral(float v) { return std::floor(v) == v; }

}  // namespace

// The instance layout is locked in display_list/instance.h; repeat the
// contract here so a change shows up as a test failure as well.
static_assert(sizeof(Instance) == 96);
static_assert(sizeof(Instance) <= 96);
static_assert(std::is_trivially_copyable_v<Instance>);
static_assert(std::is_standard_layout_v<Instance>);
static_assert(offsetof(Instance, bounds) == 0);
static_assert(offsetof(Instance, uv) == 16);
static_assert(offsetof(Instance, clip) == 32);
static_assert(offsetof(Instance, corner_radii) == 48);
static_assert(offsetof(Instance, fill_color) == 64);
static_assert(offsetof(Instance, border_color) == 72);
static_assert(offsetof(Instance, border_width) == 80);
static_assert(offsetof(Instance, kind) == 84);
static_assert(offsetof(Instance, flags) == 88);
static_assert(offsetof(Instance, reserved) == 92);

TEST_CASE("instance colors are stored as half floats, packed in WGSL pair order") {
  Instance instance{};
  instance.fill_color = pack_color(LinearRgbaPremul{0.5f, 0.25f, 0.0f, 0.5f});
  CHECK(unpack_color(instance.fill_color) == LinearRgbaPremul{0.5f, 0.25f, 0.0f, 0.5f});

  // unpack2x16float(word) takes .x from the low 16 bits: word 0 must hold (r, g).
  std::uint32_t words[2];
  std::memcpy(words, reinterpret_cast<const std::byte*>(&instance) + 64, sizeof(words));
  CHECK((words[0] & 0xFFFFu) == 0x3800u);  // r = 0.5
  CHECK((words[0] >> 16) == 0x3400u);      // g = 0.25
  CHECK((words[1] & 0xFFFFu) == 0x0000u);  // b = 0
  CHECK((words[1] >> 16) == 0x3800u);      // a = 0.5
}

UI_TEST_CASE("untextured instances never split a batch") {
  DisplayList list(memory());
  for (int i = 0; i < 100; ++i) {
    list.append(make_instance(i % 2 == 0 ? PrimitiveKind::Rect : PrimitiveKind::RoundedRect),
                TextureId{});
  }
  UI_REQUIRE(list.commands().size() == 1);
  CHECK(list.commands()[0].first_instance == 0);
  CHECK(list.commands()[0].instance_count == 100);
  CHECK(list.commands()[0].texture.is_none());
}

UI_TEST_CASE("a texture change starts a new batch; untextured instances join any batch") {
  DisplayList list(memory());
  list.append(make_instance(PrimitiveKind::Rect), TextureId{});         // 0
  list.append(make_instance(PrimitiveKind::TexturedRect), kTextureA);   // 1 binds A
  list.append(make_instance(PrimitiveKind::Rect), TextureId{});         // 2
  list.append(make_instance(PrimitiveKind::Glyph), kTextureA);          // 3
  list.append(make_instance(PrimitiveKind::TexturedRect), kTextureB);   // 4 new batch
  list.append(make_instance(PrimitiveKind::RoundedRect), TextureId{});  // 5
  list.append(make_instance(PrimitiveKind::TexturedRect), kTextureB);   // 6
  list.append(make_instance(PrimitiveKind::TexturedRect), kTextureA);   // 7 new batch

  const auto commands = list.commands();
  UI_REQUIRE(commands.size() == 3);
  CHECK(commands[0].first_instance == 0);
  CHECK(commands[0].instance_count == 4);
  CHECK(commands[0].texture == kTextureA);
  CHECK(commands[1].first_instance == 4);
  CHECK(commands[1].instance_count == 3);
  CHECK(commands[1].texture == kTextureB);
  CHECK(commands[2].first_instance == 7);
  CHECK(commands[2].instance_count == 1);
  CHECK(commands[2].texture == kTextureA);

  // Commands tile the instance array exactly.
  std::uint32_t next = 0;
  for (const DrawCommand& command : commands) {
    CHECK(command.kind == CommandKind::DrawInstances);
    CHECK(command.first_instance == next);
    next += command.instance_count;
  }
  CHECK(next == list.instances().size());

  list.clear();
  CHECK(list.empty());
  CHECK(list.commands().empty());
}

UI_TEST_CASE("builder batches through the display list") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {400, 300}, ScaleFactor{1.0f});
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 10, 10), kRed);
  builder.draw_textured_rect(LogicalRect::from_xywh(10, 0, 10, 10), kTextureA, {0, 0, 1, 1},
                             kWhite);
  builder.draw_rounded_rect(LogicalRect::from_xywh(20, 0, 10, 10), CornerRadii::uniform(2), kRed);
  builder.draw_textured_rect(LogicalRect::from_xywh(30, 0, 10, 10), kTextureB, {0, 0, 1, 1},
                             kWhite);
  CHECK(list.instances().size() == 4);
  CHECK(list.commands().size() == 2);
}

UI_TEST_CASE("nested clips intersect and pop restores the outer clip") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {200, 100}, ScaleFactor{1.0f});
  CHECK(builder.current_clip() == PhysicalRect::from_ltrb(0, 0, 200, 100));

  builder.push_clip(LogicalRect::from_ltrb(10, 10, 150, 90));
  builder.push_clip(LogicalRect::from_ltrb(100, 0, 300, 50));
  CHECK(builder.current_clip() == PhysicalRect::from_ltrb(100, 10, 150, 50));
  CHECK(builder.clip_depth() == 2);

  builder.fill_rect(LogicalRect::from_ltrb(90, 0, 160, 60), kRed);  // Partially visible.
  UI_REQUIRE(list.instances().size() == 1);
  CHECK(clip_of(list.instances()[0]) == PhysicalRect::from_ltrb(100, 10, 150, 50));
  CHECK(bounds_of(list.instances()[0]) == PhysicalRect::from_ltrb(90, 0, 160, 60));

  builder.pop_clip();
  CHECK(builder.current_clip() == PhysicalRect::from_ltrb(10, 10, 150, 90));
  builder.pop_clip();
  CHECK(builder.current_clip() == PhysicalRect::from_ltrb(0, 0, 200, 100));

  // Disjoint clips leave nothing visible.
  builder.push_clip(LogicalRect::from_ltrb(0, 0, 10, 10));
  builder.push_clip(LogicalRect::from_ltrb(50, 50, 60, 60));
  CHECK(builder.current_clip().is_empty());
  builder.fill_rect(LogicalRect::from_ltrb(0, 0, 100, 100), kRed);
  CHECK(list.instances().size() == 1);
  CHECK(builder.culled_count() == 1);
  builder.pop_clip();
  builder.pop_clip();
}

UI_TEST_CASE("instances fully outside the clip are culled") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {100, 100}, ScaleFactor{1.0f});
  builder.push_clip(LogicalRect::from_ltrb(20, 20, 80, 80));
  builder.fill_rect(LogicalRect::from_ltrb(0, 0, 10, 10), kRed);        // Outside.
  builder.fill_rect(LogicalRect::from_ltrb(80, 20, 90, 30), kRed);      // Touches the edge only.
  builder.fill_rect(LogicalRect::from_ltrb(500, 500, 510, 510), kRed);  // Off-screen.
  builder.fill_rect(LogicalRect::from_ltrb(10, 10, 30, 30), kRed);      // Overlaps: kept.
  builder.fill_rect(LogicalRect::from_ltrb(30, 30, 40, 40), kRed);      // Inside: kept.
  builder.fill_rect(LogicalRect::from_ltrb(30, 30, 20, 40), kRed);      // Negative width: culled.
  builder.pop_clip();
  CHECK(list.instances().size() == 2);
  CHECK(builder.culled_count() == 4);
}

UI_TEST_CASE("pixel snapping at fractional scale factors") {
  for (const float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
    CAPTURE(scale);
    DisplayList list(memory());
    DisplayListBuilder builder(list, memory(), {4000, 4000}, ScaleFactor{scale});

    // A row of adjacent rects with fractional logical edges.
    const float edges[] = {0.0f, 0.3f, 1.1f, 1.7f, 2.5f, 3.33f, 5.0f, 7.8f, 8.0f, 10.4f};
    constexpr int kCount = static_cast<int>(sizeof(edges) / sizeof(edges[0])) - 1;
    for (int i = 0; i < kCount; ++i) {
      builder.fill_rect(LogicalRect::from_ltrb(edges[i], 0.0f, edges[i + 1], 1.0f), kRed);
    }
    UI_REQUIRE(list.instances().size() == static_cast<std::size_t>(kCount));

    for (int i = 0; i < kCount; ++i) {
      const PhysicalRect b = bounds_of(list.instances()[static_cast<std::size_t>(i)]);
      CAPTURE(i);
      CHECK(is_integral(b.left));
      CHECK(is_integral(b.top));
      CHECK(is_integral(b.right));
      CHECK(is_integral(b.bottom));
      CHECK(b.width() >= 1.0f);  // Thin rects never vanish.
      if (i > 0) {
        const PhysicalRect previous = bounds_of(list.instances()[static_cast<std::size_t>(i - 1)]);
        // Shared logical edge -> shared physical edge, unless the previous
        // rect collapsed (both edges rounded to the same pixel) and had to
        // grow to its 1px minimum.
        const bool previous_grew =
            std::floor(edges[i - 1] * scale + 0.5f) == std::floor(edges[i] * scale + 0.5f);
        if (!previous_grew) {
          CHECK(b.left == previous.right);
        }
      }
    }

    // Each edge lands on the nearest pixel boundary.
    for (int i = 0; i < kCount; ++i) {
      const PhysicalRect b = bounds_of(list.instances()[static_cast<std::size_t>(i)]);
      CHECK(std::fabs(b.left - edges[i] * scale) <= 0.5f);
    }
  }
}

UI_TEST_CASE("integer logical rects map exactly at integer scales") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {1000, 1000}, ScaleFactor{2.0f});
  builder.fill_rect(LogicalRect::from_xywh(3, 4, 10, 20), kRed);
  UI_REQUIRE(list.instances().size() == 1);
  CHECK(bounds_of(list.instances()[0]) == PhysicalRect::from_ltrb(6, 8, 26, 48));
}

UI_TEST_CASE("snap_to_pixels") {
  CHECK(snap_to_pixels(PhysicalRect::from_ltrb(0.4f, 0.6f, 10.5f, 9.49f)) ==
        PhysicalRect::from_ltrb(0, 1, 11, 9));
  // Collapsing rect keeps one pixel.
  CHECK(snap_to_pixels(PhysicalRect::from_ltrb(3.6f, 0, 3.9f, 0.2f)) ==
        PhysicalRect::from_ltrb(4, 0, 5, 1));
  // Empty input stays empty.
  CHECK(snap_to_pixels(PhysicalRect::from_ltrb(5, 5, 5, 9)).is_empty());
  // Negative coordinates snap consistently (half rounds up).
  CHECK(snap_to_pixels(PhysicalRect::from_ltrb(-2.5f, -1.5f, -0.5f, 0.5f)) ==
        PhysicalRect::from_ltrb(-2, -1, 0, 1));
}

UI_TEST_CASE("transforms apply to geometry; rotation is rejected") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {1000, 1000}, ScaleFactor{1.5f});
  CHECK(builder.push_transform(Affine2::translation(10, 20)).has_value());
  CHECK(builder.push_transform(Affine2::scale(2, 2)).has_value());
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 10, 10), kRed);
  UI_REQUIRE(list.instances().size() == 1);
  // Logical (10,20)-(30,40), times 1.5.
  CHECK(bounds_of(list.instances()[0]) == PhysicalRect::from_ltrb(15, 30, 45, 60));

  auto rotated = builder.push_transform(Affine2::rotation(0.3f));
  CHECK_FALSE(rotated.has_value());
  CHECK(rotated.error().code == ErrorCode::Unsupported);
  CHECK(builder.transform_depth() == 2);  // Unchanged.

  // Mirroring is still axis-aligned and is normalized.
  CHECK(builder.push_transform(Affine2::scale(-1, 1)).has_value());
  builder.fill_rect(LogicalRect::from_xywh(0, 0, 10, 10), kRed);
  builder.pop_transform();
  builder.pop_transform();
  builder.pop_transform();
  CHECK(builder.transform_depth() == 0);
  UI_REQUIRE(list.instances().size() == 2);
  // Logical (10-20, 20)-(10, 40): clipped by the viewport at x = 0, but bounds stay.
  CHECK(bounds_of(list.instances()[1]) == PhysicalRect::from_ltrb(-15, 30, 15, 60));

  // Clips are transformed too.
  CHECK(builder.push_transform(Affine2::translation(100, 0)).has_value());
  builder.push_clip(LogicalRect::from_xywh(0, 0, 10, 10));
  CHECK(builder.current_clip() == PhysicalRect::from_ltrb(150, 0, 165, 15));
  builder.pop_clip();
  builder.pop_transform();
}

UI_TEST_CASE("rounded rect radii and borders are scaled, snapped and clamped") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {1000, 1000}, ScaleFactor{2.0f});
  builder.draw_rounded_rect(LogicalRect::from_xywh(0, 0, 20, 10), {1, 2, 3, 100}, kRed,
                            Border{0.3f, kWhite});
  builder.draw_rounded_rect(LogicalRect::from_xywh(0, 0, 20, 10), CornerRadii{}, kRed);
  builder.draw_rounded_rect(LogicalRect::from_xywh(0, 0, 20, 10), CornerRadii::uniform(-4), kRed);
  UI_REQUIRE(list.instances().size() == 3);

  const Instance& bordered = list.instances()[0];
  CHECK(bordered.kind == static_cast<std::uint32_t>(PrimitiveKind::RoundedRect));
  CHECK(bordered.corner_radii.top_left == 2.0f);
  CHECK(bordered.corner_radii.top_right == 4.0f);
  CHECK(bordered.corner_radii.bottom_right == 6.0f);
  CHECK(bordered.corner_radii.bottom_left == 10.0f);  // Clamped to half the height.
  CHECK(bordered.border_width == 1.0f);               // 0.6 physical px -> at least 1.
  CHECK(bordered.flags == static_cast<std::uint32_t>(InstanceFlag::Border));
  CHECK(unpack_color(bordered.border_color) == kWhite);
  CHECK(bordered.reserved == 0);

  // No radii and no border degrade to a plain rect.
  CHECK(list.instances()[1].kind == static_cast<std::uint32_t>(PrimitiveKind::Rect));
  CHECK(list.instances()[2].kind == static_cast<std::uint32_t>(PrimitiveKind::Rect));
  CHECK(list.instances()[2].corner_radii.top_left == 0.0f);
}

UI_TEST_CASE("textured rects carry uv, tint and texture") {
  DisplayList list(memory());
  DisplayListBuilder builder(list, memory(), {100, 100}, ScaleFactor{1.0f});
  builder.draw_textured_rect(LogicalRect::from_xywh(0, 0, 8, 8), kTextureB,
                             TextureRect::from_ltrb(0.25f, 0.5f, 0.75f, 1.0f),
                             LinearRgbaPremul{0.5f, 0.5f, 0.5f, 0.5f});
  UI_REQUIRE(list.instances().size() == 1);
  const Instance& instance = list.instances()[0];
  CHECK(instance.kind == static_cast<std::uint32_t>(PrimitiveKind::TexturedRect));
  CHECK(instance.uv.left == 0.25f);
  CHECK(instance.uv.bottom == 1.0f);
  CHECK(unpack_color(instance.fill_color) == LinearRgbaPremul{0.5f, 0.5f, 0.5f, 0.5f});
  CHECK(list.commands()[0].texture == kTextureB);
}
