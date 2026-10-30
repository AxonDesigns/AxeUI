# Pinned third-party dependencies for the renderer and the desktop shell.
#
# Archives are downloaded once into UI_DEPS_DIR (default: <source>/.deps),
# verified by SHA-256, and extracted next to the download. CI caches that
# directory keyed on this file, so a dependency bump invalidates the cache.
#
# Dawn: official prebuilt release packages from github.com/google/dawn
# (static libwebgpu_dawn + headers + CMake config), and the matching
# emdawnwebgpu package for Emscripten, so native and web compile against the
# same webgpu_cpp.h. See docs/architecture.md, "Renderer > Dawn".
#
# SDL3: source release, built as part of the project (static).

include_guard(GLOBAL)

set(UI_DEPS_DIR "${PROJECT_SOURCE_DIR}/.deps" CACHE PATH "Download/extract directory for pinned dependencies")

set(UI_DAWN_RELEASE "v20261016.190333")
set(UI_DAWN_COMMIT "67d4d6e64b0755d638f76549794692badfdbc8ab")
set(UI_SDL3_VERSION "3.4.18")
set(UI_SDL3_SHA256 "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3")

# ui_fetch_archive(<name> <url> <sha256> <out_var>)
# Downloads and extracts an archive (once); sets <out_var> to the extraction
# directory.
function(ui_fetch_archive name url sha256 out_var)
  set(dir "${UI_DEPS_DIR}/${name}")
  set(stamp "${dir}/.extracted-${sha256}")
  if(NOT EXISTS "${stamp}")
    get_filename_component(file_name "${url}" NAME)
    set(archive "${UI_DEPS_DIR}/downloads/${file_name}")
    if(NOT EXISTS "${archive}")
      message(STATUS "Downloading ${file_name}")
      file(DOWNLOAD "${url}" "${archive}.part"
        EXPECTED_HASH SHA256=${sha256}
        STATUS status
        TLS_VERIFY ON)
      list(GET status 0 code)
      if(NOT code EQUAL 0)
        list(GET status 1 reason)
        file(REMOVE "${archive}.part")
        message(FATAL_ERROR "Download of ${url} failed: ${reason}")
      endif()
      file(RENAME "${archive}.part" "${archive}")
    endif()
    file(SHA256 "${archive}" actual)
    if(NOT actual STREQUAL sha256)
      message(FATAL_ERROR "${archive}: SHA-256 mismatch (expected ${sha256}, got ${actual})")
    endif()
    file(REMOVE_RECURSE "${dir}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${dir}")
    file(WRITE "${stamp}" "${url}\n")
  endif()
  set(${out_var} "${dir}" PARENT_SCOPE)
endfunction()

# Makes the Dawn targets available:
#   native:     dawn::webgpu_dawn (imported, from the prebuilt package)
#   Emscripten: ui_webgpu (interface target adding --use-port=emdawnwebgpu)
# Both are exposed as the single target `ui::webgpu`.
function(ui_provide_dawn)
  if(TARGET ui_webgpu)
    return()
  endif()
  set(base "https://github.com/google/dawn/releases/download/${UI_DAWN_RELEASE}")

  if(EMSCRIPTEN)
    ui_fetch_archive(emdawnwebgpu
      "${base}/emdawnwebgpu_pkg-${UI_DAWN_RELEASE}.zip"
      "ead0f028c7ac4190713fee345c2ab79f4ca042b5d2f08dfd0e4465e915339be7"
      pkg_dir)
    set(port "${pkg_dir}/emdawnwebgpu_pkg/emdawnwebgpu.port.py")
    add_library(ui_webgpu INTERFACE)
    target_compile_options(ui_webgpu INTERFACE "--use-port=${port}")
    target_link_options(ui_webgpu INTERFACE "--use-port=${port}")
    add_library(ui::webgpu ALIAS ui_webgpu)
    return()
  endif()

  set(prefix "Dawn-${UI_DAWN_COMMIT}")
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(flavor "ubuntu-latest-Release")
    set(sha "20ff88448def9645ceeff30911e07295aeb433be23e26180c96019b4300bff5a")
  elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(flavor "macos-latest-Release")
    set(sha "736fccba78b8a2fde8bfd55d045e890933b0a01776aed5a29b9f024735404193")
  elseif(APPLE)
    set(flavor "macos-15-intel-Release")
    set(sha "277dafe9b6d8c3374f10963d54cec5548c05ab478519f85c9cb335dfbd31dde1")
  elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(flavor "windows-latest-Release")
    set(sha "57402c6aa887e30d3e5710707d6b0677d8292538ab941628c624635022bfa49f")
  else()
    message(FATAL_ERROR "No prebuilt Dawn package for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; "
                        "configure with -DUI_ENABLE_RENDERER=OFF or set UI_DAWN_DIR.")
  endif()

  set(UI_DAWN_DIR "" CACHE PATH "Use an already-extracted Dawn package instead of downloading one")
  if(UI_DAWN_DIR)
    set(root "${UI_DAWN_DIR}")
  else()
    ui_fetch_archive(dawn-${flavor} "${base}/${prefix}-${flavor}.tar.gz" "${sha}" extracted)
    set(root "${extracted}/${prefix}-${flavor}")
  endif()

  # The package keeps its config in lib/ or lib64/ depending on the platform;
  # find_package does not search lib64 on every distribution, so locate it.
  file(GLOB_RECURSE dawn_config "${root}/*/DawnConfig.cmake")
  if(NOT dawn_config)
    message(FATAL_ERROR "DawnConfig.cmake not found under ${root}")
  endif()
  list(GET dawn_config 0 dawn_config)
  get_filename_component(dawn_config_dir "${dawn_config}" DIRECTORY)
  find_package(Threads REQUIRED)
  find_package(Dawn CONFIG REQUIRED PATHS "${dawn_config_dir}" NO_DEFAULT_PATH)
  add_library(ui_webgpu INTERFACE)
  target_link_libraries(ui_webgpu INTERFACE dawn::webgpu_dawn)
  add_library(ui::webgpu ALIAS ui_webgpu)
endfunction()

# Makes SDL3::SDL3-static available, built from the pinned source release.
function(ui_provide_sdl3)
  if(TARGET SDL3::SDL3-static)
    return()
  endif()
  ui_fetch_archive(sdl3-${UI_SDL3_VERSION}
    "https://github.com/libsdl-org/SDL/releases/download/release-${UI_SDL3_VERSION}/SDL3-${UI_SDL3_VERSION}.tar.gz"
    "${UI_SDL3_SHA256}"
    sdl_dir)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  # Only video and events are used; skipping the rest keeps the build small
  # and avoids optional system dependencies.
  foreach(subsystem AUDIO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI POWER SENSOR DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
  add_subdirectory("${sdl_dir}/SDL3-${UI_SDL3_VERSION}" "${CMAKE_BINARY_DIR}/_deps/sdl3" EXCLUDE_FROM_ALL SYSTEM)
endfunction()
