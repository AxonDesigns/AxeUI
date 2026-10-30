# AxeUI architecture

This document records the design decisions behind the engine. It covers
phase 1: foundation, platform contract, display list format and C API
(part 1), and the WebGPU renderer, frame driver, SDL3 desktop shell and
language-binding readiness (part 2).

## Fixed decisions

These were set at the start of the project. Changing any of them needs an
explicit decision, not a drive-by edit.

- **Language.** C++20 for the core; a thin C11 API (`include/ui/ui.h`) on top
  for language bindings (TypeScript, Dart, C, C++).
- **No exceptions, no RTTI**, on every target (GCC/Clang `-fno-exceptions
  -fno-rtti`, MSVC `/EHs-c- /GR-` plus `_HAS_EXCEPTIONS=0`). Fallible functions
  return `Result<T, E>`.
- **Memory.** Standard containers plus custom allocators (arenas, pools). No raw
  `new`/`delete` outside allocator code.
- **Build system.** CMake.
- **Targets.** Windows (MSVC), macOS (Clang), Linux (GCC and Clang), WebAssembly
  (Emscripten). iOS and Android come later, and nothing may block them.
- **Retained mode.** The core runs correctly on a single thread (browsers may
  not allow threads). Parallelism is an optional optimization.
- **The core never owns the main loop.** The platform owns it
  (requestAnimationFrame, CADisplayLink, Choreographer). Shells call into the
  core once per frame; the core asks for frames and never blocks or spins.
- **No OS-specific code in the core.** All platform access goes through the
  platform contract, which thin shells implement.
- **Multiple surfaces from day one.** Nothing assumes exactly one window.
- **Color.** Linear-space, premultiplied alpha throughout the pipeline.
- **Graphics.** WebGPU through Dawn's C++ API (`webgpu/webgpu_cpp.h`); the same
  renderer source compiles for the web with emdawnwebgpu. Shaders are WGSL,
  embedded at build time. The desktop shell uses SDL3.

### How the no-exceptions / no-RTTI rule is enforced

1. `ui_configure_target()` (`cmake/ui_build_settings.cmake`) applies the flags.
   `ui_verify_targets_configured()` runs at the end of configuration and fails
   if any compiled target skipped it.
2. `src/foundation/build_config.h`, which every engine header includes,
   `#error`s if `__cpp_exceptions`/`__EXCEPTIONS`/`_CPPUNWIND` or
   `__cpp_rtti`/`__GXX_RTTI`/`_CPPRTTI` is defined.
3. `tests/foundation/build_flags_test.cpp` repeats the check for the test
   targets on every CI platform.

## Modules

```
                 ┌──────────── shells/sdl3 (SDL3 desktop shell)
                 ▼
ui_capi ──► ui_platform ──► ui_foundation ◄── ui_display_list
                 ▲                                  ▲
ui_frame ────────┴─────── ui_render ────────────────┘
(frame driver)            (WebGPU / Dawn)
```

| Module | Namespace | Contents |
|---|---|---|
| `src/foundation` | `ui::foundation` | geometry, units, color, f16, `Result`/`Error`, assertions, `MemoryResource`, heap/tracking/arena resources, handles and slot map, logging, UTF-8 validation and UTF-16 offsets, flags, timestamps |
| `src/platform` | `ui::platform` | platform contract (`Platform`, `Surface`, `NativeSurfaceHandle`), `Event`, `Core`, `FrameHandler`, `HostShell`/`HostSurface`, `NullShell`/`NullSurface` |
| `src/display_list` | `ui::display_list` | GPU instance format, `DisplayList`, `DisplayListBuilder` |
| `src/render` | `ui::render` | `GpuContext`, `Renderer`, `TextureManager`, `shaders/ui.wgsl` |
| `src/frame` | `ui::frame` | `FrameDriver` |
| `src/capi` | `ui::capi` (+ `ui_*` C symbols) | C API implementation |
| `shells/sdl3` | `ui::sdl3` | `SdlShell`, `SdlWindow` |
| `api/`, `tools/gen_api.py`, `bindings/` | | API description, generator, generated TypeScript and Dart bindings |

The platform layer has no WebGPU types, and no SDL types exist outside
`shells/sdl3/`.

There is no global mutable state and no static constructor with side effects.
Every service (memory resource, logger) is an object owned by the engine root
and passed down by reference.

## Frame pipeline

```
 shell                               core
 ─────                               ────
 OS input  ──► Event(surface) ──► Core::dispatch(event)
                           │  event dispatch     (route to the node tree)
                           │  state update       (per-surface input, focus, lifecycle)
                           └► request_redraw(id) ──► Surface::request_frame()
 vsync     ──────────────► Core::frame(id, timestamp)
                           └► FrameHandler::render_surface(id, state, time)
                                  FrameDriver:  rebuild dirty nodes (later)
                                                layout (later)
                                                paint to display list (app callback today)
                                                render + present (Renderer)
```

Dispatch, state update, frame scheduling, paint (through an application
callback) and rendering are implemented. Rebuilding dirty nodes and layout
arrive with the element tree, which plugs in behind the `FrameHandler` seam.

Frame scheduling rules (`src/platform/core.cpp`), all per surface:

- `request_redraw(id)` marks the surface dirty and calls
  `Surface::request_frame()` at most once until that frame arrives. Requests
  are coalesced.
- No frames are requested while the application is suspended, while the
  surface is lost, or while it has zero size (minimized).
- `Suspend` and `SurfaceLost` forget outstanding requests, because a
  backgrounded browser tab may never deliver them. `Resume` and `Resize`
  request again.
- A `frame()` call that the core did not need is a cheap no-op. If something
  requests a redraw during a frame (an animation, a skipped present), the next
  frame is requested right after.

## Platform contract

- **Two entry points.** `Core::dispatch(const Event&)` and
  `Core::frame(SurfaceId, Timestamp)`. Every shell (desktop, web, mobile,
  embedding host, C API, test) uses exactly these. There is no test-only way
  to inject input.
- **`Platform`, process-wide** (`platform.h`): clipboard get/set and
  `wake()`, the only call allowed from any thread.
- **`Surface`, one per window/view/canvas** (`surface.h`): `info()` (physical
  size, scale factor, refresh rate), `native_handle()`, `request_frame()`,
  `set_cursor()`, `set_ime_cursor_area()`. A shell registers each surface with
  `Core::add_surface()`, which returns its `SurfaceId` (a handle with
  `HandleType::Surface`), and unregisters it with `remove_surface()`.
  Removal during dispatch is deferred until the outermost call returns.
- **`NativeSurfaceHandle`** (`native_surface.h`): a tagged, platform-neutral
  description of the OS object a GPU surface is made from: Win32 (HWND,
  HINSTANCE), a `CAMetalLayer` (macOS), Xlib (display, window), Wayland
  (display, surface), and reserved variants for Android (`ANativeWindow`),
  iOS (`CAMetalLayer`) and the web (canvas selector). Pointers are opaque, so
  the platform layer never includes graphics or windowing headers, and the
  renderer never includes SDL.
- **`Event`, platform to core.** Already converted to logical pixels. Every
  event except `Suspend`/`Resume` carries the `SurfaceId` it concerns. Pointer
  down/up/move/cancel (pointer id, kind mouse/touch/pen, buttons, pressure);
  scroll (pixel vs. line deltas); key down/up (physical key as a USB HID usage
  ID, logical key, modifiers, repeat); text input; IME preedit and commit;
  resize; scale factor change; focus gained/lost; surface lost; close
  requested; suspend; resume. Text is UTF-8 and is valid only during
  `dispatch()`. The core validates it and copies what it keeps. Events for
  unknown or removed surfaces are logged and dropped.
- **`HostShell` / `HostSurface`.** Shipping shells for when the application
  owns the windows: embedding, the C API, the future web and mobile glue, and
  tests. The app supplies sizes, scale factors and native handles, pushes
  events and ticks, and answers the core through callbacks.
- **`NullShell` / `NullSurface`.** Headless: fixed surface, no native handle
  (rendered offscreen), cursor and IME calls ignored, clipboard in memory,
  frame requests counted.

## Memory

- **`MemoryResource`** is the allocation interface: `allocate(bytes, align)`,
  which returns `nullptr` on exhaustion, and `deallocate(p, bytes, align)`.
  `Allocator<T>` adapts it for standard containers (`Vector<T>`, `String`). It
  has no default constructor, so a container must be told where its memory
  comes from. Containers cannot report failure without exceptions, so
  `Allocator<T>` aborts when out of memory; code that can report
  `OutOfMemory` uses the resource directly (see `allocate_unique`).
- **Why not `std::pmr`.** It works on GCC, MSVC and Emscripten (verified with
  Emscripten 6.0.13). But Apple's libc++ ships `memory_resource` in the system
  dylib and marks it `introduced = macOS 14.0 / iOS 17.0`
  (`_LIBCPP_INTRODUCED_IN_LLVM_16`). Using it would make those the minimum
  deployment targets. `MemoryResource` has the same shape, so an adapter to
  pmr is trivial if those minimums ever become acceptable.
- **`HeapResource`**: `malloc` for default alignment, aligned `operator new`
  above that.
- **`TrackingResource`** counts live and peak allocations and bytes, plus
  total and failed allocations. The engine root (the C API's `ui_context`, the
  test fixture) installs one over the heap and passes it everywhere, so its
  stats cover every engine allocation.
  `ui_context_get_allocation_stats()` exposes them.
- **`Arena`** is a bump allocator with per-frame `reset()`. It keeps standard
  blocks for reuse, gives oversize requests dedicated blocks, and returns
  everything on `release()`.
  - Debug fill: new allocations are filled with `0xCD`, and memory released by
    `reset()` with `0xDD`. Reading either pattern in a debugger means
    uninitialized or stale arena memory.
  - Passthrough: under AddressSanitizer (detected at compile time, overridable
    per arena), every allocation becomes its own upstream block, freed on
    `deallocate()`/`reset()`, so the sanitizer sees exact lifetimes.

## Errors, assertions, logging

- `Result<T, E = Error>` is an expected-style type and `[[nodiscard]]`.
  `Result<void>` is success when default-constructed. `UI_TRY(expr)` propagates
  errors. Calling `value()` on an error aborts.
- `Error` is an `ErrorCode` plus a message with static storage duration. The
  constructor takes a reference to a char array, which rejects plain pointers.
  Error codes mirror `ui_error_code` one to one (static-asserted).
- `UI_ASSERT(cond, msg)` is always on. `UI_DEBUG_ASSERT` is checked only when
  `UI_DEBUG` (no `NDEBUG`). Both report `file:line: assertion failed: msg
  (cond)` to stderr, then abort.
- `Logger` routes records (level, code, message, source location) to an
  application callback. Without one, library code prints nothing.

## Handles

Generational handles that fit in 53 bits, so they are exact numbers in
JavaScript and in Dart compiled to JavaScript:

```
bit 63      53 52        47 46                  27 26                 0
   [ 0 : 11   | type : 6   | generation : 20     | index : 27         ]
```

- The top 11 bits are always zero; a value with any of them set is rejected as
  `InvalidHandle`. The C API passes handles as `uint64_t`.
- The type tag (`HandleType`, at most 63 kinds) lets every lookup reject a
  handle of the wrong kind (`WrongHandleType`), not only stale ones.
- Generation 0 is never issued, so 0 is never a valid handle and serves as
  null.
- Removal bumps the slot's generation. A slot whose generation would pass
  `2^20 - 1` is retired permanently instead of wrapping.
- Freed slots are reused in FIFO order to spread generations across slots.
- Every lookup returns `Result`. Null, never-issued, stale, mistyped or forged
  handles are errors, never UB. In the C API they are also reported as
  diagnostics.

## Coordinates and color

- Geometry types (`Vec2`, `Size`, `Rect`, `Insets`) are parameterized by space:
  `LogicalSpace`, `PhysicalSpace` or `TextureSpace`. Scalars use
  `LogicalPx`/`PhysicalPx`. Mixing spaces does not compile; conversion goes
  only through `to_physical`/`to_logical` with a `ScaleFactor`.
- `Rect` stores edges and covers the half-open area `[left, right) x [top,
  bottom)`. Zero, negative or NaN extents are empty. Empty rects intersect
  nothing and do not contribute to unions.
- `Affine2` is a 2D affine transform; `inverse()` returns `Result`.
- Color types name their space and alpha convention: `SrgbRgba8Straight`,
  `LinearRgbaStraight`, `LinearRgbaPremul`. Conversions are explicit functions
  using the exact piecewise sRGB curves. All 256 8-bit values round-trip.

## Display list

The format the renderer consumes, designed for a WebGPU instanced pipeline.
One primitive is one 96-byte instance. The vertex shader expands it into a quad
from `vertex_index` alone (6 vertices, no vertex or index buffer). The fragment
shader evaluates the shape's signed distance function, applies the clip rect
by coverage, and outputs premultiplied color (see "Renderer").

### Instance layout

| Offset | Size | Field | Type | Meaning |
|---:|---:|---|---|---|
| 0 | 16 | `bounds` | 4 × f32 | left, top, right, bottom in physical px, pixel-snapped |
| 16 | 16 | `uv` | 4 × f32 | u0, v0, u1, v1 (normalized; textured kinds) |
| 32 | 16 | `clip` | 4 × f32 | axis-aligned clip rect, physical px |
| 48 | 16 | `corner_radii` | 4 × f32 | top-left, top-right, bottom-right, bottom-left, physical px |
| 64 | 8 | `fill_color` | 4 × f16 | linear premultiplied RGBA (tint for textured kinds) |
| 72 | 8 | `border_color` | 4 × f16 | linear premultiplied RGBA |
| 80 | 4 | `border_width` | f32 | physical px; 0 = no border |
| 84 | 4 | `kind` | u32 | `PrimitiveKind` |
| 88 | 4 | `flags` | u32 | `InstanceFlag` bits |
| 92 | 4 | `reserved` | u32 | must be 0 |
| | **96** | | | size 96, align 4 (CPU) / 16 (WGSL), stride 96 |

`static_assert`s in `src/display_list/instance.h` lock the size and every
offset. Matching WGSL storage-buffer struct:

```wgsl
struct Instance {
  bounds:       vec4<f32>,
  uv:           vec4<f32>,
  clip:         vec4<f32>,
  corner_radii: vec4<f32>,
  fill_color:   vec2<u32>,  // unpack2x16float(.x) = (r, g), (.y) = (b, a)
  border_color: vec2<u32>,
  border_width: f32,
  kind:         u32,
  flags:        u32,
  reserved:     u32,
}
```

Colors are stored as f16 bit patterns, not 8-bit sRGB, so gradients and
blending stay linear and precise. Neither consumption path needs the optional
`shader-f16` feature: a storage buffer unpacks them with `unpack2x16float`, and
an instance-stepped vertex buffer uses the `float16x4` vertex format.

Primitive kinds: `Rect` (0), `RoundedRect` (1, optional border), `TexturedRect`
(2), `Glyph` (3, reserved for the text task). Values 4–255 are reserved.

### Commands and batching

`DrawCommand { kind, first_instance, instance_count, texture }` means "draw
instances `[first, first + count)` with `texture` bound". Untextured instances
do not sample the texture, so they join the current command whatever it binds.
A new command starts only when a textured instance needs a different texture
from the one the current command already binds. Clip rects are per-instance,
so changing the clip never splits a batch.

`CommandKind::ClipMask` is reserved for clips that are not axis-aligned rects
(rotated or rounded clips, paths). They will need a mask pass; the builder
does not emit it yet.

### Builder

`DisplayListBuilder` takes logical-pixel input and keeps a transform stack and
a clip stack. For each primitive it:

1. transforms the rect by the current transform (scale + translate only;
   `push_transform` rejects rotation and shear with `Unsupported`);
2. converts to physical pixels with the scale factor;
3. snaps each edge to the nearest pixel boundary, so rects that share an edge
   still share it after snapping. A non-empty rect that would collapse keeps
   one pixel and may overlap its neighbour by one pixel;
4. culls it if it is empty or does not intersect the current clip (the
   intersection of all pushed clips, also snapped);
5. scales and clamps radii and border to the snapped bounds. Borders snap to
   whole pixels, minimum 1 px. Rounded rects with no radii and no border
   degrade to `Rect`;
6. appends it, and `DisplayList` batches by texture.

Limitations, by design for now: axis-aligned transforms and clips only; under
non-uniform scale, radii use the smaller axis factor (no elliptical corners).

## Renderer

`src/render/` draws display lists with WebGPU through Dawn's C++ API.

### Dawn: prebuilt packages, pinned

Two options were evaluated:

- **Building Dawn from source** with its CMake build and dependency
  fetching (`DAWN_FETCH_DEPENDENCIES`). It works on every platform but pulls
  several hundred MB of third-party sources and takes a long time on CI
  runners, even with caching.
- **Official prebuilt packages.** Since 2025, Dawn publishes daily GitHub
  releases (github.com/google/dawn/releases) with a static monolithic
  `libwebgpu_dawn` plus headers and a CMake package config
  (`dawn::webgpu_dawn`) for Ubuntu, macOS (arm64 and Intel) and Windows, and
  the matching `emdawnwebgpu_pkg` for Emscripten.

The renderer uses the **prebuilt packages**, pinned to release
`v20261016.190333` (Dawn commit `67d4d6e6`), downloaded and SHA-256-checked by
`cmake/ui_dependencies.cmake` into `.deps/`, which CI caches keyed on that
file. The Emscripten build uses the `emdawnwebgpu` package from the *same*
release (`--use-port=<pkg>/emdawnwebgpu.port.py`), so native and web compile
against the same `webgpu_cpp.h`. Consequences:

- MSVC: the package is built against the release CRT, so with the renderer
  enabled every target uses `/MD` and `_ITERATOR_DEBUG_LEVEL=0`, in Debug too
  (MSVC's debug iterators are off). It is built with MSVC 19.51 (Visual
  Studio 2026, GitHub image `windows-2025-vs2026`) and references STL
  helpers that older toolsets lack (`__std_min_element_8u`, `__std_rotate`,
  ...), so building on Windows requires VS 2026. Pick the Dawn release by
  the toolset it was built with when bumping it.
- The Linux package has no SwiftShader ICD; headless rendering uses Mesa
  lavapipe through the Vulkan loader (see "Golden images").
- `UI_DAWN_DIR` points the build at an already-extracted package; that is how
  a source build can be substituted later.

### Structure

- **`GpuContext`**: one `wgpu::Instance`, adapter, device and queue per
  process, shared by every surface. It requests no optional features and the
  default limits only, so the renderer runs on any conforming implementation,
  browsers included. Device-lost, uncaptured-error and Dawn logging callbacks
  go to the engine logger (non-capturing lambdas with a userdata pointer, so
  Dawn never heap-allocates closures). Native builds create the device
  synchronously (`WaitAny`). Under Emscripten that path returns `Unavailable`;
  the asynchronous browser flow arrives with the web shell.
- **One render pipeline** for every primitive (`shaders/ui.wgsl`), cached per
  target format and sRGB-encoding variant (a pipeline-overridable constant).
  The vertex shader expands each instance into two triangles from
  `vertex_index` alone (no vertex or index buffer), reading the instance from
  a storage buffer at `instance_index`. The quad covers the bounds limited to
  the clip rect, grown by one pixel. The fragment shader computes a signed
  distance in pixels to a box with four independent corner radii and turns it
  into coverage (`clamp(0.5 - d, 0, 1)`). An optional inner border is the
  difference with an inset box. Clipping is exact pixel-area coverage of the
  per-instance clip rect, so clip changes never split a batch.
- **Primitive kinds**: rect, rounded rect with border, textured rect (texels
  are premultiplied, the fill color is a premultiplied tint), and glyph (a
  one-pixel placeholder frame until the text task). Kinds 4–255 are free;
  gradients and shadows will take the next values.
- **Instance upload**: each render target (surface or offscreen) has a ring of
  `kFramesInFlight = 3` storage buffers with their bind groups. A buffer that
  is too small is replaced with one of `max(needed, 2 × current, 256
  instances)` and never shrunk.
- **Batching**: one draw call per display-list command
  (`Draw(6, count, 0, first)`). Untextured batches bind a 1×1 white texture
  (`TextureId{}`), so they never force a bind-group change.
- **Textures** (`TextureManager`): handle-addressed (`HandleType::Texture`),
  RGBA8 linear or RGBA8 sRGB. `update()` writes a CPU copy at once, applying
  premultiplication when the source is straight alpha. For sRGB textures that
  happens in linear light (decode, multiply, re-encode). The dirty rectangle is
  queued, and `flush()` uploads all queued rectangles once per frame straight
  from the copy. A destroyed texture referenced by a display list draws white
  and is counted in `FrameStats::missing_textures`.
- **Headless rendering**: `render_offscreen()` renders into an RGBA8-sRGB
  texture and optionally reads it back. Buffer copies use rows padded to
  256 bytes, unpadded when copied out. Surfaces without a native handle
  (`NullSurface`, tools, tests) get a per-surface offscreen target;
  `read_back(id)` returns its last frame.

### WGSL instance layout

The `Instance` struct in `ui.wgsl` must match `ui::display_list::Instance`
byte for byte (table in "Display list > Instance layout"). Colors are pairs of
f16 packed in `vec2<u32>` and read with `unpack2x16float`, so no `shader-f16`
feature is needed. `tests/render/renderer_test.cpp` parses the embedded WGSL,
computes each field's offset with WGSL's alignment rules, and fails if any
name, offset, size or the array stride differs from the C++ layout.

### Color output

Blending must happen in linear space, so the renderer picks, per surface:

1. **sRGB surface format** (`BGRA8UnormSrgb`/`RGBA8UnormSrgb`) when the
   surface offers one: the hardware encodes and blends linearly.
2. **Non-sRGB format plus an sRGB view format** when the surface's formats are
   all non-sRGB, which is what browsers do (`bgra8unorm` / `rgba8unorm`).
   WebGPU allows the sRGB variant as a view format for canvases, and natively
   the configuration is validated in an error scope.
3. **Shader encoding** (`encode_srgb` pipeline constant) when neither works.
   Blending then happens on encoded values. This is the documented fallback.

Expected path per platform: Vulkan on Linux and Windows, Metal on macOS, and
D3D12 on Windows normally offer an sRGB format (path 1); browsers use path 2.
`Renderer::color_output_path(id)` reports the choice and the renderer logs a
warning on path 3. **Verified here:** only the offscreen path (RGBA8-sRGB,
Linux/lavapipe). The surface paths could not be exercised on the machine this
was built on (no display), so the per-platform list is the expectation from
the WebGPU spec and Dawn's backends, not a measurement.

### Surface status handling

`render_surface()` handles every status `GetCurrentTexture` can return in this
Dawn version: `SuccessOptimal` (present); `SuccessSuboptimal` (present, then
reconfigure next frame); `Timeout` (skip, retry next frame); `Outdated`
(reconfigure, skip); `Lost` (recreate the `wgpu::Surface` from the native
handle, reconfigure, skip); `Error` (device loss is checked first; otherwise
skip and reconfigure). The prompt's "out of memory" and "device lost"
statuses no longer exist as separate values in current `webgpu.h`; both
surface as `Error`. A zero-size (minimized) surface is skipped before any of
this. The frame driver turns every skip into a new frame request.

These paths need a real swapchain. Dawn offers no way to force a surface
status headlessly, so they are **not covered by automated tests** and could
not be verified by hand on the build machine. To verify by hand on a desktop:
resize a window quickly (Outdated/Suboptimal); minimize and restore (zero
size); on Windows, lock the screen or change the display mode (Lost); watch
the log for "skipped" outcomes followed by correct frames.

### Device loss

The device-lost callback only sets an atomic flag; it may fire on another
thread. At the start of every render call, `ensure_device()` sees the flag and
recreates the adapter and device, then rebuilds everything that belongs to
the device: shader, layouts, pipelines, instance rings, offscreen targets and
surface configurations (the `wgpu::Surface` objects belong to the instance and
are kept). Textures are restored from their **CPU copies**. That was chosen
over per-texture restore callbacks so recovery needs no application code and
works the same in every binding, at the cost of keeping one CPU copy per
texture. The frame during which loss is detected is skipped and retried.
`Renderer::simulate_device_loss()` uses Dawn's `ForceLoss` (native only); the
test renders a textured scene, forces the loss, renders again and checks that
the pixels are identical and textures restored.

## Frame driver and multi-surface presentation

`FrameDriver` (`src/frame/`) is the `FrameHandler` the core calls from
`Core::frame()`. For each surface with a pending frame it resets that
surface's `DisplayListBuilder` to its size and scale, calls the application's
`paint(SurfaceId, DisplayListBuilder&)` callback, then renders and presents.
It is deliberately small: it is where the element tree will plug in, with
"paint the tree for this surface" replacing the callback. Each surface keeps
its display list and builder across frames, so a steady-state frame makes no
engine allocations. This is verified with the allocation tracker:
`renderer_test.cpp` renders 60 frames on two surfaces after warm-up and
checks the allocation count is unchanged.

**Presentation strategy.** Surfaces are rendered and presented
independently. With a blocking vsync present mode (Fifo), presenting several
windows back to back can make each present wait for its own vsync. That
serializes them and divides the frame rate by the number of windows. The
renderer therefore:

- configures **Mailbox** wherever the surface offers it (`PresentPolicy::
  LowLatency`, the default). Mailbox is tear-free and never blocks, so windows
  cannot stall each other;
- falls back to **Fifo** elsewhere (always available);
- leaves pacing to the shell. The SDL shell delivers each window's frame no
  earlier than one refresh period (from that window's display) after the
  previous one. With Mailbox this pacing is what keeps a window from rendering
  frames it cannot show; with Fifo it keeps windows from queuing more than
  one frame each.

**Not measured.** Measuring this needs two real windows on a display, and the
machine this was built on has none (a headless VM). The two-window frame rate
with Fifo and Mailbox is still to be recorded on each desktop OS, using the
example with two continuously animating windows and the shell's frame
timestamps. Record the numbers here.

## SDL3 desktop shell

`shells/sdl3/` implements the platform contract with SDL 3.4.18, built from
the pinned source release with only the video and events subsystems.

- **Windows**: `SdlShell::create_window()` creates SDL windows with
  high-pixel-density enabled and registers each with the core. Each window
  reports its pixel size, display scale (`SDL_GetWindowDisplayScale`) and
  refresh rate. Size, scale and display changes (including moving to a
  monitor with a different scale) dispatch `ScaleFactorChanged`/`Resize`.
  Minimizing reports a zero size, so no frames are requested and Fifo presents
  cannot block. Drivers that learn a window's size only from the first
  configure event (Wayland) are covered: the shell estimates the size until
  the real one arrives as a Resize.
- **Native handles**: Win32 HWND/HINSTANCE, Xlib display/window and Wayland
  display/surface from SDL window properties. On macOS the shell creates a
  `CAMetalLayer`-backed view with `SDL_Metal_CreateView` (window flag
  `SDL_WINDOW_METAL`) and hands over its layer.
- **Input**: the mouse, touch fingers and pens become pointers with
  non-colliding ids (mouse 0, touch `1<<62 | finger`, pen `2<<62 | pen`).
  Held buttons are tracked from the events themselves. Touch- and
  pen-synthesized mouse events are ignored. Wheel input is
  `ScrollUnit::Lines`, with fractional values from precise devices, because
  SDL reports notches, not pixels. Keys: the SDL scancode is the HID usage
  (`PhysicalKey`), plus the logical key from the keycode, modifiers and
  repeat. Text input and IME preedit/commit are per window (preedit cursors
  converted from characters to UTF-8 bytes), and the IME candidate area is
  forwarded with `SDL_SetTextInputArea`.
- **Clipboard, cursors** (system cursors created lazily), and a
  **thread-safe wake** (`SDL_PushEvent` of a registered event type).
- **Loop**: the application owns it: `while (shell.pump()) { ... }`. `pump()`
  sleeps in `SDL_WaitEventTimeout`, indefinitely when no window wants a
  frame, otherwise until the earliest frame deadline. It never busy-loops.
- **Hazards from "Known platform hazards"**: (1) during the Windows modal
  move/resize loop and macOS live resize, SDL sends live-resize expose events
  (`data1 == 1`) to event watchers, and the shell renders the window from its
  watcher. (2) All window calls assert the main thread; only `wake()` may come
  from elsewhere.

`examples/hello_surfaces` opens two windows. The main one draws bordered
rounded rects with different radii, nested clips, a textured rect, and a
button that changes color on hover and click (rect containment). The second
draws a grid of circles and can be closed on its own; closing the main
window quits.

## Language bindings

The engine is used from C, C++, TypeScript and Dart. The C API is the
boundary; everything else is generated from a description of it.

- **Source of truth**: `api/ui_api.json` describes enums, structs, callbacks
  and functions, recording ownership, nullability, out-parameters, string
  encoding and thread rules. `tools/gen_api.py` (Python standard library only)
  generates:
  - `include/ui/ui.h`, the C11 header;
  - `bindings/typescript/ui_module.d.ts` (the raw Emscripten module) and
    `bindings/typescript/ui.ts` (a thin wrapper: BigInt ↔ number handles,
    UTF-8 strings, struct marshaling, out-parameters, callbacks through
    `addFunction`);
  - `bindings/dart/lib/src/ui_bindings.dart` (`dart:ffi`);
  - `bindings/wasm/exported_functions.json`.

  `tools/gen_api.py --check` (a CTest test and a CI job) fails when any
  generated file is out of date.
- **ABI rules**, enforced by the generator and documented in `ui.h`: no structs
  or unions by value (structs only by pointer, no unions or bitfields);
  fixed-width integers only; enums cross as `int32_t` (typedef plus
  constants); booleans as `uint8_t`; every struct starts with `uint32_t size`
  so fields can be appended. Input structs shorter than the library's are
  zero-extended; output structs are never written past the caller's `size`.
  Structs have no interior padding on 64-bit or wasm32.
- **Handles** are `uint64_t` with the top 11 bits zero (see "Handles"); the
  TypeScript wrapper converts them to exact numbers.
- **Strings**: UTF-8, pointer plus `uint32_t` byte length; every string
  parameter, field and return value documents its ownership. Text positions:
  any API exposing them must state UTF-8 bytes vs. UTF-16 code units;
  `ui_utf8_to_utf16_offset` and `ui_utf16_to_utf8_offset` convert, rejecting
  offsets inside a code point or surrogate pair.
- **Event queue**: engine-to-host events (`UI_EVENT_LOG`,
  `UI_EVENT_FRAME_REQUESTED`, `UI_EVENT_WAKE`) are queued and drained with
  `ui_context_poll_event()`. A polled message stays valid until the next poll.
  The queue is bounded at 4096 entries; overflow is counted and dropped. A log
  callback and an event callback are optional conveniences that bypass the
  queue.
- **Reentrancy**: API calls from inside callbacks are safe. Each entry point
  holds an `ApiScope`. `ui_surface_destroy()` during dispatch marks the
  handle stale at once and destroys the surface when the outermost call
  returns (`Core::remove_surface` has the same rule internally).
  `ui_context_destroy()` from a callback is refused with a diagnostic.
- **Threading**: a context belongs to the thread that created it; any other
  thread gets `UI_ERROR_WRONG_THREAD`. `ui_context_wake()` is the exception:
  it only touches atomics, queues one coalesced `UI_EVENT_WAKE`, and calls the
  host's thread-safe wake callback.
- **Artifacts**: `ui_shared` builds `libaxeui` (`.so`/`.dylib`/`.dll`). All
  code is compiled with hidden visibility, so only `UI_API` (`ui_*`) functions
  are exported; on Linux, static archives are also excluded from export, and a
  CTest checks the exported symbols on Linux and macOS. Under Emscripten,
  `ui_wasm` builds an ES module (`axeui.mjs` + `axeui.wasm`; `MODULARIZE`,
  `EXPORT_ES6`) exporting exactly the generated function list.
- **Smoke tests** (CTest, CI): TypeScript runs under Node (native type
  stripping) against `axeui.mjs`, and the wrapper is type-checked with the
  pinned `tsc`. Dart runs `bin/smoke.dart` against `libaxeui` with
  `NativeCallable`. Both call the version function, register the log
  callback, and trigger errors with stale and forged handles.

The context root is a pointer, not a handle, because resolving a handle needs
a table, and a process-wide table would be global mutable state.

## Testing

- doctest, vendored, in no-exceptions mode. doctest rejects `REQUIRE` at
  compile time when exceptions are off; `UI_REQUIRE` records a failure and
  returns from the test instead.
- **Leak checking.** `UI_TEST_CASE` runs each test in `LeakCheckedFixture`,
  which owns a `TrackingResource`. When the test case ends, the fixture fails
  it if any allocation is still live. `leak_check_test.cpp` contains a
  deliberate leak marked `doctest::should_fail()`: if the checker stopped
  catching leaks, that test would fail the suite. The leaked block is freed
  after the check, so LeakSanitizer stays clean.
- **Death test.** `ui_assert_death_test` runs through `cmake/ui_expect_abort.cmake`,
  which expects a non-zero exit plus the assertion message (also under Node).
- Platform tests drive `Core` only through `HostShell`/`HostSurface` with
  recording callbacks: the same path a real embedder uses.
- Renderer tests need a WebGPU adapter. Where there is none (Node), they are
  skipped with a message. They print every diagnostic the renderer logs and
  fail on errors only: warnings differ between drivers and backends (e.g.
  Dawn's Metal backend on CI's paravirtual GPU).
- The SDL3 shell is tested on SDL's `dummy` video driver: real SDL windows and
  event loop with injected SDL events and no display (windows have no native
  surface, so frames go through the headless path).

### Golden images

- **Reference configuration**: Linux with Mesa **lavapipe** (Vulkan, CPU).
  The prebuilt Dawn has no SwiftShader, and lavapipe is installable on CI
  (`mesa-vulkan-drivers`). CTest points the Vulkan loader at the lavapipe ICD
  only (`VK_ICD_FILENAMES`/`VK_DRIVER_FILES`), so a real GPU on the machine
  does not change results. The goldens in `tests/goldens/` were generated with
  Mesa 25.2.8 / LLVM 20.1.2 (Ubuntu 24.04 `noble-updates`,
  `25.2.8-0ubuntu0.24.04.4`), the same package the CI job installed when this
  was last checked; the job prints the version it gets. Golden tests
  run only when the adapter is lavapipe; other
  configurations build and run every non-golden test, and skip goldens with a
  message.
- **Comparison**: a pixel differs when any channel differs by more than 2;
  a test passes with at most 4 differing pixels. On failure, `<name>.actual.png`
  and `<name>.diff.png` are written to `<build>/golden-failures/`, which CI
  uploads as an artifact.
- **Cases**: solid and rounded rects at fractional positions at scales 1.0,
  1.25, 1.5 and 2.0; four different corner radii; borders; nested clips with
  a transform; textured rects (sRGB and linear, sub-rectangle, tint); alpha
  blending over white, black, gray and colored backgrounds; texture switches
  (three batches, also asserted as draw calls); the same scene on two
  surfaces at scales 1.0 and 1.5 through the frame driver.
- **Updating**: goldens change only through `tools/update_goldens.sh [build-dir]`,
  which sets `UI_UPDATE_GOLDENS=1` for one run. Never set it in CI. Run it on
  the reference configuration, inspect the images and their diff, and commit
  them with the change that caused them. If CI's Mesa version moves, update
  the goldens in the same way and record the new version here.

## Benchmarks

`ui_render_benchmark [frames]` builds and submits a 1920×1080 frame of 10,000
instances (rects, bordered rounded rects, textured rects; the texture changes
every 500 instances) and reports CPU time per frame. GPU execution is
asynchronous and not included. CTest runs a short pass that also checks the
steady state makes no engine allocations.

Baseline (Release, GCC 13, Mesa lavapipe; 6-core AMD EPYC VM, 300 frames,
median):

| Metric | Value |
|---|---|
| Build display list | 0.96 ms/frame |
| Submit (flush, upload, encode, submit) | 0.25 ms/frame |
| Draw calls | 20 |
| Bind-group changes | 20 |
| Engine allocations after warm-up | 0 |

## Prior art

Several designs are inspired by **Raven**, an Apache-2.0 C11 GUI library. They
were reimplemented from a description of the ideas; no Raven source was
copied, translated or consulted.

- Arena debug fill patterns (`0xCD` fresh, `0xDD` reset).
- Allocation tracking for every engine allocation, with custom pooling stepping
  aside (arena passthrough) under AddressSanitizer.
- Generational handles with a type tag, retiring slots instead of wrapping
  generations, and errors instead of UB for bad handles.
- A single event path: injected input goes through the same entry points as
  real input.
- `HostShell`: a shipping shell for host-owned windows, also used by tests.
- The instanced display list: one fixed-size instance per primitive, quads
  expanded from the vertex index, SDF fragment shading, per-instance clip rect,
  and batching only on texture changes.
- The leak-checking test harness, with a deliberate-leak self-test.

## Known platform hazards

1. **Windows modal move/resize loop.** While the user drags or resizes a
   window, Windows runs its own modal message loop inside `DefWindowProc`, and
   the application's loop stops. The shell must keep producing frames during
   that loop. The SDL3 shell does it from an event watcher on SDL's
   live-resize expose events. A shell written directly on Win32 would use a
   `SetTimer` between `WM_ENTERSIZEMOVE` and `WM_EXITSIZEMOVE`.
2. **Driver-sent window messages vs. a blocked UI thread.** Graphics drivers can
   *send* (synchronously) window messages to the UI thread while a swapchain is
   created or resized. If the UI thread is blocked waiting on a render thread at
   that moment, and the render thread is inside the driver call waiting for the
   UI thread to process that message, both deadlock. Any such wait on the UI
   thread must keep servicing sent messages, e.g. `MsgWaitForMultipleObjectsEx`
   with `QS_SENDMESSAGE` and a `PeekMessage` pump, never a plain blocking wait.
   The renderer currently configures and presents on the UI thread, so it
   never waits on another thread.
3. **Mobile GPU surface loss.** iOS and Android destroy or invalidate the GPU
   surface when the app is backgrounded (`surfaceDestroyed`, scene
   background). Shells must send `Suspend` and `SurfaceLost`, the renderer
   must release the surface and its swapchain-dependent resources, and on
   return the shell sends `Resize` (new surface) and `Resume` so the renderer
   recreates them. The core already stops requesting frames in between. A
   new native handle is picked up by the renderer, which recreates its
   `wgpu::Surface` when the handle changes.
4. **Web clipboard is asynchronous.** `navigator.clipboard.readText()` returns a
   promise and needs permission, so `Platform::clipboard_get_text` will return
   `Unavailable` on the web. Paste must come from the DOM `paste` event, which
   needs a dedicated event kind when the web shell is written.
5. **Web threads.** Threads need `SharedArrayBuffer`, which requires
   cross-origin isolation (COOP/COEP headers). The core is single-threaded by
   design; any parallel path must remain optional.
6. **Asynchronous WebGPU initialization on the web.** Adapter and device
   requests complete from the browser event loop; blocking (`WaitAny` with a
   timeout) needs ASYNCIFY or JSPI. The renderer compiles for the web but
   returns `Unavailable` from initialization there; the web shell must drive
   the asynchronous flow.
7. **Wayland window sizes.** A Wayland window's size is known only after the
   compositor's first configure event. Shells must treat the initial size as
   provisional and rely on the following Resize.
8. **SDL's dummy video driver polls.** It has no blocking event wait, so SDL
   falls back to polling every millisecond (measured: ~5% of one core while
   idle). That is SDL's fallback, not the shell loop: X11, Wayland, Windows
   and Cocoa have blocking waits. Do not use the dummy driver to measure idle
   CPU.
