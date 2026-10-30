#pragma once

#include <cstdint>
#include <span>

#include "display_list/instance.h"
#include "foundation/allocator.h"

namespace ui::display_list {

// Renderer-defined texture identifier. Zero means "no texture".
struct TextureId {
  std::uint64_t value = 0;

  [[nodiscard]] constexpr bool is_none() const noexcept { return value == 0; }
  friend constexpr bool operator==(TextureId, TextureId) = default;
};

enum class CommandKind : std::uint32_t {
  DrawInstances = 0,
  // Reserved: render a mask for clips that are not axis-aligned rects
  // (rotated or rounded clips, paths). Not emitted yet; see
  // docs/architecture.md, "Display list".
  ClipMask = 1,
};

// "Draw instances [first_instance, first_instance + instance_count) with
// `texture` bound." A texture of none means no instance in the range samples
// one; the renderer may bind any texture (e.g. a 1x1 dummy).
struct DrawCommand {
  CommandKind kind = CommandKind::DrawInstances;
  std::uint32_t first_instance = 0;
  std::uint32_t instance_count = 0;
  TextureId texture;
};

// A frame's draw data: a flat instance array plus the commands that batch it.
//
// Batching rule: untextured instances join the current command regardless of
// its texture, since they do not sample it. A new command starts only when a
// textured instance needs a texture other than the one the current command
// already binds. Clip rects live in each instance, so clipping never splits a
// batch.
class DisplayList {
 public:
  explicit DisplayList(foundation::MemoryResource& memory);

  // Appends one instance. Textured kinds require a texture; untextured kinds
  // must pass TextureId{} (none).
  void append(const Instance& instance, TextureId texture);

  // Removes everything, keeping capacity for the next frame.
  void clear() noexcept;

  [[nodiscard]] std::span<const Instance> instances() const noexcept { return instances_; }
  [[nodiscard]] std::span<const DrawCommand> commands() const noexcept { return commands_; }
  [[nodiscard]] bool empty() const noexcept { return instances_.empty(); }

 private:
  foundation::Vector<Instance> instances_;
  foundation::Vector<DrawCommand> commands_;
};

}  // namespace ui::display_list
