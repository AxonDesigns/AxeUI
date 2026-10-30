# Desktop shell (SDL3). SDL types stay inside this directory.
ui_provide_sdl3()

add_library(ui_shell_sdl3 STATIC
  sdl3/sdl_shell.cpp)
target_include_directories(ui_shell_sdl3 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ui_shell_sdl3 PUBLIC ui_platform PRIVATE SDL3::SDL3-static)
ui_configure_target(ui_shell_sdl3)

if(UI_BUILD_TESTS)
  # Runs the shell on SDL's "dummy" video driver (no display needed).
  find_package(Threads REQUIRED)
  add_executable(ui_shell_sdl3_tests sdl3/sdl_shell_test.cpp)
  target_link_libraries(ui_shell_sdl3_tests PRIVATE
    ui_test_support ui_shell_sdl3 ui_frame SDL3::SDL3-static Threads::Threads)
  target_include_directories(ui_shell_sdl3_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
  ui_configure_target(ui_shell_sdl3_tests)
  add_test(NAME ui_shell_sdl3_tests COMMAND ui_shell_sdl3_tests)
  set_tests_properties(ui_shell_sdl3_tests PROPERTIES ENVIRONMENT "${UI_RENDER_TEST_ENV}")
endif()
