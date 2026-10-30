// AxeUI display-list renderer.
//
// One render pipeline draws every primitive. Each display-list instance is
// expanded into a quad from the vertex index alone (6 vertices, no vertex or
// index buffer); the fragment shader evaluates the primitive as a signed
// distance in pixels and turns it into pixel coverage. Colors are linear and
// premultiplied; blending is One / OneMinusSrcAlpha.
//
// The Instance struct must match ui::display_list::Instance byte for byte
// (src/display_list/instance.h); tests/render/wgsl_layout_test.cpp checks it.

struct Instance {
  bounds: vec4<f32>,        // left, top, right, bottom (physical px)
  uv: vec4<f32>,            // u0, v0, u1, v1
  clip: vec4<f32>,          // left, top, right, bottom (physical px)
  corner_radii: vec4<f32>,  // top-left, top-right, bottom-right, bottom-left
  fill_color: vec2<u32>,    // 4 x f16: (r, g), (b, a)
  border_color: vec2<u32>,  // 4 x f16
  border_width: f32,
  kind: u32,
  flags: u32,
  reserved: u32,
}

struct Globals {
  viewport: vec2<f32>,  // Render target size in physical px.
  padding: vec2<f32>,
}

const KIND_RECT: u32 = 0u;
const KIND_ROUNDED_RECT: u32 = 1u;
const KIND_TEXTURED_RECT: u32 = 2u;
const KIND_GLYPH: u32 = 3u;
const FLAG_BORDER: u32 = 1u;

// True when the render target has no sRGB format or view (some browsers):
// the shader then encodes linear output to sRGB itself. Blending then happens
// on encoded values, which is the documented fallback.
override encode_srgb: bool = false;

@group(0) @binding(0) var<uniform> globals: Globals;
@group(0) @binding(1) var<storage, read> instances: array<Instance>;
@group(1) @binding(0) var color_texture: texture_2d<f32>;
@group(1) @binding(1) var color_sampler: sampler;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) @interpolate(flat) instance: u32,
}

@vertex
fn vs_main(@builtin(vertex_index) vertex: u32,
           @builtin(instance_index) index: u32) -> VertexOutput {
  let inst = instances[index];
  // Cover every pixel the primitive can touch: the bounds limited to the
  // clip, both grown by one pixel so partially covered edge pixels are
  // rasterized (their coverage is computed in the fragment shader).
  let lo = max(inst.bounds.xy, inst.clip.xy) - vec2<f32>(1.0);
  let hi = min(inst.bounds.zw, inst.clip.zw) + vec2<f32>(1.0);
  // Two triangles over the corners: (lt, rt, lb), (lb, rt, rb).
  var corners = array<vec2<f32>, 6>(
      vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
      vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0));
  let p = mix(lo, hi, corners[vertex]);
  let ndc = vec2<f32>(p.x / globals.viewport.x * 2.0 - 1.0, 1.0 - p.y / globals.viewport.y * 2.0);

  var out: VertexOutput;
  out.position = vec4<f32>(ndc, 0.0, 1.0);
  out.instance = index;
  return out;
}

fn unpack_color(c: vec2<u32>) -> vec4<f32> {
  return vec4<f32>(unpack2x16float(c.x), unpack2x16float(c.y));
}

// Signed distance from `p` (relative to the box center, y down) to a box
// with half-extent `half_size` and per-corner radii (tl, tr, br, bl).
fn sd_rounded_box(p: vec2<f32>, half_size: vec2<f32>, radii: vec4<f32>) -> f32 {
  var r = select(radii.z, radii.y, p.y < 0.0);  // Right side: tr above, br below.
  if (p.x < 0.0) {
    r = select(radii.w, radii.x, p.y < 0.0);    // Left side: tl above, bl below.
  }
  let q = abs(p) - half_size + vec2<f32>(r);
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2<f32>(0.0))) - r;
}

// Coverage of a pixel whose center is at signed distance `d` from an edge.
fn coverage(d: f32) -> f32 {
  return clamp(0.5 - d, 0.0, 1.0);
}

// Exact area of the unit pixel centered at `p` that lies inside `clip`.
fn clip_coverage(p: vec2<f32>, clip: vec4<f32>) -> f32 {
  let lo = max(p - vec2<f32>(0.5), clip.xy);
  let hi = min(p + vec2<f32>(0.5), clip.zw);
  let extent = max(hi - lo, vec2<f32>(0.0));
  return extent.x * extent.y;
}

fn linear_to_srgb(c: vec3<f32>) -> vec3<f32> {
  let low = c * 12.92;
  let high = 1.055 * pow(c, vec3<f32>(1.0 / 2.4)) - vec3<f32>(0.055);
  return select(high, low, c <= vec3<f32>(0.0031308));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  let inst = instances[in.instance];
  let p = in.position.xy;  // Pixel center in physical px.
  let size = inst.bounds.zw - inst.bounds.xy;
  let half_size = size * 0.5;
  let local = p - (inst.bounds.xy + inst.bounds.zw) * 0.5;
  let fill = unpack_color(inst.fill_color);

  // Sampled unconditionally (level 0, no derivatives) so it is valid in any
  // control flow; untextured batches bind a 1x1 white texture.
  let uv = mix(inst.uv.xy, inst.uv.zw, (p - inst.bounds.xy) / max(size, vec2<f32>(1e-6)));
  let texel = textureSampleLevel(color_texture, color_sampler, uv, 0.0);

  let outer = coverage(sd_rounded_box(local, half_size, inst.corner_radii));
  var color = fill * outer;

  switch inst.kind {
    case KIND_TEXTURED_RECT: {
      // Texels are premultiplied; the fill color is a premultiplied tint.
      color = texel * fill * outer;
    }
    case KIND_GLYPH: {
      // Placeholder until the text task: a one-pixel frame in the fill color.
      let inner_half = max(half_size - vec2<f32>(1.0), vec2<f32>(0.0));
      let inner = select(0.0, coverage(sd_rounded_box(local, inner_half, vec4<f32>(0.0))),
                         all(inner_half > vec2<f32>(0.0)));
      color = fill * max(outer - inner, 0.0);
    }
    default: {
      if ((inst.flags & FLAG_BORDER) != 0u && inst.border_width > 0.0) {
        let bw = inst.border_width;
        let inner_half = max(half_size - vec2<f32>(bw), vec2<f32>(0.0));
        let inner_radii = max(inst.corner_radii - vec4<f32>(bw), vec4<f32>(0.0));
        let inner = select(0.0, coverage(sd_rounded_box(local, inner_half, inner_radii)),
                           all(inner_half > vec2<f32>(0.0)));
        let border = unpack_color(inst.border_color);
        color = border * max(outer - inner, 0.0) + fill * inner;
      }
    }
  }

  color = color * clip_coverage(p, inst.clip);

  if (encode_srgb && color.a > 0.0) {
    color = vec4<f32>(linear_to_srgb(color.rgb / color.a) * color.a, color.a);
  }
  return color;
}
