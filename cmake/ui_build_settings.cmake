# Shared build settings for every AxeUI target.
#
# Every target (library, test, tool) must call ui_configure_target(). It applies
# the language standard, the no-exceptions / no-RTTI flags, warnings and
# sanitizers. ui_verify_targets_configured() runs at the end of the top-level
# CMakeLists.txt and fails the configure step if any target skipped it, so no
# target can silently build with exceptions or RTTI enabled.

include_guard(GLOBAL)

option(UI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(UI_ENABLE_ASAN "Build with AddressSanitizer (GCC/Clang)" OFF)
option(UI_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

if((UI_ENABLE_ASAN OR UI_ENABLE_UBSAN) AND (MSVC OR EMSCRIPTEN))
  message(FATAL_ERROR "UI_ENABLE_ASAN / UI_ENABLE_UBSAN are supported with GCC and Clang only.")
endif()

# CMake seeds MSVC's CMAKE_CXX_FLAGS with /EHsc and /GR. Remove them so the
# per-target /EHs-c- /GR- flags are the only ones on the command line
# (otherwise cl emits D9025 "overriding" warnings).
if(MSVC)
  string(REGEX REPLACE "/EH[a-z]*" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  string(REGEX REPLACE "/GR-?" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

function(ui_configure_target target)
  set_target_properties(${target} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    UI_CONFIGURED TRUE)

  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
      $<$<COMPILE_LANGUAGE:CXX>:/EHs-c- /GR->)
    # Tell the MSVC STL that exceptions are off, so it aborts instead of
    # throwing and does not trigger C4530.
    target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    if(UI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wsign-conversion -Wcast-align -Wimplicit-fallthrough
      $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti
                                -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual>)
    if(UI_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()

    set(sanitizers "")
    if(UI_ENABLE_ASAN)
      list(APPEND sanitizers address)
    endif()
    if(UI_ENABLE_UBSAN)
      list(APPEND sanitizers undefined)
    endif()
    if(sanitizers)
      list(JOIN sanitizers "," sanitizer_list)
      # vptr checking needs RTTI, which is disabled everywhere.
      set(sanitizer_flags -fsanitize=${sanitizer_list} -fno-omit-frame-pointer)
      if(UI_ENABLE_UBSAN)
        list(APPEND sanitizer_flags -fno-sanitize=vptr -fno-sanitize-recover=all)
      endif()
      target_compile_options(${target} PRIVATE ${sanitizer_flags})
      target_link_options(${target} PRIVATE ${sanitizer_flags})
    endif()
  endif()

  if(EMSCRIPTEN)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      # EXIT_RUNTIME makes the process exit code reflect main()'s return
      # value under Node, so CTest sees test failures.
      target_link_options(${target} PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 -sSTACK_SIZE=1MB)
    endif()
  endif()
endfunction()

function(_ui_collect_targets dir out_var)
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  foreach(subdir IN LISTS subdirs)
    _ui_collect_targets("${subdir}" sub_targets)
    list(APPEND targets ${sub_targets})
  endforeach()
  set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

function(ui_verify_targets_configured)
  _ui_collect_targets("${CMAKE_CURRENT_SOURCE_DIR}" targets)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "INTERFACE_LIBRARY" OR type STREQUAL "UTILITY")
      continue()
    endif()
    get_target_property(configured ${target} UI_CONFIGURED)
    if(NOT configured)
      message(FATAL_ERROR
        "Target '${target}' was not passed to ui_configure_target(); "
        "every compiled target must disable exceptions and RTTI.")
    endif()
  endforeach()
endfunction()
