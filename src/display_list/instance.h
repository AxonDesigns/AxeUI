#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "foundation/color.h"
#include "foundation/float16.h"

namespace ui::display_list {

// GPU instance format
// -------------------
// One primitive is one fixed-size Instance. The renderer draws N instances
// with 6 vertices each and no vertex or index buffer: the vertex shader picks
// the quad corner from vertex_index and expands `bounds`; the fragment shader
// evaluates the primitive's signed distance function for exact, antialiased
// edges, discards outside `clip`, and blends with premultiplied alpha.
//
// The layout is fixed by the static_asserts below and documented in
// docs/architecture.md. The matching WGSL storage-buffer struct is:
//
//   struct Instance {
//     bounds:       vec4<f32>,  // offset  0  left, top, right, bottom (physical px)
//     uv:           vec4<f32>,  // offset 16  u0, v0, u1, v1 (normalized)
//     clip:         vec4<f32>,  // offset 32  left, top, right, bottom (physical px)
//     corner_radii: vec4<f32>,  // offset 48  top-left, top-right, bottom-right, bottom-left
//     fill_color:   vec2<u32>,  // offset 64  4 x f16: unpack2x16float(.x) = rg, (.y) = ba
//     border_color: vec2<u32>,  // offset 72  4 x f16, same packing
//     border_width: f32,        // offset 80  physical px
//     kind:         u32,        // offset 84  PrimitiveKind
//     flags:        u32,        // offset 88  InstanceFlag bits
//     reserved:     u32,        // offset 92  must be 0
//   }                           // size 96, align 16, array stride 96
//
// As an instance-stepped vertex buffer the same bytes map to attributes
// float32x4 @0, @16, @32, @48; float16x4 @64, @72; float32 @80; uint32 @84,
// @88 (arrayStride 96). Colors are stored as f16 bits so neither path needs
// the optional WGSL shader-f16 feature.

// Primitive kinds. Values are shared with the shaders; append only.
enum class PrimitiveKind : std::uint32_t {
  Rect = 0,          // Solid axis-aligned rect, square corners.
  RoundedRect = 1,   // Per-corner radii, optional inner border.
  TexturedRect = 2,  // Samples the bound texture at `uv`, multiplied by fill_color.
  Glyph = 3,         // Reserved for the text task: glyph from an atlas texture.
  // 4..255: reserved for future kinds (gradients, shadows, nine-slice, ...).
};

[[nodiscard]] constexpr bool uses_texture(PrimitiveKind kind) noexcept {
  return kind == PrimitiveKind::TexturedRect || kind == PrimitiveKind::Glyph;
}

enum class InstanceFlag : std::uint32_t {
  None = 0,
  Border = 1u << 0,  // border_width > 0: draw an inner border in border_color.
};

struct PackedRect {
  float left;
  float top;
  float right;
  float bottom;
};

struct PackedRadii {
  float top_left;
  float top_right;
  float bottom_right;
  float bottom_left;
};

// Linear, premultiplied RGBA as four IEEE binary16 values.
struct PackedColor {
  std::uint16_t r;
  std::uint16_t g;
  std::uint16_t b;
  std::uint16_t a;
};

struct Instance {
  PackedRect bounds;
  PackedRect uv;
  PackedRect clip;
  PackedRadii corner_radii;
  PackedColor fill_color;
  PackedColor border_color;
  float border_width;
  std::uint32_t kind;
  std::uint32_t flags;
  std::uint32_t reserved;
};

// Layout lock: changing any of these requires updating the WGSL struct and
// docs/architecture.md together.
static_assert(std::is_trivially_copyable_v<Instance>);
static_assert(std::is_standard_layout_v<Instance>);
static_assert(sizeof(float) == 4);
static_assert(std::endian::native == std::endian::little,
              "f16 pair packing assumes a little-endian host (true for all targets)");
static_assert(sizeof(Instance) == 96);
static_assert(sizeof(Instance) % 16 == 0, "WGSL array stride must be a multiple of 16");
static_assert(alignof(Instance) == 4);
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

[[nodiscard]] inline PackedColor pack_color(foundation::LinearRgbaPremul c) noexcept {
  return {foundation::float_to_half_bits(c.r), foundation::float_to_half_bits(c.g),
          foundation::float_to_half_bits(c.b), foundation::float_to_half_bits(c.a)};
}

[[nodiscard]] inline foundation::LinearRgbaPremul unpack_color(PackedColor c) noexcept {
  return {foundation::half_bits_to_float(c.r), foundation::half_bits_to_float(c.g),
          foundation::half_bits_to_float(c.b), foundation::half_bits_to_float(c.a)};
}

}  // namespace ui::display_list
