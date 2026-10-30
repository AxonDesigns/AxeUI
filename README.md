# AxeUI

A cross-platform, retained-mode UI engine in C++20 with a C11 API for language
bindings. Targets: Windows (MSVC), macOS (Clang), Linux (GCC, Clang) and
WebAssembly (Emscripten); iOS and Android later.

This repository is at phase 1: the foundation library, the platform contract
and the display list data format. There is no rendering, text, layout or
widgets yet. See [docs/architecture.md](docs/architecture.md) for the design.

## Layout

```
include/ui/ui.h        public C API (conventions only, for now)
src/foundation/        geometry, units, color, Result, allocators, handles, logging
src/platform/          platform contract (Shell, Event, Core) + HostShell, NullShell
src/display_list/      GPU instance format, display list, builder (no GPU code)
src/capi/              C API implementation
tests/                 doctest unit tests and the leak-checking harness
third_party/doctest/   doctest 2.4.11 (MIT), vendored
cmake/                 shared compiler settings and helper scripts
```

Library targets: `ui_foundation`, `ui_platform`, `ui_display_list`, `ui_capi`
(all static).

## Requirements

- CMake 3.21+
- A C++20 compiler. Verified locally: GCC 13, Clang 18, Emscripten 6.0.13.
  CI also covers MSVC 2022 (`windows-2022`) and Apple Clang (`macos-14`).
- For WebAssembly: Emscripten (CI pins 6.0.13) and Node.js

## Build and test

Every target is built with exceptions and RTTI disabled. Pass
`-DUI_WARNINGS_AS_ERRORS=ON` to match CI.

### Linux (GCC or Clang)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Pick the compiler with `-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang`
(or `g++` / `gcc`).

With sanitizers (GCC or Clang):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DUI_ENABLE_ASAN=ON -DUI_ENABLE_UBSAN=ON -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

Under AddressSanitizer, arenas switch to passthrough mode, so every arena
allocation is a separate heap block the sanitizer can track.

### macOS (Apple Clang)

Same commands as Linux. Xcode or the Command Line Tools provide the compiler.

### Windows (MSVC)

From a Developer PowerShell / Command Prompt for VS 2022:

```powershell
cmake -S . -B build -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

### WebAssembly (Emscripten)

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Debug -DUI_WARNINGS_AS_ERRORS=ON
cmake --build build-wasm --parallel
ctest --test-dir build-wasm --output-on-failure   # runs the tests under Node
```

## Build options

| Option                   | Default | Effect                                        |
|--------------------------|---------|-----------------------------------------------|
| `UI_BUILD_TESTS`         | ON at top level | Build the unit tests                  |
| `UI_WARNINGS_AS_ERRORS`  | OFF     | `-Werror` / `/WX` (CI turns it on)            |
| `UI_ENABLE_ASAN`         | OFF     | AddressSanitizer (GCC/Clang)                  |
| `UI_ENABLE_UBSAN`        | OFF     | UndefinedBehaviorSanitizer (GCC/Clang)        |

## Tests

| CTest name              | What it covers                                           |
|-------------------------|----------------------------------------------------------|
| `ui_foundation_tests`   | geometry, units, color, f16, Result, arena, tracking, handles, logging, UTF-8, leak-checker self-test |
| `ui_platform_tests`     | Core driven through HostShell with recording callbacks; NullShell; events |
| `ui_display_list_tests` | instance layout, batching, clipping/culling, pixel snapping |
| `ui_capi_tests`         | C API behavior, invalid-handle reporting                  |
| `ui_c11_header_test`    | `include/ui/ui.h` compiled as strict C11 and exercised    |
| `ui_assert_death_test`  | `UI_ASSERT` reports file, line and message, then aborts   |

Engine tests use `UI_TEST_CASE` instead of `TEST_CASE`. It runs the test in a
fixture whose memory resource is tracked. The test fails if any allocation made
through that resource is still live when it ends. Because exceptions are off,
use `UI_REQUIRE` (check, then return) where you would normally use doctest's
`REQUIRE`.

## Code style

`.clang-format` (checked in CI with clang-format 18.1.8) and `.clang-tidy`.
CMake writes `compile_commands.json` into the build directory for clang-tidy
and editors.
