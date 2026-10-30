#include "foundation/geometry.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <type_traits>

#include "support/test_support.h"

namespace {

using namespace ui::foundation;

template <class A, class B>
concept Addable = requires(A a, B b) { a + b; };

template <class A, class B>
concept Comparable = requires(A a, B b) { a == b; };

// Units cannot be mixed by accident.
static_assert(Addable<LogicalPx, LogicalPx>);
static_assert(!Addable<LogicalPx, PhysicalPx>);
static_assert(!Comparable<LogicalPx, PhysicalPx>);
static_assert(!std::is_convertible_v<float, LogicalPx>);
static_assert(!std::is_convertible_v<LogicalPx, PhysicalPx>);
static_assert(!std::is_constructible_v<PhysicalPx, LogicalPx>);
static_assert(Addable<LogicalVec2, LogicalVec2>);
static_assert(!Addable<LogicalVec2, PhysicalVec2>);
static_assert(!std::is_convertible_v<LogicalRect, PhysicalRect>);
static_assert(!std::is_constructible_v<PhysicalRect, LogicalRect>);

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

}  // namespace

TEST_CASE("units convert only through an explicit scale factor") {
  const ScaleFactor scale{1.5f};
  CHECK(to_physical(LogicalPx{10.0f}, scale) == PhysicalPx{15.0f});
  CHECK(to_logical(PhysicalPx{15.0f}, scale) == LogicalPx{10.0f});
  CHECK(to_physical(LogicalRect::from_xywh(2, 4, 10, 20), scale) ==
        PhysicalRect::from_ltrb(3, 6, 18, 36));
  CHECK(to_logical(PhysicalSize{30, 60}, ScaleFactor{2.0f}) == LogicalSize{15, 30});
  CHECK(LogicalPx{3.0f} + LogicalPx{4.0f} == LogicalPx{7.0f});
  CHECK(LogicalPx{3.0f} < LogicalPx{4.0f});
  CHECK(LogicalPx{8.0f} / LogicalPx{2.0f} == doctest::Approx(4.0f));
}

TEST_CASE("scale factor validity") {
  CHECK(ScaleFactor::is_valid(1.0f));
  CHECK(ScaleFactor::is_valid(1.25f));
  CHECK_FALSE(ScaleFactor::is_valid(0.0f));
  CHECK_FALSE(ScaleFactor::is_valid(-1.0f));
  CHECK_FALSE(ScaleFactor::is_valid(kNaN));
  CHECK_FALSE(ScaleFactor::is_valid(std::numeric_limits<float>::infinity()));
}

TEST_CASE("empty and negative rects") {
  CHECK(LogicalRect{}.is_empty());
  CHECK(LogicalRect::from_xywh(0, 0, 0, 10).is_empty());
  CHECK(LogicalRect::from_xywh(0, 0, -5, 10).is_empty());
  CHECK(LogicalRect::from_ltrb(0, 0, kNaN, 10).is_empty());
  CHECK_FALSE(LogicalRect::from_xywh(0, 0, 1, 1).is_empty());

  const LogicalRect negative = LogicalRect::from_ltrb(10, 10, 0, 0);
  CHECK(negative.is_empty());
  CHECK_FALSE(negative.contains(LogicalVec2{5, 5}));
  CHECK(negative.normalized() == LogicalRect::from_ltrb(0, 0, 10, 10));

  const LogicalRect r = LogicalRect::from_xywh(0, 0, 10, 10);
  CHECK(r.intersection(negative) == LogicalRect{});
  CHECK_FALSE(r.intersects(negative));
  CHECK(r.union_with(negative) == r);
  CHECK(negative.union_with(r) == r);
  CHECK(negative.union_with(LogicalRect{}) == LogicalRect{});
  CHECK_FALSE(r.contains(negative));
  CHECK_FALSE(negative.contains(r));

  const LogicalSize empty_size{-1, 5};
  CHECK(empty_size.is_empty());
}

TEST_CASE("rect containment is half-open") {
  const LogicalRect r = LogicalRect::from_xywh(10, 20, 30, 40);
  CHECK(r.contains(LogicalVec2{10, 20}));
  CHECK(r.contains(LogicalVec2{39.99f, 59.99f}));
  CHECK_FALSE(r.contains(LogicalVec2{40, 30}));
  CHECK_FALSE(r.contains(LogicalVec2{20, 60}));
  CHECK_FALSE(r.contains(LogicalVec2{kNaN, 30}));
  CHECK(r.contains(r));
  CHECK(r.contains(LogicalRect::from_xywh(15, 25, 5, 5)));
  CHECK_FALSE(r.contains(LogicalRect::from_xywh(15, 25, 50, 5)));
}

TEST_CASE("rect intersection and union") {
  const LogicalRect a = LogicalRect::from_ltrb(0, 0, 10, 10);
  const LogicalRect b = LogicalRect::from_ltrb(5, 5, 15, 15);
  CHECK(a.intersection(b) == LogicalRect::from_ltrb(5, 5, 10, 10));
  CHECK(a.intersects(b));
  CHECK(a.union_with(b) == LogicalRect::from_ltrb(0, 0, 15, 15));

  const LogicalRect touching = LogicalRect::from_ltrb(10, 0, 20, 10);
  CHECK_FALSE(a.intersects(touching));
  CHECK(a.intersection(touching) == LogicalRect{});

  const LogicalRect disjoint = LogicalRect::from_ltrb(50, 50, 60, 60);
  CHECK(a.intersection(disjoint) == LogicalRect{});
  CHECK(a.union_with(disjoint) == LogicalRect::from_ltrb(0, 0, 60, 60));
}

TEST_CASE("rect accessors, insets and translation") {
  const LogicalRect r = LogicalRect::from_origin_size({1, 2}, {3, 4});
  CHECK(r.width() == 3);
  CHECK(r.height() == 4);
  CHECK(r.origin() == LogicalVec2{1, 2});
  CHECK(r.size() == LogicalSize{3, 4});
  CHECK(r.center() == LogicalVec2{2.5f, 4.0f});
  CHECK(r.translated({10, 10}) == LogicalRect::from_ltrb(11, 12, 14, 16));

  const LogicalRect box = LogicalRect::from_ltrb(0, 0, 100, 50);
  const LogicalInsets insets{1, 2, 3, 4};
  CHECK(box.inset(insets) == LogicalRect::from_ltrb(4, 1, 98, 47));
  CHECK(box.inset(insets).outset(insets) == box);
  CHECK(box.inset(LogicalInsets::uniform(30)).is_empty());
  CHECK(insets.horizontal() == 6);
  CHECK(insets.vertical() == 4);
  CHECK(LogicalInsets::symmetric(1, 2) == LogicalInsets{1, 2, 1, 2});
}

TEST_CASE("affine composition order") {
  const Affine2 t = Affine2::translation(10, 0) * Affine2::scale(2, 3);  // Scale, then translate.
  CHECK(t.transform_point(LogicalVec2{1, 1}) == LogicalVec2{12, 3});
  CHECK(t.transform_vector(LogicalVec2{1, 1}) == LogicalVec2{2, 3});
  CHECK(t.is_scale_translate());
  CHECK_FALSE(Affine2::rotation(0.5f).is_scale_translate());
  CHECK(Affine2::identity().is_identity());
  CHECK((Affine2::identity() * t) == t);
  CHECK((t * Affine2::identity()) == t);
}

TEST_CASE("affine inverse") {
  const Affine2 t = Affine2::translation(5, -3) * Affine2::rotation(0.7f) * Affine2::scale(2, 0.5f);
  auto inverse = t.inverse();
  UI_REQUIRE(inverse.has_value());
  CHECK(approx_equal(t * *inverse, Affine2::identity(), 1e-5f));
  CHECK(approx_equal(*inverse * t, Affine2::identity(), 1e-5f));

  const LogicalVec2 p{3, 4};
  const LogicalVec2 back = inverse->transform_point(t.transform_point(p));
  CHECK(back.x == doctest::Approx(p.x).epsilon(1e-5));
  CHECK(back.y == doctest::Approx(p.y).epsilon(1e-5));

  auto singular = Affine2::scale(0, 1).inverse();
  CHECK_FALSE(singular.has_value());
  CHECK(singular.error().code == ErrorCode::InvalidArgument);

  const Affine2 nan_transform{kNaN, 0, 0, 1, 0, 0};
  CHECK_FALSE(nan_transform.inverse().has_value());

  const float huge = std::numeric_limits<float>::max();
  CHECK_FALSE(Affine2::scale(huge, huge).inverse().has_value());  // Determinant overflows.
}

TEST_CASE("affine transform of rects") {
  const LogicalRect r = LogicalRect::from_ltrb(0, 0, 10, 20);
  CHECK(Affine2::scale(-1, 1).transform_rect(r) == LogicalRect::from_ltrb(-10, 0, 0, 20));

  const LogicalRect rotated = Affine2::rotation(std::numbers::pi_v<float> / 2).transform_rect(r);
  CHECK(rotated.left == doctest::Approx(-20.0f));
  CHECK(rotated.right == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(rotated.top == doctest::Approx(0.0f).epsilon(1e-5));
  CHECK(rotated.bottom == doctest::Approx(10.0f));

  CHECK(Affine2::translation(5, 5).transform_rect(LogicalRect{}) == LogicalRect{});
}
