add_library(ui_foundation STATIC
  foundation/allocator.cpp
  foundation/arena.cpp
  foundation/assertions.cpp
  foundation/color.cpp
  foundation/error_code.cpp
  foundation/float16.cpp
  foundation/geometry.cpp
  foundation/logging.cpp
  foundation/tracking_resource.cpp
  foundation/utf8.cpp)
target_include_directories(ui_foundation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
ui_configure_target(ui_foundation)


add_library(ui_platform STATIC
  platform/core.cpp
  platform/event.cpp
  platform/host_shell.cpp
  platform/null_shell.cpp)
target_link_libraries(ui_platform PUBLIC ui_foundation)
ui_configure_target(ui_platform)

add_library(ui_display_list STATIC
  display_list/builder.cpp
  display_list/display_list.cpp)
target_link_libraries(ui_display_list PUBLIC ui_foundation)
ui_configure_target(ui_display_list)

# The C API facade over the engine (public header: include/ui/ui.h).
add_library(ui_capi STATIC
  capi/capi_context.cpp
  capi/ui_capi.cpp)
target_include_directories(ui_capi PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(ui_capi PUBLIC ui_foundation)
ui_configure_target(ui_capi)
