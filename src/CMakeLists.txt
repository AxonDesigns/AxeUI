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
target_link_libraries(ui_capi PUBLIC ui_platform)
ui_configure_target(ui_capi)

# Binding artifacts for the C API.
if(EMSCRIPTEN)
  # ES module for JavaScript/TypeScript: axeui.mjs + axeui.wasm, exporting
  # exactly the functions listed in bindings/wasm/exported_functions.json
  # (generated from api/ui_api.json).
  add_executable(ui_wasm capi/capi_context.cpp capi/ui_capi.cpp)
  target_include_directories(ui_wasm PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(ui_wasm PRIVATE ui_platform)
  ui_configure_target(ui_wasm)
  set_target_properties(ui_wasm PROPERTIES OUTPUT_NAME axeui SUFFIX ".mjs")
  set(ui_exports "${PROJECT_SOURCE_DIR}/bindings/wasm/exported_functions.json")
  target_link_options(ui_wasm PRIVATE
    --no-entry
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createUiModule
    -sENVIRONMENT=web,node
    -sALLOW_TABLE_GROWTH=1
    "-sEXPORTED_FUNCTIONS=@${ui_exports}"
    "-sEXPORTED_RUNTIME_METHODS=addFunction,removeFunction,UTF8ToString,HEAPU8")
  set_property(TARGET ui_wasm APPEND PROPERTY LINK_DEPENDS "${ui_exports}")
else()
  # Shared library for C and FFI bindings (Dart, Python, ...). Everything is
  # built with hidden visibility; only UI_API (ui_*) functions are exported.
  add_library(ui_shared SHARED capi/capi_context.cpp capi/ui_capi.cpp)
  target_include_directories(ui_shared PUBLIC "${PROJECT_SOURCE_DIR}/include")
  target_compile_definitions(ui_shared PRIVATE UI_BUILDING_LIBRARY PUBLIC UI_SHARED)
  target_link_libraries(ui_shared PRIVATE ui_platform)
  set_target_properties(ui_shared PROPERTIES OUTPUT_NAME axeui)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(ui_shared PRIVATE "LINKER:--exclude-libs,ALL")
  endif()
  ui_configure_target(ui_shared)
endif()

if(UI_ENABLE_RENDERER)
  # WGSL sources are embedded into the binary at build time.
  set(ui_shader_cpp "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_wgsl.cpp")
  add_custom_command(
    OUTPUT "${ui_shader_cpp}"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/ui.wgsl"
      "-DOUTPUT=${ui_shader_cpp}"
      -DSYMBOL=kUiWgsl
      -DNAMESPACE=ui::render::shaders
      -P "${PROJECT_SOURCE_DIR}/cmake/ui_embed_file.cmake"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/render/shaders/ui.wgsl"
      "${PROJECT_SOURCE_DIR}/cmake/ui_embed_file.cmake"
    COMMENT "Embedding ui.wgsl"
    VERBATIM)

  add_library(ui_render STATIC
    render/gpu_context.cpp
    render/renderer.cpp
    render/texture_manager.cpp
    "${ui_shader_cpp}")
  target_link_libraries(ui_render PUBLIC ui_display_list ui_platform ui::webgpu)
  ui_configure_target(ui_render)

  # Orchestration between core, application paint and renderer.
  add_library(ui_frame STATIC
    frame/frame_driver.cpp)
  target_link_libraries(ui_frame PUBLIC ui_render)
  ui_configure_target(ui_frame)
endif()
