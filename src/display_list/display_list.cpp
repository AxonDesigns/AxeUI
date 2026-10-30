#include "display_list/display_list.h"

#include <limits>

#include "foundation/assertions.h"

namespace ui::display_list {

DisplayList::DisplayList(foundation::MemoryResource& memory)
    : instances_(foundation::Allocator<Instance>(memory)),
      commands_(foundation::Allocator<DrawCommand>(memory)) {}

void DisplayList::append(const Instance& instance, TextureId texture) {
  const bool textured = uses_texture(static_cast<PrimitiveKind>(instance.kind));
  UI_ASSERT(textured != texture.is_none(),
            "textured primitives need a texture; untextured ones must not have one");
  UI_ASSERT(instances_.size() < std::numeric_limits<std::uint32_t>::max(),
            "display list instance count exceeds 32 bits");

  const auto index = static_cast<std::uint32_t>(instances_.size());
  instances_.push_back(instance);

  DrawCommand* current = commands_.empty() ? nullptr : &commands_.back();
  const bool can_join = current != nullptr && current->kind == CommandKind::DrawInstances &&
                        (!textured || current->texture.is_none() || current->texture == texture);
  if (can_join) {
    if (textured) {
      current->texture = texture;  // First textured instance decides the binding.
    }
    ++current->instance_count;
    return;
  }
  commands_.push_back(DrawCommand{CommandKind::DrawInstances, index, 1, texture});
}

void DisplayList::clear() noexcept {
  instances_.clear();
  commands_.clear();
}

}  // namespace ui::display_list
