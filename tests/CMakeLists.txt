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

if(UI_ENABLE_RENDERER)
  # Test-only PNG I/O (stb, vendored); never linked into engine libraries.
  add_library(ui_golden STATIC render/golden.cpp)
  target_include_directories(ui_golden PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_include_directories(ui_golden SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")
  target_compile_definitions(ui_golden PRIVATE
    "UI_GOLDEN_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/goldens\""
    "UI_GOLDEN_OUTPUT_DIR=\"${CMAKE_BINARY_DIR}/golden-failures\"")
  ui_configure_target(ui_golden)

  ui_add_test(ui_render_tests
    SOURCES
      render/golden_test.cpp
      render/renderer_test.cpp
    LIBRARIES ui_frame ui_golden)

  # Golden reference configuration: on Linux, point the Vulkan loader at Mesa
  # lavapipe only, so render tests never depend on the machine's GPU.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_file(UI_LAVAPIPE_ICD lvp_icd.json
      PATHS /usr/share/vulkan/icd.d /etc/vulkan/icd.d /usr/local/share/vulkan/icd.d
      NO_DEFAULT_PATH)
    if(UI_LAVAPIPE_ICD)
      set(UI_RENDER_TEST_ENV "VK_ICD_FILENAMES=${UI_LAVAPIPE_ICD};VK_DRIVER_FILES=${UI_LAVAPIPE_ICD}"
          PARENT_SCOPE)
      set(UI_RENDER_TEST_ENV "VK_ICD_FILENAMES=${UI_LAVAPIPE_ICD};VK_DRIVER_FILES=${UI_LAVAPIPE_ICD}")
    else()
      message(WARNING "Mesa lavapipe not found (apt install mesa-vulkan-drivers); "
                      "render tests use whatever Vulkan driver exists and goldens are skipped")
    endif()
  endif()
  set_tests_properties(ui_render_tests PROPERTIES ENVIRONMENT "${UI_RENDER_TEST_ENV}")

  # CPU cost of a 10,000-instance frame. Run by hand for numbers; CTest runs
  # a short pass to keep it working (and to check zero steady-state allocations).
  add_executable(ui_render_benchmark render/render_benchmark.cpp)
  target_include_directories(ui_render_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(ui_render_benchmark PRIVATE ui_render)
  ui_configure_target(ui_render_benchmark)
  add_test(NAME ui_render_benchmark_smoke COMMAND ui_render_benchmark 5)
  set_tests_properties(ui_render_benchmark_smoke PROPERTIES ENVIRONMENT "${UI_RENDER_TEST_ENV}")
endif()

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

# The shared library exports only the C API.
# Sanitizer builds instrument the library (extra exported symbols, and it
# cannot be loaded into a non-instrumented process such as the Dart VM), so
# the artifact checks run in regular builds only.
set(ui_check_artifacts TRUE)
if(UI_ENABLE_ASAN OR UI_ENABLE_UBSAN)
  set(ui_check_artifacts FALSE)
endif()

if(TARGET ui_shared AND NOT WIN32 AND ui_check_artifacts)
  find_program(UI_NM NAMES nm llvm-nm)
  if(UI_NM)
    add_test(NAME ui_shared_exports
      COMMAND "${CMAKE_COMMAND}" "-DLIBRARY=$<TARGET_FILE:ui_shared>" "-DNM=${UI_NM}"
              "-DAPPLE=${APPLE}" -P "${PROJECT_SOURCE_DIR}/cmake/ui_check_exports.cmake")
  endif()
endif()

# TypeScript binding smoke test: runs the generated wrapper against the wasm
# ES module under Node (which strips TypeScript types natively).
if(TARGET ui_wasm)
  find_program(UI_NODE node)
  if(UI_NODE)
    add_test(NAME ui_typescript_smoke
      COMMAND "${UI_NODE}" test/smoke.ts "$<TARGET_FILE:ui_wasm>"
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bindings/typescript")
  endif()
endif()

# Dart binding smoke test against the shared library (when a Dart SDK is
# installed). `dart pub get` runs first as a fixture.
if(TARGET ui_shared AND ui_check_artifacts)
  find_program(UI_DART dart)
  if(UI_DART)
    add_test(NAME ui_dart_pub_get COMMAND "${UI_DART}" pub get
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bindings/dart")
    set_tests_properties(ui_dart_pub_get PROPERTIES FIXTURES_SETUP ui_dart)
    add_test(NAME ui_dart_smoke
      COMMAND "${UI_DART}" run bin/smoke.dart "$<TARGET_FILE:ui_shared>"
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bindings/dart")
    set_tests_properties(ui_dart_smoke PROPERTIES FIXTURES_REQUIRED ui_dart)
  endif()
endif()

# include/ui/ui.h and the bindings must match api/ui_api.json.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME ui_api_generated_files_up_to_date
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/gen_api.py" --check)
endif()
