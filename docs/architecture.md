# AxeUI architecture

This document records the design decisions behind the engine. It covers phase 1:
foundation, platform contract, display list format and C API conventions.

## Fixed decisions

These were set at the start of the project. Changing any of them needs an
explicit decision, not a drive-by edit.

- **Language.** C++20 for the core; a thin C11 API (`include/ui/ui.h`) on top
  for language bindings.
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
- **Color.** Linear-space, premultiplied alpha throughout the pipeline.
- **Graphics.** WebGPU via Dawn. Native shells will use SDL3 and the web shell
  Emscripten. None of these are dependencies yet.

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
ui_capi ──────────► ui_foundation ◄──── ui_platform
                          ▲
                          └──────────── ui_display_list
```

| Module | Namespace | Contents |
|---|---|---|
| `src/foundation` | `ui::foundation` | geometry, units, color, f16, `Result`/`Error`, assertions, `MemoryResource`, heap/tracking/arena resources, handles and slot map, logging, UTF-8 validation, flags, timestamps |
| `src/platform` | `ui::platform` | `Shell` contract, `Event`, `Core`, `HostShell`, `NullShell` |
| `src/display_list` | `ui::display_list` | GPU instance format, `DisplayList`, `DisplayListBuilder` |
| `src/capi` | `ui::capi` (+ `ui_*` C symbols) | C API implementation |

There is no global mutable state and no static constructor with side effects.
Every service (memory resource, logger) is an object owned by the engine root
and passed down by reference.

## Frame pipeline

```
 shell                               core
 ─────                               ────
 OS input  ──► Event ──► Core::dispatch(event)
                           │  event dispatch     (route to the node tree)
                           │  state update       (input state, focus, lifecycle)
                           └► request_redraw() ──► Shell::request_frame()
 vsync     ──────────────► Core::frame(timestamp)
                           │  rebuild dirty nodes
                           │  layout
                           │  paint to display list
                           └  render (WebGPU, consumes the display list)
```

Phase 1 implements dispatch, state update and frame scheduling. The later
stages are marked in `Core::render_frame()` and arrive with the tree, layout
and renderer tasks.

Frame scheduling rules (`src/platform/core.cpp`):

- `request_redraw()` marks the UI dirty and calls `Shell::request_frame()` at
  most once until that frame arrives. Requests are coalesced.
- No frames are requested while suspended, while the surface is lost, or while
  the surface has zero size.
- `Suspend` and `SurfaceLost` forget an outstanding request, because a
  backgrounded browser tab may never deliver it. `Resume` and `Resize` request
  again.
- A `frame()` call that the core did not need is a cheap no-op. If something
  requests a redraw during a frame (an animation, say), the next frame is
  requested right after.

## Platform contract

- **Two entry points.** `Core::dispatch(const Event&)` and
  `Core::frame(Timestamp)`. Every shell (native, web, mobile, embedding host,
  test) uses exactly these. There is no test-only way to inject input.
- **`Shell`, core to platform.** Surface info (physical size and scale factor),
  `request_frame()`, `set_cursor()`, `set_ime_cursor_area()`, and clipboard
  get/set. Creating the graphics surface is a documented placeholder in
  `shell.h`; the renderer task wires in WebGPU.
- **`Event`, platform to core.** Already converted to logical pixels. Pointer
  down/up/move/cancel (pointer id, kind mouse/touch/pen, buttons, pressure);
  scroll (pixel vs. line deltas); key down/up (physical key as a USB HID usage
  ID, logical key, modifiers, repeat); text input; IME preedit and commit;
  resize; scale factor change; focus gained/lost; suspend; resume; surface
  lost; close requested. Text is UTF-8 and is valid only during `dispatch()`.
  The core validates it and copies what it keeps.
- **`HostShell`.** A shipping shell for when the application owns the window:
  embedding, the future web and mobile glue, and tests. The app reports the
  surface, pushes events and ticks, and answers the core through callbacks.
  The core attaches itself on construction and detaches on destruction.
- **`NullShell`.** Headless: fixed surface, ignores cursor and IME calls, keeps
  the clipboard in memory, and counts frame requests.

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

64-bit generational handles, `[ type : 8 | generation : 24 | index : 32 ]`.

- The type tag (`HandleType`) lets every lookup reject a handle of the wrong
  kind (`WrongHandleType`), not only stale ones.
- Generation 0 is never issued, so 0 is never a valid handle and serves as
  null.
- Removal bumps the slot's generation. A slot whose generation would pass
  `2^24 - 1` is retired permanently instead of wrapping.
- Freed slots are reused in FIFO order to spread generations across slots.
- Every lookup returns `Result`. Null, never-issued, stale or mistyped handles
  are errors, never UB. In the C API, `ui::capi::resolve()` also reports them
  through the context's log callback.

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

The format the renderer will consume, designed for a WebGPU instanced pipeline.
One primitive is one 96-byte instance. The vertex shader expands it into a quad
from `vertex_index` alone (6 vertices, no vertex or index buffer). The fragment
shader evaluates the shape's signed distance function, discards outside the
clip rect, and outputs premultiplied color.

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

## C API conventions

See the header comment in `include/ui/ui.h`. In short: `ui_`/`UI_` prefixes,
C11 and C++ compatible, `ui_error_code` return values with out-parameters,
`ui_context*` as the opaque root, and `uint64_t ui_handle` for everything
inside it (0 = null). Invalid handles are reported through the log callback
and never cause UB. Strings are UTF-8 as pointer + length. Enums are only ever
appended to.

The root is a pointer, not a handle, because resolving a handle needs a table,
and a process-wide table would be global mutable state.

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
- Platform tests drive `Core` only through `HostShell::push_event`/`tick` with
  recording callbacks: the same path a real embedder uses.

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

For the future shell tasks.

1. **Windows modal move/resize loop.** While the user drags or resizes a
   window, Windows runs its own modal message loop inside `DefWindowProc`, and
   the application's loop stops. The shell must keep producing frames during
   that loop, e.g. with a `SetTimer` started on `WM_ENTERSIZEMOVE` and stopped
   on `WM_EXITSIZEMOVE` that calls `Core::frame()`. Otherwise the window
   freezes or stretches stale content while resizing.
2. **Driver-sent window messages vs. a blocked UI thread.** Graphics drivers can
   *send* (synchronously) window messages to the UI thread while a swapchain is
   created or resized. If the UI thread is blocked waiting on a render thread at
   that moment, and the render thread is inside the driver call waiting for the
   UI thread to process that message, both deadlock. Any such wait on the UI
   thread must keep servicing sent messages, e.g. `MsgWaitForMultipleObjectsEx`
   with `QS_SENDMESSAGE` and a `PeekMessage` pump, never a plain blocking wait.
3. **Mobile GPU surface loss.** iOS and Android destroy or invalidate the GPU
   surface when the app is backgrounded (`surfaceDestroyed`, scene
   background). Shells must send `Suspend` and `SurfaceLost`, the renderer
   must release the surface and its swapchain-dependent resources, and on
   return the shell sends `Resize` (new surface) and `Resume` so the renderer
   recreates them. The core already stops requesting frames in between.
4. **Web clipboard is asynchronous.** `navigator.clipboard.readText()` returns a
   promise and needs permission, so `Shell::clipboard_get_text` will return
   `Unavailable` on the web. Paste must come from the DOM `paste` event, which
   needs a dedicated event kind when the web shell is written.
5. **Web threads.** Threads need `SharedArrayBuffer`, which requires
   cross-origin isolation (COOP/COEP headers). The core is single-threaded by
   design; any parallel path must remain optional.
