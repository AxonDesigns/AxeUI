cmake_minimum_required(VERSION 3.21)

project(AxeUI
  VERSION 0.1.0
  DESCRIPTION "Cross-platform retained-mode UI engine"
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ui_build_settings)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # For clang-tidy and editors.

option(UI_BUILD_TESTS "Build the AxeUI unit tests" ${PROJECT_IS_TOP_LEVEL})

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(src)

if(UI_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

ui_verify_targets_configured()
