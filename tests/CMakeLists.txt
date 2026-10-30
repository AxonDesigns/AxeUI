# doctest, vendored from https://github.com/doctest/doctest (v2.4.11, MIT).
# Included as a SYSTEM directory so our warning flags do not apply to it.
add_library(ui_doctest INTERFACE)
target_include_directories(ui_doctest SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party/doctest")
target_compile_definitions(ui_doctest INTERFACE DOCTEST_CONFIG_NO_EXCEPTIONS)

# Harness shared by all test executables (doctest main, leak checker).
add_library(ui_test_support OBJECT
  support/test_main.cpp
  support/test_support.cpp)
target_include_directories(ui_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ui_test_support PUBLIC ui_doctest ui_foundation)
ui_configure_target(ui_test_support)

# ui_add_test(<name> SOURCES <files...> LIBRARIES <targets...>)
function(ui_add_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES")
  add_executable(${name} ${arg_SOURCES})
  target_link_libraries(${name} PRIVATE ui_test_support ${arg_LIBRARIES})
  ui_configure_target(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

ui_add_test(ui_foundation_tests
  SOURCES
    foundation/arena_test.cpp
    foundation/assertions_test.cpp
    foundation/build_flags_test.cpp
    foundation/color_test.cpp
    foundation/float16_test.cpp
    foundation/geometry_test.cpp
    foundation/handle_test.cpp
    foundation/leak_check_test.cpp
    foundation/logging_test.cpp
    foundation/result_test.cpp
    foundation/tracking_resource_test.cpp
    foundation/utf8_test.cpp
  LIBRARIES ui_foundation)

ui_add_test(ui_platform_tests
  SOURCES
    platform/core_test.cpp
    platform/event_test.cpp
    platform/null_shell_test.cpp
  LIBRARIES ui_platform)

ui_add_test(ui_display_list_tests
  SOURCES
    display_list/display_list_test.cpp
  LIBRARIES ui_display_list)

ui_add_test(ui_capi_tests
  SOURCES
    capi/capi_test.cpp
  LIBRARIES ui_capi)

# include/ui/ui.h must be valid C11: compile a C translation unit against it
# (strict C11, no extensions, same warning flags) and run it.
add_executable(ui_c11_header_test capi/c11_header_test.c)
target_link_libraries(ui_c11_header_test PRIVATE ui_capi)
# The library is C++, so link with the C++ driver to pull in its runtime.
set_target_properties(ui_c11_header_test PROPERTIES LINKER_LANGUAGE CXX)
ui_configure_target(ui_c11_header_test)
add_test(NAME ui_c11_header_test COMMAND ui_c11_header_test)

# UI_ASSERT must report file, line and message, then abort.
add_executable(ui_assert_death_test support/assert_death_main.cpp)
target_include_directories(ui_assert_death_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ui_assert_death_test PRIVATE ui_foundation)
ui_configure_target(ui_assert_death_test)
add_test(NAME ui_assert_death_test
  COMMAND "${CMAKE_COMMAND}"
    "-DPROGRAM=$<TARGET_FILE:ui_assert_death_test>"
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    "-DEXPECTED_REGEX=assert_death_main\\.cpp:[0-9]+: assertion failed: deliberate assertion failure"
    -P "${PROJECT_SOURCE_DIR}/cmake/ui_expect_abort.cmake")
