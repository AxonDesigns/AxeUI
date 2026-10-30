#include "capi/capi_context.h"

namespace {

// Bridges the engine Logger to the application's C callback.
void forward_log(void* user_data, const ui::foundation::LogRecord& record) {
  const auto* context = static_cast<const ui_context*>(user_data);
  if (context->log_callback != nullptr) {
    context->log_callback(context->log_user_data, ui::capi::to_c(record.level),
                          ui::capi::to_c(record.code), record.message.data(),
                          record.message.size());
  }
}

}  // namespace

ui_context::ui_context() noexcept { logger.set_callback(&forward_log, this); }

ui_context::~ui_context() {
  const ui::foundation::AllocationStats& stats = memory.stats();
  if (stats.live_allocations != 0) {
    UI_LOGF(logger, ui::foundation::LogLevel::Error, ui::foundation::ErrorCode::Internal,
            "ui_context_destroy: %zu allocation(s) (%zu bytes) still live", stats.live_allocations,
            stats.live_bytes);
  }
}

namespace ui::capi {

void report_bad_handle(const ui_context& context, const foundation::Error& error, ui_handle handle,
                       const char* function) {
  UI_LOGF(context.logger, foundation::LogLevel::Error, error.code,
          "%s: %s (handle 0x%016" PRIx64 ")", function, error.message, handle);
}

}  // namespace ui::capi
