cmake_minimum_required(VERSION 3.21)

project(AxeUI
  VERSION 0.1.0
  DESCRIPTION "Cross-platform retained-mode UI engine"
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ui_build_settings)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For clang-tidy and editors.
# Static libraries are also linked into the shared C API library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(UI_BUILD_TESTS "Build the AxeUI unit tests" ${PROJECT_IS_TOP_LEVEL})
option(UI_ENABLE_RENDERER "Build the WebGPU renderer (downloads pinned Dawn)" ON)
if(EMSCRIPTEN)
  set(ui_desktop_default OFF)
else()
  set(ui_desktop_default ${UI_ENABLE_RENDERER})
endif()
option(UI_ENABLE_SDL3_SHELL "Build the SDL3 desktop shell and examples (downloads pinned SDL3)"
       ${ui_desktop_default})

include(ui_dependencies)

if(UI_ENABLE_RENDERER AND MSVC)
  # The prebuilt Dawn package is built against the release CRT. Every target
  # in the process must match it, so all configurations use /MD and the
  # release iterator debug level (docs/architecture.md, "Renderer > Dawn").
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
  add_compile_definitions(_ITERATOR_DEBUG_LEVEL=0)
endif()

if(UI_ENABLE_RENDERER)
  ui_provide_dawn()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(src)

if(UI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(UI_ENABLE_SDL3_SHELL)
  if(NOT UI_ENABLE_RENDERER)
    message(FATAL_ERROR "UI_ENABLE_SDL3_SHELL requires UI_ENABLE_RENDERER")
  endif()
  add_subdirectory(shells)
  add_subdirectory(examples)
endif()

ui_verify_targets_configured()
