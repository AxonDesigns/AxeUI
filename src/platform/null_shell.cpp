#include "platform/null_shell.h"

namespace ui::platform {

NullShell::NullShell(foundation::MemoryResource& memory) noexcept
    : clipboard_(foundation::Allocator<char>(memory)) {}

foundation::Result<void> NullShell::clipboard_get_text(foundation::String& out) {
  out.assign(clipboard_);
  return {};
}

foundation::Result<void> NullShell::clipboard_set_text(std::string_view text) {
  clipboard_.assign(text);
  return {};
}

}  // namespace ui::platform
