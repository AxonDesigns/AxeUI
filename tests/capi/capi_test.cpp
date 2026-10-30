#include <atomic>
#include <cstring>
#include <string>
#include <thread>
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
                uint32_t length) {
  static_cast<std::vector<CLogRecord>*>(user_data)->push_back(
      {level, code, std::string(message, length)});
}

ui_event poll(ui_context* context) {
  ui_event event{};
  event.size = sizeof(event);
  CHECK(ui_context_poll_event(context, &event) == UI_OK);
  return event;
}

ui_surface_desc surface_desc(uint32_t width = 800, uint32_t height = 600, float scale = 1.0f) {
  ui_surface_desc desc{};
  desc.size = sizeof(desc);
  desc.width = width;
  desc.height = height;
  desc.scale = scale;
  desc.refresh_rate_hz = 60.0f;
  return desc;
}

// RAII for contexts in tests.
struct Context {
  Context() { CHECK(ui_context_create(nullptr, &ptr) == UI_OK); }
  ~Context() { ui_context_destroy(ptr); }
  ui_context* ptr = nullptr;
};

}  // namespace

TEST_CASE("C API version and error names") {
  CHECK(ui_get_version() == UI_VERSION);
  for (int code = 0; code < static_cast<int>(kErrorCodeCount); ++code) {
    CHECK(std::string(ui_error_code_name(code)) != "UI_ERROR_UNKNOWN");
  }
  CHECK(std::string(ui_error_code_name(UI_ERROR_WRONG_THREAD)) == "UI_ERROR_WRONG_THREAD");
  CHECK(std::string(ui_error_code_name(-1)) == "UI_ERROR_UNKNOWN");
  CHECK(std::string(ui_error_code_name(99)) == "UI_ERROR_UNKNOWN");
}

TEST_CASE("C API context creation validates its descriptor") {
  ui_context* context = nullptr;
  CHECK(ui_context_create(nullptr, nullptr) == UI_ERROR_INVALID_ARGUMENT);
  ui_context_desc desc{};
  desc.size = 0;
  CHECK(ui_context_create(&desc, &context) == UI_ERROR_INVALID_ARGUMENT);
  desc.size = sizeof(desc);
  desc.min_log_level = 42;
  CHECK(ui_context_create(&desc, &context) == UI_ERROR_INVALID_ARGUMENT);
  // An older caller's smaller struct: missing fields default to zero.
  desc.size = sizeof(uint32_t);
  CHECK(ui_context_create(&desc, &context) == UI_OK);
  ui_context_destroy(context);
  ui_context_destroy(nullptr);
}

TEST_CASE("C API logging goes to the callback, filtered by level") {
  Context c;
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(c.ptr, record_log, &logs, UI_LOG_WARNING) == UI_OK);
  c.ptr->logger.log(LogLevel::Info, ErrorCode::Ok, "filtered");
  c.ptr->logger.log(LogLevel::Error, ErrorCode::Internal, "kept");
  UI_REQUIRE(logs.size() == 1);
  CHECK(logs[0].level == UI_LOG_ERROR);
  CHECK(logs[0].code == UI_ERROR_INTERNAL);
  CHECK(logs[0].message == "kept");
  CHECK(ui_context_set_log_callback(nullptr, record_log, &logs, UI_LOG_INFO) ==
        UI_ERROR_INVALID_ARGUMENT);
  CHECK(ui_context_set_log_callback(c.ptr, record_log, &logs, 7) == UI_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("invalid handles crossing the C API are reported, never UB") {
  Context c;
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(c.ptr, record_log, &logs, UI_LOG_TRACE) == UI_OK);

  const ui_surface_desc desc = surface_desc();
  ui_handle surface = UI_NULL_HANDLE;
  CHECK(ui_surface_create(c.ptr, &desc, &surface) == UI_OK);
  CHECK(surface != UI_NULL_HANDLE);
  CHECK(surface < (uint64_t{1} << 53));  // Exact in JavaScript.
  CHECK(ui_surface_destroy(c.ptr, surface) == UI_OK);

  CHECK(ui_surface_frame(c.ptr, UI_NULL_HANDLE, 0) == UI_ERROR_INVALID_HANDLE);
  CHECK(ui_surface_frame(c.ptr, surface, 0) == UI_ERROR_STALE_HANDLE);
  const ui_handle texture = Handle::make(HandleType::Texture, 1, 0).bits();
  CHECK(ui_surface_resize(c.ptr, texture, 1, 1, 1.0f) == UI_ERROR_WRONG_HANDLE_TYPE);
  CHECK(ui_surface_destroy(c.ptr, ~uint64_t{0}) == UI_ERROR_INVALID_HANDLE);  // Forged.

  UI_REQUIRE(logs.size() == 4);
  CHECK(logs[0].code == UI_ERROR_INVALID_HANDLE);
  CHECK(logs[0].message.find("ui_surface_frame: ") == 0);
  CHECK(logs[1].code == UI_ERROR_STALE_HANDLE);
  CHECK(logs[2].code == UI_ERROR_WRONG_HANDLE_TYPE);
  CHECK(logs[2].message.find("ui_surface_resize: ") == 0);
  CHECK(logs[3].code == UI_ERROR_INVALID_HANDLE);
  for (const CLogRecord& log : logs) {
    CHECK(log.level == UI_LOG_ERROR);
    CHECK(log.message.find("(handle 0x") != std::string::npos);
  }
}

TEST_CASE("engine-to-host events are queued and drained with poll") {
  Context c;
  const ui_surface_desc desc = surface_desc(640, 480, 2.0f);
  ui_handle surface = UI_NULL_HANDLE;
  CHECK(ui_surface_create(c.ptr, &desc, &surface) == UI_OK);

  ui_event event = poll(c.ptr);
  CHECK(event.type == UI_EVENT_FRAME_REQUESTED);
  CHECK(event.surface == surface);
  CHECK(poll(c.ptr).type == UI_EVENT_NONE);

  CHECK(ui_surface_frame(c.ptr, surface, 1'000'000) == UI_OK);
  CHECK(poll(c.ptr).type == UI_EVENT_NONE);  // Nothing changed: no new frame.
  CHECK(ui_surface_resize(c.ptr, surface, 1280, 960, 2.0f) == UI_OK);
  event = poll(c.ptr);
  CHECK(event.type == UI_EVENT_FRAME_REQUESTED);
  CHECK(ui_surface_resize(c.ptr, surface, 10, 10, 0.0f) == UI_ERROR_INVALID_ARGUMENT);

  // Without a log callback, diagnostics are queued as EVENT_LOG; the message
  // stays valid until the next poll.
  CHECK(ui_surface_frame(c.ptr, UI_NULL_HANDLE, 0) == UI_ERROR_INVALID_HANDLE);
  event = poll(c.ptr);
  CHECK(event.type == UI_EVENT_LOG);
  CHECK(event.code == UI_ERROR_INVALID_HANDLE);
  CHECK(event.level == UI_LOG_ERROR);
  UI_REQUIRE(event.message != nullptr);
  CHECK(std::string(event.message, event.message_length).find("ui_surface_frame") == 0);
  CHECK(poll(c.ptr).type == UI_EVENT_NONE);
}

TEST_CASE("output structs are never written past the caller's size") {
  Context c;
  struct OldEvent {
    uint32_t size;
    int32_t type;
  };
  unsigned char buffer[64];
  std::memset(buffer, 0xAB, sizeof(buffer));
  OldEvent old{sizeof(OldEvent), -1};
  std::memcpy(buffer, &old, sizeof(old));
  CHECK(ui_context_poll_event(c.ptr, reinterpret_cast<ui_event*>(buffer)) == UI_OK);
  std::memcpy(&old, buffer, sizeof(old));
  CHECK(old.size == sizeof(OldEvent));
  CHECK(old.type == UI_EVENT_NONE);
  CHECK(buffer[sizeof(OldEvent)] == 0xAB);  // Untouched.

  ui_event too_small{};
  too_small.size = 4;
  CHECK(ui_context_poll_event(c.ptr, &too_small) == UI_ERROR_INVALID_ARGUMENT);
}

namespace {

struct ReentrantHost {
  ui_context* context = nullptr;
  ui_handle destroy_me = UI_NULL_HANDLE;
  int frame_requests = 0;
  ui_error_code destroy_result = UI_ERROR_INTERNAL;
  ui_error_code frame_after_destroy = UI_OK;
};

void reentrant_event(void* user_data, const ui_event* event) {
  auto* host = static_cast<ReentrantHost*>(user_data);
  if (event->type != UI_EVENT_FRAME_REQUESTED) {
    return;
  }
  ++host->frame_requests;
  if (event->surface == host->destroy_me) {
    // Called while the engine dispatches the resize: destruction is deferred,
    // and the handle is stale right away.
    host->destroy_result = ui_surface_destroy(host->context, event->surface);
    host->frame_after_destroy = ui_surface_frame(host->context, event->surface, 0);
  }
}

}  // namespace

TEST_CASE("API calls from inside callbacks are safe; destruction is deferred") {
  Context c;
  ReentrantHost host;
  host.context = c.ptr;
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(c.ptr, record_log, &logs, UI_LOG_TRACE) == UI_OK);
  CHECK(ui_context_set_event_callback(c.ptr, reentrant_event, &host) == UI_OK);

  const ui_surface_desc desc = surface_desc();
  ui_handle surface = UI_NULL_HANDLE;
  CHECK(ui_surface_create(c.ptr, &desc, &surface) == UI_OK);
  CHECK(host.frame_requests == 1);  // Delivered immediately, not queued.
  CHECK(ui_surface_frame(c.ptr, surface, 1) == UI_OK);

  host.destroy_me = surface;
  CHECK(ui_surface_resize(c.ptr, surface, 1024, 768, 1.0f) == UI_OK);
  CHECK(host.destroy_result == UI_OK);
  CHECK(host.frame_after_destroy == UI_ERROR_STALE_HANDLE);
  CHECK(ui::capi::engine(*c.ptr).surfaces.empty());  // Destroyed once the outer call returned.
  CHECK(ui::capi::engine(*c.ptr).core.surface_count() == 0);
  CHECK(ui_surface_frame(c.ptr, surface, 2) == UI_ERROR_STALE_HANDLE);

  // Destroying the context from inside a callback is refused.
  CHECK(ui_context_set_event_callback(
            c.ptr,
            [](void* user, const ui_event*) { ui_context_destroy(static_cast<ui_context*>(user)); },
            c.ptr) == UI_OK);
  ui_handle other = UI_NULL_HANDLE;
  CHECK(ui_surface_create(c.ptr, &desc, &other) == UI_OK);
  bool refused = false;
  for (const CLogRecord& log : logs) {
    refused = refused || log.message.find("ui_context_destroy: called from inside a callback") == 0;
  }
  CHECK(refused);
  CHECK(ui_context_set_event_callback(c.ptr, nullptr, nullptr) == UI_OK);
}

TEST_CASE("calls from a thread that does not own the context fail; wake works anywhere") {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
  MESSAGE("skipping: this wasm build has no threads");
  return;
#endif
  Context c;
  std::atomic<int> wakes{0};
  CHECK(ui_context_set_wake_callback(
            c.ptr, [](void* user) { static_cast<std::atomic<int>*>(user)->fetch_add(1); },
            &wakes) == UI_OK);

  ui_error_code from_other_thread = UI_OK;
  ui_error_code wake_result = UI_ERROR_INTERNAL;
  std::thread other([&] {
    ui_event event{};
    event.size = sizeof(event);
    from_other_thread = ui_context_poll_event(c.ptr, &event);
    wake_result = ui_context_wake(c.ptr);
    static_cast<void>(ui_context_wake(c.ptr));  // Coalesced.
  });
  other.join();
  CHECK(from_other_thread == UI_ERROR_WRONG_THREAD);
  CHECK(wake_result == UI_OK);
  CHECK(wakes.load() == 2);
  CHECK(poll(c.ptr).type == UI_EVENT_WAKE);
  CHECK(poll(c.ptr).type == UI_EVENT_NONE);
}

TEST_CASE("C API allocation stats drop when surfaces go away") {
  Context c;
  ui_allocation_stats before{};
  before.size = sizeof(before);
  CHECK(ui_context_get_allocation_stats(c.ptr, &before) == UI_OK);
  const ui_surface_desc desc = surface_desc();
  ui_handle surfaces[3] = {};
  for (ui_handle& s : surfaces) {
    CHECK(ui_surface_create(c.ptr, &desc, &s) == UI_OK);
  }
  ui_allocation_stats during{};
  during.size = sizeof(during);
  CHECK(ui_context_get_allocation_stats(c.ptr, &during) == UI_OK);
  CHECK(during.live_allocations > before.live_allocations);
  for (ui_handle s : surfaces) {
    CHECK(ui_surface_destroy(c.ptr, s) == UI_OK);
  }
  while (poll(c.ptr).type != UI_EVENT_NONE) {
  }
  ui_allocation_stats after{};
  after.size = sizeof(after);
  CHECK(ui_context_get_allocation_stats(c.ptr, &after) == UI_OK);
  CHECK(after.live_allocations < during.live_allocations);
  // Internal queues keep their capacity, so measure steady state: further
  // create/destroy cycles must not leave anything behind.
  for (int cycle = 0; cycle < 5; ++cycle) {
    for (ui_handle& s : surfaces) {
      CHECK(ui_surface_create(c.ptr, &desc, &s) == UI_OK);
    }
    for (ui_handle s : surfaces) {
      CHECK(ui_surface_destroy(c.ptr, s) == UI_OK);
    }
    while (poll(c.ptr).type != UI_EVENT_NONE) {
    }
  }
  ui_allocation_stats steady{};
  steady.size = sizeof(steady);
  CHECK(ui_context_get_allocation_stats(c.ptr, &steady) == UI_OK);
  CHECK(steady.live_allocations == after.live_allocations);
  CHECK(steady.live_bytes == after.live_bytes);
  CHECK(ui_context_get_allocation_stats(c.ptr, nullptr) == UI_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("C API converts text offsets between UTF-8 and UTF-16") {
  const char text[] =
      "a\xF0\x9F\x98\x80"
      "b";  // a, U+1F600, b.
  uint32_t out = 0;
  CHECK(ui_utf8_to_utf16_offset(text, 6, 5, &out) == UI_OK);
  CHECK(out == 3);
  CHECK(ui_utf16_to_utf8_offset(text, 6, 3, &out) == UI_OK);
  CHECK(out == 5);
  CHECK(ui_utf16_to_utf8_offset(text, 6, 2, &out) == UI_ERROR_INVALID_ARGUMENT);
  CHECK(ui_utf8_to_utf16_offset(text, 6, 3, &out) == UI_ERROR_INVALID_ARGUMENT);
  CHECK(ui_utf8_to_utf16_offset(nullptr, 0, 0, &out) == UI_OK);
  CHECK(ui_utf8_to_utf16_offset(text, 6, 0, nullptr) == UI_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("the resolve helper reports invalid handles for any slot map") {
  Context c;
  std::vector<CLogRecord> logs;
  CHECK(ui_context_set_log_callback(c.ptr, record_log, &logs, UI_LOG_TRACE) == UI_OK);
  {
    SlotMap<int> nodes(c.ptr->memory, HandleType::Node);
    const Handle live = *nodes.emplace(5);
    auto ok = ui::capi::resolve(*c.ptr, nodes, live.bits(), "ui_node_test");
    UI_REQUIRE(ok.has_value());
    CHECK(**ok == 5);
    CHECK(ui::capi::resolve(*c.ptr, nodes, UI_NULL_HANDLE, "ui_node_test").error().code ==
          ErrorCode::InvalidHandle);
  }
  UI_REQUIRE(logs.size() == 1);
  CHECK(logs[0].message.find("ui_node_test: ") == 0);
}
