/*
 * AxeUI public C API.
 *
 * GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit by hand;
 * CI fails if this file is out of date.
 *
 * Conventions (they apply to every function):
 *
 * - Language: C11; also valid C++. No C++ types. All declarations are
 *   extern "C".
 *
 * - Naming: functions and types use `ui_` in snake_case (ui_context_create,
 *   ui_error_code); macros and constants use `UI_` (UI_ERROR_STALE_HANDLE).
 *   Functions are named ui_<object>_<verb>.
 *
 * - ABI rules, so every language can bind the API mechanically:
 *   - no structs or unions by value: structs are passed by pointer only, and
 *     there are no unions or bitfields;
 *   - fixed-width integer types only (no int, long, size_t, bool);
 *   - enums cross the boundary as int32_t (each enum is a typedef of int32_t
 *     plus constants);
 *   - booleans are uint8_t (0 or 1);
 *   - every struct starts with `uint32_t size`, set by the caller to
 *     sizeof(struct), so fields can be appended in later versions; structs
 *     have no interior padding on 64-bit or wasm32 targets.
 *
 * - Errors: fallible functions return ui_error_code; UI_OK (0) is success.
 *   Results come back through out-parameters, written only on success.
 *   Errors that need more detail than a code are also reported as
 *   diagnostics (log callback or UI_EVENT_LOG).
 *
 * - Objects: the root object, ui_context, is an opaque pointer. Objects inside
 *   a context are named by ui_handle, an opaque 64-bit value whose top 11
 *   bits are always zero, so every handle is an exact integer in JavaScript
 *   and Dart-on-web (< 2^53). 0 (UI_NULL_HANDLE) is never valid. Stale,
 *   mistyped or forged handles never cause undefined behavior: the call
 *   fails with UI_ERROR_STALE_HANDLE / UI_ERROR_WRONG_HANDLE_TYPE /
 *   UI_ERROR_INVALID_HANDLE and a diagnostic is reported.
 *
 * - Strings: UTF-8, passed as pointer + uint32_t byte length (not
 *   NUL-terminated) unless documented otherwise. Every string parameter and
 *   return value documents its ownership. The caller never frees strings
 *   returned by the library.
 *
 * - Text positions: any function that exposes positions inside text states
 *   whether offsets are UTF-8 bytes or UTF-16 code units.
 *   ui_utf8_to_utf16_offset() and ui_utf16_to_utf8_offset() convert between
 *   them.
 *
 * - Events: engine-to-host events are queued; the host drains them with
 *   ui_context_poll_event(). Callbacks (log, event) are an optional
 *   convenience that bypasses the queue.
 *
 * - Reentrancy: calling the API from inside a callback is allowed. Object
 *   destruction requested while the engine is dispatching is deferred until
 *   the outermost call returns; the handle becomes stale immediately.
 *
 * - Threading: a context belongs to the thread that created it. Every call
 *   on it must come from that thread, or it fails with UI_ERROR_WRONG_THREAD.
 *   Exceptions, callable from any thread: ui_context_wake(), and functions
 *   that take no context.
 */

#ifndef UI_UI_H
#define UI_UI_H

#include <stdint.h>

#if defined(_WIN32) && defined(UI_SHARED)
#if defined(UI_BUILDING_LIBRARY)
#define UI_API __declspec(dllexport)
#else
#define UI_API __declspec(dllimport)
#endif
#elif defined(UI_SHARED) && (defined(__GNUC__) || defined(__clang__))
#define UI_API __attribute__((visibility("default")))
#else
#define UI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Version ------------------------------------------------------------ */

#define UI_VERSION_MAJOR 0
#define UI_VERSION_MINOR 2
#define UI_VERSION_PATCH 0

/* Packs a version as 0x00MMmmpp (major, minor, patch; 8 bits each).
 * (`0u + x` converts to unsigned without a cast, so the macro stays
 * warning-free when used from C++ with -Wold-style-cast.) */
#define UI_MAKE_VERSION(major, minor, patch) \
  (((0u + (major)) << 16) | ((0u + (minor)) << 8) | (0u + (patch)))

/* The version this header describes. */
#define UI_VERSION UI_MAKE_VERSION(UI_VERSION_MAJOR, UI_VERSION_MINOR, UI_VERSION_PATCH)

/* ---- Handles ------------------------------------------------------------ */

/* Opaque reference to an object inside a ui_context (< 2^53). Treat it as
 * an identifier only: do not decode or construct it. */
typedef uint64_t ui_handle;

#define UI_NULL_HANDLE UINT64_C(0)

/* Opaque root object. */
typedef struct ui_context ui_context;

/* ---- Enums (int32_t) ----------------------------------------------------- */

/* Result of every fallible function. Mirrors ui::foundation::ErrorCode. */
typedef int32_t ui_error_code;
enum {
  UI_OK = 0,  /* Success. */
  UI_ERROR_INVALID_ARGUMENT = 1,
  UI_ERROR_OUT_OF_MEMORY = 2,
  UI_ERROR_INVALID_HANDLE = 3,  /* Null handle, or one never issued. */
  UI_ERROR_STALE_HANDLE = 4,  /* Its object has been destroyed. */
  UI_ERROR_WRONG_HANDLE_TYPE = 5,  /* Refers to a different kind of object. */
  UI_ERROR_CAPACITY_EXCEEDED = 6,
  UI_ERROR_UNSUPPORTED = 7,
  UI_ERROR_UNAVAILABLE = 8,
  UI_ERROR_INVALID_STATE = 9,
  UI_ERROR_INTERNAL = 10,
  UI_ERROR_GPU = 11,
  UI_ERROR_DEVICE_LOST = 12,
  UI_ERROR_WRONG_THREAD = 13,  /* Called from a thread that does not own the context. */
};

typedef int32_t ui_log_level;
enum {
  UI_LOG_TRACE = 0,
  UI_LOG_DEBUG = 1,
  UI_LOG_INFO = 2,
  UI_LOG_WARNING = 3,
  UI_LOG_ERROR = 4,
};

/* Engine-to-host events, drained with ui_context_poll_event(). */
typedef int32_t ui_event_type;
enum {
  UI_EVENT_NONE = 0,  /* The queue is empty. */
  UI_EVENT_LOG = 1,  /* A diagnostic (when no log callback is set): level, code, message. */
  UI_EVENT_FRAME_REQUESTED = 2,  /* The surface wants a frame: call ui_surface_frame() at its next vsync. */
  UI_EVENT_WAKE = 3,  /* ui_context_wake() was called. */
};

/* ---- Structs ------------------------------------------------------------ */

/* Input. Parameters for ui_context_create(). */
typedef struct ui_context_desc {
  /* sizeof(ui_context_desc). */
  uint32_t size;
  /* Diagnostics below this level are dropped. */
  ui_log_level min_log_level;
} ui_context_desc;

/*
 * Input. A host-owned drawable area (window, view, canvas) registered with
 * ui_surface_create().
 */
typedef struct ui_surface_desc {
  /* sizeof(ui_surface_desc). */
  uint32_t size;
  /* Physical pixels. */
  uint32_t width;
  /* Physical pixels. */
  uint32_t height;
  /* Physical pixels per logical pixel, in [1/64, 64]. */
  float scale;
  /* Display refresh rate; 0 if unknown. */
  float refresh_rate_hz;
} ui_surface_desc;

/*
 * Output. Memory allocated by a context. The caller sets size; the library
 * fills the rest.
 */
typedef struct ui_allocation_stats {
  /* sizeof(ui_allocation_stats), set by the caller. */
  uint32_t size;
  /* Must be ignored. */
  uint32_t reserved;
  uint64_t live_allocations;
  uint64_t live_bytes;
  uint64_t peak_allocations;
  uint64_t peak_bytes;
  uint64_t total_allocations;
} ui_allocation_stats;

/*
 * Output. One engine-to-host event. The caller sets size; the library fills
 * the rest.
 */
typedef struct ui_event {
  /* sizeof(ui_event), set by the caller. */
  uint32_t size;
  ui_event_type type;
  /* EVENT_LOG: the error code (UI_OK for information). */
  ui_error_code code;
  /* EVENT_LOG: severity. */
  ui_log_level level;
  /* EVENT_FRAME_REQUESTED: the surface. */
  ui_handle surface;
  /*
   * EVENT_LOG: UTF-8 text of message_length bytes (not NUL-terminated); NULL
   * otherwise. Ownership: Owned by the context; valid until the next
   * ui_context_poll_event() or ui_context_destroy().
   */
  const char* message;
  uint32_t message_length;
  /* Must be ignored. */
  uint32_t reserved;
} ui_event;

/* ---- Callbacks ---------------------------------------------------------- */

/*
 * Receives diagnostics, including invalid handles passed to the API. Called
 * on the owning thread, possibly while the engine is dispatching. `message`:
 * UTF-8, message_length bytes, not NUL-terminated. Ownership: Valid only
 * during the call.
 */
typedef void (*ui_log_callback)(void* user_data, ui_log_level level, ui_error_code code, const char* message, uint32_t message_length);

/*
 * Optional convenience: receives events immediately instead of queuing them
 * for ui_context_poll_event(). Called on the owning thread, possibly while
 * the engine is dispatching. `event`: Ownership: Valid only during the call.
 */
typedef void (*ui_event_callback)(void* user_data, const ui_event* event);

/*
 * Called by ui_context_wake() on the calling thread, so the host can
 * interrupt its own event wait. Must be thread-safe and must not call the
 * API.
 */
typedef void (*ui_wake_callback)(void* user_data);

/* ---- Functions ---------------------------------------------------------- */

/*
 * The version of the linked library, packed like UI_MAKE_VERSION. Compare
 * with UI_VERSION to detect a header/library mismatch.
 *
 * Thread: any.
 */
UI_API uint32_t ui_get_version(void);

/*
 * Stable name of an error code ("UI_ERROR_STALE_HANDLE"); "UI_ERROR_UNKNOWN"
 * for unknown values.
 *
 * Thread: any.
 * Returns: Static; never NULL, never freed.
 */
UI_API const char* ui_error_code_name(ui_error_code code);

/*
 * Creates a context. The calling thread becomes its owner: every other
 * function taking this context must be called on that thread, except
 * ui_context_wake().
 *
 * Thread: any.
 * desc: NULL for defaults; may be NULL.
 * out_context: Receives the context on success.
 */
UI_API ui_error_code ui_context_create(const ui_context_desc* desc, ui_context** out_context);

/*
 * Destroys a context and everything in it. NULL is ignored. Must not be
 * called from inside a callback.
 *
 * Thread: the context's owner.
 * context: may be NULL.
 */
UI_API void ui_context_destroy(ui_context* context);

/*
 * Routes diagnostics to callback (NULL: they are queued as UI_EVENT_LOG
 * instead).
 *
 * Thread: the context's owner.
 * callback: may be NULL.
 */
UI_API ui_error_code ui_context_set_log_callback(ui_context* context, ui_log_callback callback, void* user_data, ui_log_level min_level);

/*
 * Delivers events to callback immediately instead of queuing them (NULL:
 * queue).
 *
 * Thread: the context's owner.
 * callback: may be NULL.
 */
UI_API ui_error_code ui_context_set_event_callback(ui_context* context, ui_event_callback callback, void* user_data);

/*
 * Sets the host's thread-safe wake-up hook used by ui_context_wake(). Set it
 * before other threads may call ui_context_wake().
 *
 * Thread: the context's owner.
 * callback: may be NULL.
 */
UI_API ui_error_code ui_context_set_wake_callback(ui_context* context, ui_wake_callback callback, void* user_data);

/*
 * Removes the oldest queued event into out_event. Sets out_event->type to
 * UI_EVENT_NONE when the queue is empty.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_context_poll_event(ui_context* context, ui_event* out_event);

/*
 * Thread-safe: queues one UI_EVENT_WAKE (coalesced) and calls the wake
 * callback. The only function that may be called from any thread.
 *
 * Thread: any.
 */
UI_API ui_error_code ui_context_wake(ui_context* context);

/*
 * Memory currently and historically allocated by the context.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_context_get_allocation_stats(const ui_context* context, ui_allocation_stats* out_stats);

/*
 * Registers a host-owned surface. The engine will ask for its first frame
 * with UI_EVENT_FRAME_REQUESTED.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_surface_create(ui_context* context, const ui_surface_desc* desc, ui_handle* out_surface);

/*
 * Unregisters a surface. Called during dispatch (from a callback),
 * destruction is deferred until dispatch ends; the handle is stale
 * immediately.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_surface_destroy(ui_context* context, ui_handle surface);

/*
 * Reports a new physical size and scale factor.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_surface_resize(ui_context* context, ui_handle surface, uint32_t width, uint32_t height, float scale);

/*
 * Delivers the frame requested with UI_EVENT_FRAME_REQUESTED. timestamp_ns
 * is the host's monotonic clock.
 *
 * Thread: the context's owner.
 */
UI_API ui_error_code ui_surface_frame(ui_context* context, ui_handle surface, int64_t timestamp_ns);

/*
 * Converts a byte offset into UTF-8 text to the matching UTF-16 code-unit
 * offset. Fails with UI_ERROR_INVALID_ARGUMENT for invalid UTF-8 or an
 * offset that is not on a code-point boundary.
 *
 * Thread: any.
 * text: UTF-8, text_length bytes; ownership: Borrowed for the call.
 */
UI_API ui_error_code ui_utf8_to_utf16_offset(const char* text, uint32_t text_length, uint32_t utf8_offset, uint32_t* out_utf16_offset);

/*
 * Converts a UTF-16 code-unit offset to the matching byte offset into UTF-8
 * text. Fails with UI_ERROR_INVALID_ARGUMENT for invalid UTF-8 or an offset
 * inside a surrogate pair or past the end.
 *
 * Thread: any.
 * text: UTF-8, text_length bytes; ownership: Borrowed for the call.
 */
UI_API ui_error_code ui_utf16_to_utf8_offset(const char* text, uint32_t text_length, uint32_t utf16_offset, uint32_t* out_utf8_offset);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UI_UI_H */
