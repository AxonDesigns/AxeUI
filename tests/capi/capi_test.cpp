#include <cstring>
#include <string>
#include <vector>

#include "capi/capi_context.h"
#include "support/test_support.h"
#include "ui/ui.h"

using namespace ui::foundation;

namespace {

struct CLogRecord {
  ui_log_level level;
  ui_error_code code;
  std::string message;
};

void record_log(void* user_data, ui_log_level level, ui_error_code code, const char* message,
                size_t length) {
  static_cast<std::vector<CLogRecord>*>(user_data)->push_back(
      {level, code, std::string(message, length)});
}

}  // namespace

TEST_CASE("C API version and error names") {
  CHECK(ui_get_version() == UI_VERSION);
  CHECK(UI_VERSION == UI_MAKE_VERSION(UI_VERSION_MAJOR, UI_VERSION_MINOR, UI_VERSION_PATCH));
  for (int code = 0; code < static_cast<int>(kErrorCodeCount); ++code) {
    CHECK(std::string(ui_error_code_name(static_cast<ui_error_code>(code))) != "UI_ERROR_UNKNOWN");
  }
}

TEST_CASE("C API context lifecycle and logging") {
  ui_context* context = nullptr;
  UI_REQUIRE(ui_context_create(&context) == UI_OK);

  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(context, record_log, &logs, UI_LOG_WARNING) == UI_OK);
  context->logger.log(LogLevel::Info, ErrorCode::Ok, "filtered");
  context->logger.log(LogLevel::Error, ErrorCode::Internal, "kept");
  UI_REQUIRE(logs.size() == 1);
  CHECK(logs[0].level == UI_LOG_ERROR);
  CHECK(logs[0].code == UI_ERROR_INTERNAL);
  CHECK(logs[0].message == "kept");

  CHECK(ui_context_set_log_callback(nullptr, record_log, &logs, UI_LOG_INFO) ==
        UI_ERROR_INVALID_ARGUMENT);
  ui_context_destroy(context);
}

TEST_CASE("C API reports engine allocations made through the context") {
  ui_context* context = nullptr;
  UI_REQUIRE(ui_context_create(&context) == UI_OK);
  void* block = context->memory.allocate(128, 16);

  ui_allocation_stats stats{};
  CHECK(ui_context_get_allocation_stats(context, &stats) == UI_OK);
  CHECK(stats.live_allocations == 1);
  CHECK(stats.live_bytes == 128);
  CHECK(stats.peak_bytes == 128);

  context->memory.deallocate(block, 128, 16);
  CHECK(ui_context_get_allocation_stats(context, &stats) == UI_OK);
  CHECK(stats.live_allocations == 0);
  CHECK(stats.total_allocations == 1);
  ui_context_destroy(context);
}

TEST_CASE("invalid handles crossing the C API are reported through the log callback") {
  ui_context* context = nullptr;
  UI_REQUIRE(ui_context_create(&context) == UI_OK);
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(context, record_log, &logs, UI_LOG_TRACE) == UI_OK);
  {
    SlotMap<int> nodes(context->memory, HandleType::Node);
    const Handle live = *nodes.emplace(5);
    const Handle stale = *nodes.emplace(6);
    CHECK(nodes.remove(stale).has_value());

    auto ok = ui::capi::resolve(*context, nodes, live.bits(), "ui_node_test");
    UI_REQUIRE(ok.has_value());
    CHECK(**ok == 5);
    CHECK(logs.empty());

    CHECK(ui::capi::resolve(*context, nodes, UI_NULL_HANDLE, "ui_node_test").error().code ==
          ErrorCode::InvalidHandle);
    CHECK(ui::capi::resolve(*context, nodes, stale.bits(), "ui_node_test").error().code ==
          ErrorCode::StaleHandle);
    const ui_handle texture = Handle::make(HandleType::Texture, 1, 0).bits();
    CHECK(ui::capi::resolve(*context, nodes, texture, "ui_node_test").error().code ==
          ErrorCode::WrongHandleType);
  }
  UI_REQUIRE(logs.size() == 3);
  CHECK(logs[0].code == UI_ERROR_INVALID_HANDLE);
  CHECK(logs[1].code == UI_ERROR_STALE_HANDLE);
  CHECK(logs[2].code == UI_ERROR_WRONG_HANDLE_TYPE);
  for (const CLogRecord& log : logs) {
    CHECK(log.level == UI_LOG_ERROR);
    CHECK(log.message.find("ui_node_test: ") == 0);
    CHECK(log.message.find("(handle 0x") != std::string::npos);
  }
  CHECK(logs[1].message.find("destroyed") != std::string::npos);
  ui_context_destroy(context);
}

TEST_CASE("destroying a context with live allocations reports a leak") {
  ui_context* context = nullptr;
  UI_REQUIRE(ui_context_create(&context) == UI_OK);
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(context, record_log, &logs, UI_LOG_TRACE) == UI_OK);
  HeapResource heap;
  // Leak through the context's tracker, then release the block directly to
  // the heap after destruction so the test itself does not leak.
  void* block = context->memory.allocate(32, 8);
  ui_context_destroy(context);
  heap.deallocate(block, 32, 8);
  UI_REQUIRE(logs.size() == 1);
  CHECK(logs[0].code == UI_ERROR_INTERNAL);
  CHECK(logs[0].message.find("1 allocation(s)") != std::string::npos);
}
