/*
 * AxeUI public C API.
 *
 * Conventions (apply to every function added to this header):
 *
 * - Language: C11. The header compiles as C11 and as C++; it contains no C++
 *   types. Every declaration is wrapped in extern "C".
 *
 * - Naming: everything is prefixed. Functions and types use `ui_` in
 *   snake_case (ui_context_create, ui_error_code), macros and enumerators use
 *   `UI_` in upper case (UI_ERROR_STALE_HANDLE). Functions are named
 *   ui_<object>_<verb>.
 *
 * - Errors: fallible functions return ui_error_code; UI_OK (0) is success.
 *   Results come back through out-parameters, which are written only on
 *   success. Functions that cannot fail return void or a value directly.
 *   Errors that need more detail than a code are also reported through the
 *   context's log callback.
 *
 * - Objects: the root object, ui_context, is an opaque pointer created and
 *   destroyed explicitly. Every object inside a context is referred to by a
 *   ui_handle, an opaque 64-bit value. 0 (UI_NULL_HANDLE) is never a valid
 *   handle. A handle that is stale (its object was destroyed) or refers to the
 *   wrong kind of object never causes undefined behavior: the call fails with
 *   UI_ERROR_STALE_HANDLE / UI_ERROR_WRONG_HANDLE_TYPE / UI_ERROR_INVALID_HANDLE
 *   and the error is reported through the log callback.
 *
 * - Strings: UTF-8, passed as pointer + byte length (not NUL-terminated)
 *   unless documented otherwise. Strings returned by the API are static or
 *   owned by the context; the caller never frees them.
 *
 * - Threading: a context and its objects are used from one thread (the UI
 *   thread). Functions without a context argument are thread-safe.
 *
 * - ABI: enums are passed as int32_t-sized values; enumerators are only ever
 *   appended. Structs passed by pointer may grow at the end only.
 */

#ifndef UI_UI_H
#define UI_UI_H

#include <stddef.h>
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
#define UI_VERSION_MINOR 1
#define UI_VERSION_PATCH 0

/* Packs a version as 0x00MMmmpp (major, minor, patch; 8 bits each).
 * (`0u + x` converts to unsigned without a cast, so the macro stays
 * warning-free when used from C++ with -Wold-style-cast.) */
#define UI_MAKE_VERSION(major, minor, patch) \
  (((0u + (major)) << 16) | ((0u + (minor)) << 8) | (0u + (patch)))

/* The version this header describes. */
#define UI_VERSION UI_MAKE_VERSION(UI_VERSION_MAJOR, UI_VERSION_MINOR, UI_VERSION_PATCH)

/* The version of the linked library. Compare with UI_VERSION to detect a
 * header/library mismatch. */
UI_API uint32_t ui_get_version(void);

/* ---- Errors ------------------------------------------------------------- */

typedef enum ui_error_code {
  UI_OK = 0,
  UI_ERROR_INVALID_ARGUMENT = 1,
  UI_ERROR_OUT_OF_MEMORY = 2,
  UI_ERROR_INVALID_HANDLE = 3,    /* Null handle, or one never issued. */
  UI_ERROR_STALE_HANDLE = 4,      /* Its object has been destroyed. */
  UI_ERROR_WRONG_HANDLE_TYPE = 5, /* Refers to a different kind of object. */
  UI_ERROR_CAPACITY_EXCEEDED = 6,
  UI_ERROR_UNSUPPORTED = 7,
  UI_ERROR_UNAVAILABLE = 8,
  UI_ERROR_INVALID_STATE = 9,
  UI_ERROR_INTERNAL = 10,
  UI_ERROR_GPU = 11,
  UI_ERROR_DEVICE_LOST = 12,
  UI_ERROR_WRONG_THREAD = 13,
  UI_ERROR_FORCE_32BIT = 0x7FFFFFFF
} ui_error_code;

/* Static, stable name of an error code ("UI_ERROR_STALE_HANDLE"); never NULL. */
UI_API const char* ui_error_code_name(ui_error_code code);

/* ---- Handles ------------------------------------------------------------ */

/* Opaque reference to an object inside a ui_context. Treat it as an
 * identifier only: do not decode or construct it. */
typedef uint64_t ui_handle;

#define UI_NULL_HANDLE UINT64_C(0)

/* ---- Logging ------------------------------------------------------------ */

typedef enum ui_log_level {
  UI_LOG_TRACE = 0,
  UI_LOG_DEBUG = 1,
  UI_LOG_INFO = 2,
  UI_LOG_WARNING = 3,
  UI_LOG_ERROR = 4,
  UI_LOG_FORCE_32BIT = 0x7FFFFFFF
} ui_log_level;

/* Receives diagnostics, including invalid handles passed to the API.
 * `message` is UTF-8 of `message_length` bytes and is valid only during the
 * call. `code` is UI_OK for purely informational messages. */
typedef void (*ui_log_callback)(void* user_data, ui_log_level level, ui_error_code code,
                                const char* message, size_t message_length);

/* ---- Context ------------------------------------------------------------ */

typedef struct ui_context ui_context;

/* Creates a context. On success writes it to *out_context. */
UI_API ui_error_code ui_context_create(ui_context** out_context);

/* Destroys a context and everything in it. NULL is ignored. */
UI_API void ui_context_destroy(ui_context* context);

/* Routes the context's diagnostics to `callback` (NULL disables logging).
 * Messages below `min_level` are dropped. */
UI_API ui_error_code ui_context_set_log_callback(ui_context* context, ui_log_callback callback,
                                                 void* user_data, ui_log_level min_level);

typedef struct ui_allocation_stats {
  uint64_t live_allocations;
  uint64_t live_bytes;
  uint64_t peak_allocations;
  uint64_t peak_bytes;
  uint64_t total_allocations;
} ui_allocation_stats;

/* Memory currently and historically allocated by the context. */
UI_API ui_error_code ui_context_get_allocation_stats(const ui_context* context,
                                                     ui_allocation_stats* out_stats);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* UI_UI_H */
