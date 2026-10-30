# AxeUI

A cross-platform, retained-mode UI engine in C++20 with a C11 API for language
bindings (TypeScript, Dart, C, C++). Targets: Windows (MSVC), macOS (Clang),
Linux (GCC, Clang; X11 and Wayland) and WebAssembly (Emscripten); iOS and
Android later.

Phase 1 is in place: foundation library, platform contract, display list, a
WebGPU renderer (Dawn), a frame driver, an SDL3 desktop shell with a
two-window example, golden-image tests, and a binding-ready C API with
generated TypeScript and Dart bindings. Text, layout, the element tree and
widgets come later. See [docs/architecture.md](docs/architecture.md) for the
design.

## Layout

```
api/ui_api.json        C API description (source of truth)
include/ui/ui.h        public C API (generated from api/ui_api.json)
src/foundation/        geometry, units, color, Result, allocators, handles, logging
src/platform/          platform contract (Platform, Surface, Event, Core) + host/null shells
src/display_list/      GPU instance format, display list, builder
src/render/            WebGPU renderer (Dawn) and WGSL shaders
src/frame/             frame driver (core -> paint -> render)
src/capi/              C API implementation
shells/sdl3/           SDL3 desktop shell (Windows, macOS, X11, Wayland)
examples/              hello_surfaces: two windows
bindings/              generated TypeScript and Dart bindings + smoke tests
tools/                 gen_api.py (bindings generator), update_goldens.sh
tests/                 doctest unit tests, renderer and golden-image tests
tests/goldens/         reference images (Linux, Mesa lavapipe)
third_party/           doctest 2.4.11 (MIT), stb_image/stb_image_write (test only)
cmake/                 build settings, pinned dependencies, helper scripts
```

Libraries (static unless noted): `ui_foundation`, `ui_platform`,
`ui_display_list`, `ui_render`, `ui_frame`, `ui_capi`, `ui_shell_sdl3`,
`ui_shared` (shared: `libaxeui`, exports only `ui_*`). Under Emscripten,
`ui_wasm` builds the ES module `axeui.mjs`.

## Requirements

- CMake 3.21+, Python 3 (for the API generator check)
- A C++20 compiler. Verified locally: GCC 13, Clang 18, Emscripten 6.0.13
  (all on Linux). CI also builds with MSVC and Apple Clang (`macos-15`).
- Windows: Visual Studio 2026 (MSVC 19.51 or newer). The pinned prebuilt
  Dawn is built with it, and older toolsets cannot link it.
- Network access on the first configure: pinned, checksummed dependencies
  (prebuilt Dawn, SDL3 source, emdawnwebgpu) are downloaded into `.deps/`.
  Use `-DUI_ENABLE_RENDERER=OFF` for a build without them.
- Linux: X11 and Wayland development headers for SDL3, and Mesa's Vulkan
  drivers for headless rendering and golden tests:

  ```sh
  sudo apt install mesa-vulkan-drivers libvulkan1 libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
    libegl1-mesa-dev
  ```
- Optional: Node.js 22.18+ (TypeScript smoke test), Dart 3.5+ (Dart smoke test).

## Build, test and run

Every target is built with exceptions and RTTI disabled. Pass
`-DUI_WARNINGS_AS_ERRORS=ON` to match CI.

### Linux (GCC or Clang; X11 or Wayland)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/examples/hello_surfaces
```

SDL picks Wayland or X11 automatically; force one with
`SDL_VIDEO_DRIVER=wayland` or `SDL_VIDEO_DRIVER=x11`. Pick the compiler with
`-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang`.

With sanitizers (GCC or Clang):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DUI_ENABLE_ASAN=ON -DUI_ENABLE_UBSAN=ON -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

### macOS (Apple Clang)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/examples/hello_surfaces
```

The renderer uses Metal through Dawn; the shell creates a `CAMetalLayer`-backed
view for each window.

### Windows (MSVC)

From a Developer PowerShell for VS 2026:

```powershell
cmake -S . -B build -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
.\build\examples\Debug\hello_surfaces.exe
```

The renderer uses D3D12 through Dawn. Because the prebuilt Dawn uses the
release C runtime, all configurations build with `/MD`.

### WebAssembly (Emscripten)

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Debug -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build-wasm --parallel
ctest --test-dir build-wasm --output-on-failure   # runs under Node, incl. the TypeScript smoke test
```

The renderer compiles against emdawnwebgpu; the web shell (part 3) will run it
in a browser. GPU tests are skipped under Node.

### The example

`hello_surfaces` opens two windows. The main window has bordered rounded
rects, nested clips, a textured rect, and a button that reacts to hover and
click. Close the second window independently; close the main window to quit.
Resize the windows or drag them to a monitor with a different scale factor:
they stay sharp and correctly sized.

## Build options

| Option                   | Default | Effect                                        |
|--------------------------|---------|-----------------------------------------------|
| `UI_BUILD_TESTS`         | ON at top level | Build the tests                       |
| `UI_ENABLE_RENDERER`     | ON      | WebGPU renderer and frame driver (downloads pinned Dawn) |
| `UI_ENABLE_SDL3_SHELL`   | ON on desktop | SDL3 shell and examples (downloads pinned SDL3) |
| `UI_WARNINGS_AS_ERRORS`  | OFF     | `-Werror` / `/WX` (CI turns it on)            |
| `UI_ENABLE_ASAN`         | OFF     | AddressSanitizer (GCC/Clang)                  |
| `UI_ENABLE_UBSAN`        | OFF     | UndefinedBehaviorSanitizer (GCC/Clang)        |
| `UI_DEPS_DIR`            | `.deps` | Where pinned dependencies are downloaded      |
| `UI_DAWN_DIR`            | (empty) | Use an already-extracted Dawn package         |

## Tests

| CTest name                          | What it covers |
|-------------------------------------|----------------|
| `ui_foundation_tests`               | geometry, units, color, f16, Result, arena, tracking, handles, logging, UTF-8/UTF-16, leak-checker self-test |
| `ui_platform_tests`                 | multi-surface Core driven through HostShell/HostSurface; NullShell; events |
| `ui_display_list_tests`             | instance layout, batching, clipping/culling, pixel snapping |
| `ui_render_tests`                   | renderer (lavapipe on Linux), WGSL/C++ layout parity, device loss, zero steady-state allocations, golden images |
| `ui_render_benchmark_smoke`         | short benchmark pass |
| `ui_shell_sdl3_tests`               | SDL3 shell on SDL's dummy video driver |
| `ui_capi_tests`, `ui_c11_header_test` | C API behavior; header compiled as strict C11 |
| `ui_shared_exports`                 | `libaxeui` exports only `ui_*` |
| `ui_dart_smoke`, `ui_typescript_smoke` | binding smoke tests (when Dart / the wasm build are available) |
| `ui_api_generated_files_up_to_date` | `ui.h` and bindings match `api/ui_api.json` |
| `ui_assert_death_test`              | `UI_ASSERT` reports file, line and message, then aborts |

Engine tests use `UI_TEST_CASE` instead of `TEST_CASE`: the test fails if
any allocation made through the fixture's memory resource is still live when
it ends. Because exceptions are off, use `UI_REQUIRE` (check, then return)
instead of doctest's `REQUIRE`.

**Golden images** are compared only on the reference configuration (Linux,
Mesa lavapipe); elsewhere they are skipped. After an intended rendering
change, regenerate them on Linux with `tools/update_goldens.sh build`, review
the images, and commit them. CI never updates goldens.

**Benchmark**: `./build/tests/ui_render_benchmark 300` (Release build) prints
build and submit time for a 10,000-instance frame.

## C API and bindings

Edit `api/ui_api.json`, then run `python3 tools/gen_api.py` to regenerate
`include/ui/ui.h`, `bindings/typescript/*`, `bindings/dart/lib/src/*` and
`bindings/wasm/exported_functions.json`. CI fails if they are stale. The
conventions and ABI rules are documented at the top of `ui.h` and in
docs/architecture.md, "Language bindings".

## Code style

`.clang-format` (checked in CI with clang-format 18.1.8; the generated `ui.h`
is excluded) and `.clang-tidy`. CMake writes `compile_commands.json` into the
build directory for clang-tidy and editors.
