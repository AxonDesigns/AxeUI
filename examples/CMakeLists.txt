add_executable(hello_surfaces hello_surfaces/main.cpp)
find_package(Threads REQUIRED)
target_link_libraries(hello_surfaces PRIVATE ui_frame ui_shell_sdl3 Threads::Threads)
ui_configure_target(hello_surfaces)
