#pragma once

#include <cstddef>

#include "display_list/display_list.h"
#include "foundation/allocator.h"
#include "foundation/color.h"
#include "foundation/geometry.h"
#include "foundation/result.h"

namespace ui::display_list {

// Corner radii in logical pixels.
struct CornerRadii {
  float top_left = 0.0f;
  float top_right = 0.0f;
  float bottom_right = 0.0f;
  float bottom_left = 0.0f;

  [[nodiscard]] static constexpr CornerRadii uniform(float r) noexcept { return {r, r, r, r}; }
};

// An inner border. Width in logical pixels; zero means no border.
struct Border {
  float width = 0.0f;
  foundation::LinearRgbaPremul color;
};

// Rounds each edge of a physical rect to the nearest pixel boundary. Edges
// are snapped independently, so rects that share an edge before snapping
// share it after: no seams, no overlaps. A non-empty rect that would collapse
// (both edges rounding to the same pixel) keeps a width/height of one pixel,
// extending past its right/bottom edge: hairlines stay visible at the cost of
// overlapping a neighbour by one pixel.
[[nodiscard]] foundation::PhysicalRect snap_to_pixels(
    const foundation::PhysicalRect& rect) noexcept;

// Records draw calls into a DisplayList.
//
// Input is in logical pixels, in the coordinate space of the current
// transform. The builder applies the transform stack, converts to physical
// pixels, snaps edges to the pixel grid, intersects the clip stack, culls
// primitives that end up invisible, and lets DisplayList batch by texture.
//
// Limits (by design, for now):
// - Transforms must be scale + translate. Rotation or shear cannot be
//   expressed by an axis-aligned instance and is rejected by push_transform().
// - Clips are axis-aligned rects. Rotated / rounded clips need the reserved
//   CommandKind::ClipMask path.
// - Elliptical corners are not supported: under non-uniform scale, radii are
//   scaled by the smaller axis factor.
class DisplayListBuilder {
 public:
  // `viewport` (physical pixels) is the initial clip. `memory` holds the
  // builder's transform and clip stacks.
  DisplayListBuilder(DisplayList& output, foundation::MemoryResource& memory,
                     foundation::PhysicalSize viewport, foundation::ScaleFactor scale);
  ~DisplayListBuilder();

  // Starts a new frame: clears `output`, empties the stacks (keeping their
  // capacity, so a reused builder does not allocate) and sets a new viewport
  // and scale. Pushes and pops must be balanced before calling it.
  void reset(foundation::PhysicalSize viewport, foundation::ScaleFactor scale);

  [[nodiscard]] DisplayList& output() const noexcept { return *output_; }
  [[nodiscard]] foundation::ScaleFactor scale() const noexcept { return scale_; }

  DisplayListBuilder(const DisplayListBuilder&) = delete;
  DisplayListBuilder& operator=(const DisplayListBuilder&) = delete;

  // Composes `transform` onto the current one (applied first, then the
  // current). Fails with Unsupported, leaving the stack unchanged, if the
  // result has rotation or shear.
  [[nodiscard]] foundation::Result<void> push_transform(const foundation::Affine2& transform);
  void pop_transform();

  // Intersects `rect` (in the current transform's space) with the current
  // clip.
  void push_clip(const foundation::LogicalRect& rect);
  void pop_clip();

  void fill_rect(const foundation::LogicalRect& rect, foundation::LinearRgbaPremul color);
  void draw_rounded_rect(const foundation::LogicalRect& rect, const CornerRadii& radii,
                         foundation::LinearRgbaPremul fill, const Border& border = {});
  // `tint` multiplies the sampled texel (opaque white draws the texture as is).
  void draw_textured_rect(const foundation::LogicalRect& rect, TextureId texture,
                          const foundation::TextureRect& uv, foundation::LinearRgbaPremul tint);

  [[nodiscard]] const foundation::Affine2& current_transform() const noexcept {
    return transforms_.back();
  }
  [[nodiscard]] const foundation::PhysicalRect& current_clip() const noexcept {
    return clips_.back();
  }
  [[nodiscard]] std::size_t transform_depth() const noexcept { return transforms_.size() - 1; }
  [[nodiscard]] std::size_t clip_depth() const noexcept { return clips_.size() - 1; }
  // Primitives dropped because they were empty or entirely clipped.
  [[nodiscard]] std::size_t culled_count() const noexcept { return culled_; }

 private:
  [[nodiscard]] foundation::PhysicalRect to_device(
      const foundation::LogicalRect& rect) const noexcept;
  [[nodiscard]] float length_scale() const noexcept;
  void emit(const foundation::LogicalRect& rect, Instance instance, TextureId texture);

  DisplayList* output_;
  foundation::ScaleFactor scale_;
  foundation::Vector<foundation::Affine2> transforms_;
  foundation::Vector<foundation::PhysicalRect> clips_;
  std::size_t culled_ = 0;
};

}  // namespace ui::display_list
