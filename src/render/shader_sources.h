#pragma once

// WGSL sources embedded at build time from src/render/shaders/*.wgsl
// (cmake/ui_embed_file.cmake). NUL-terminated; _size excludes the NUL.

namespace ui::render::shaders {

extern const char kUiWgsl[];
extern const unsigned kUiWgsl_size;

}  // namespace ui::render::shaders
