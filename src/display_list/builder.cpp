#include "display_list/builder.h"

#include <algorithm>
#include <cmath>

namespace ui::display_list {

using foundation::Affine2;
using foundation::LogicalRect;
using foundation::PhysicalRect;

namespace {

float snap(float value) noexcept { return std::floor(value + 0.5f); }

PackedRect pack(const foundation::Rect<foundation::PhysicalSpace>& r) noexcept {
  return {r.left, r.top, r.right, r.bottom};
}

// Clamps a physical radius to [0, limit]; NaN becomes 0.
float clamp_radius(float radius, float limit) noexcept {
  return radius > 0.0f ? std::min(radius, limit) : 0.0f;
}

Instance blank_instance(PrimitiveKind kind) noexcept {
  Instance instance{};
  instance.kind = static_cast<std::uint32_t>(kind);
  return instance;
}

}  // namespace

PhysicalRect snap_to_pixels(const PhysicalRect& rect) noexcept {
  PhysicalRect snapped{snap(rect.left), snap(rect.top), snap(rect.right), snap(rect.bottom)};
  if (!rect.is_empty()) {
    if (snapped.right <= snapped.left) {
      snapped.right = snapped.left + 1.0f;
    }
    if (snapped.bottom <= snapped.top) {
      snapped.bottom = snapped.top + 1.0f;
    }
  }
  return snapped;
}

DisplayListBuilder::DisplayListBuilder(DisplayList& output, foundation::MemoryResource& memory,
                                       foundation::PhysicalSize viewport,
                                       foundation::ScaleFactor scale)
    : output_(&output),
      scale_(scale),
      transforms_(foundation::Allocator<Affine2>(memory)),
      clips_(foundation::Allocator<PhysicalRect>(memory)) {
  transforms_.push_back(Affine2::identity());
  clips_.push_back(
      snap_to_pixels(PhysicalRect::from_xywh(0.0f, 0.0f, viewport.width, viewport.height)));
}

DisplayListBuilder::~DisplayListBuilder() {
  UI_DEBUG_ASSERT(transforms_.size() == 1 && clips_.size() == 1,
                  "unbalanced push/pop on DisplayListBuilder");
}

foundation::Result<void> DisplayListBuilder::push_transform(const Affine2& transform) {
  const Affine2 composed = transforms_.back() * transform;
  if (!composed.is_scale_translate()) {
    return foundation::fail(foundation::Error{
        foundation::ErrorCode::Unsupported,
        "display list transforms must be scale + translate (no rotation or shear)"});
  }
  transforms_.push_back(composed);
  return {};
}

void DisplayListBuilder::pop_transform() {
  UI_ASSERT(transforms_.size() > 1, "pop_transform without matching push_transform");
  transforms_.pop_back();
}

void DisplayListBuilder::push_clip(const LogicalRect& rect) {
  const PhysicalRect device = snap_to_pixels(to_device(rect));
  clips_.push_back(clips_.back().intersection(device));
}

void DisplayListBuilder::pop_clip() {
  UI_ASSERT(clips_.size() > 1, "pop_clip without matching push_clip");
  clips_.pop_back();
}

PhysicalRect DisplayListBuilder::to_device(const LogicalRect& rect) const noexcept {
  // Exact for scale + translate; also normalizes mirrored (negative-scale) rects.
  return foundation::to_physical(transforms_.back().transform_rect(rect), scale_);
}

float DisplayListBuilder::length_scale() const noexcept {
  const Affine2& t = transforms_.back();
  return scale_.value() * std::min(std::fabs(t.a), std::fabs(t.d));
}

void DisplayListBuilder::emit(const LogicalRect& rect, Instance instance, TextureId texture) {
  if (rect.is_empty()) {
    ++culled_;
    return;
  }
  const PhysicalRect bounds = snap_to_pixels(to_device(rect));
  const PhysicalRect& clip = clips_.back();
  if (bounds.is_empty() || !bounds.intersects(clip)) {
    ++culled_;
    return;
  }
  instance.bounds = pack(bounds);
  instance.clip = pack(clip);

  // Radii and border are clamped to what the snapped bounds can hold.
  const float limit = std::min(bounds.width(), bounds.height()) * 0.5f;
  PackedRadii& radii = instance.corner_radii;
  radii.top_left = clamp_radius(radii.top_left, limit);
  radii.top_right = clamp_radius(radii.top_right, limit);
  radii.bottom_right = clamp_radius(radii.bottom_right, limit);
  radii.bottom_left = clamp_radius(radii.bottom_left, limit);
  instance.border_width = clamp_radius(instance.border_width, limit);

  output_->append(instance, texture);
}

void DisplayListBuilder::fill_rect(const LogicalRect& rect, foundation::LinearRgbaPremul color) {
  Instance instance = blank_instance(PrimitiveKind::Rect);
  instance.fill_color = pack_color(color);
  emit(rect, instance, TextureId{});
}

void DisplayListBuilder::draw_rounded_rect(const LogicalRect& rect, const CornerRadii& radii,
                                           foundation::LinearRgbaPremul fill,
                                           const Border& border) {
  const float factor = length_scale();
  const bool has_border = border.width > 0.0f && std::isfinite(border.width);
  const bool has_radii = radii.top_left > 0.0f || radii.top_right > 0.0f ||
                         radii.bottom_right > 0.0f || radii.bottom_left > 0.0f;

  Instance instance =
      blank_instance(has_border || has_radii ? PrimitiveKind::RoundedRect : PrimitiveKind::Rect);
  instance.fill_color = pack_color(fill);
  instance.corner_radii = {radii.top_left * factor, radii.top_right * factor,
                           radii.bottom_right * factor, radii.bottom_left * factor};
  if (has_border) {
    // Borders snap to whole pixels and never vanish.
    instance.border_width = std::max(1.0f, snap(border.width * factor));
    instance.border_color = pack_color(border.color);
    instance.flags = static_cast<std::uint32_t>(InstanceFlag::Border);
  }
  emit(rect, instance, TextureId{});
}

void DisplayListBuilder::draw_textured_rect(const LogicalRect& rect, TextureId texture,
                                            const foundation::TextureRect& uv,
                                            foundation::LinearRgbaPremul tint) {
  UI_ASSERT(!texture.is_none(), "draw_textured_rect needs a texture");
  Instance instance = blank_instance(PrimitiveKind::TexturedRect);
  instance.uv = {uv.left, uv.top, uv.right, uv.bottom};
  instance.fill_color = pack_color(tint);
  emit(rect, instance, texture);
}

}  // namespace ui::display_list
