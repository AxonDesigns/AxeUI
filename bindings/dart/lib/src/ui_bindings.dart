// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.
//
// dart:ffi bindings for the AxeUI C API. Handles are Dart ints (< 2^53,
// so they are exact on the web too).

// ignore_for_file: constant_identifier_names, non_constant_identifier_names

import 'dart:ffi';

const int UI_VERSION = 512;
const int UI_NULL_HANDLE = 0;

const int UI_OK = 0;
const int UI_ERROR_INVALID_ARGUMENT = 1;
const int UI_ERROR_OUT_OF_MEMORY = 2;
const int UI_ERROR_INVALID_HANDLE = 3;
const int UI_ERROR_STALE_HANDLE = 4;
const int UI_ERROR_WRONG_HANDLE_TYPE = 5;
const int UI_ERROR_CAPACITY_EXCEEDED = 6;
const int UI_ERROR_UNSUPPORTED = 7;
const int UI_ERROR_UNAVAILABLE = 8;
const int UI_ERROR_INVALID_STATE = 9;
const int UI_ERROR_INTERNAL = 10;
const int UI_ERROR_GPU = 11;
const int UI_ERROR_DEVICE_LOST = 12;
const int UI_ERROR_WRONG_THREAD = 13;

const int UI_LOG_TRACE = 0;
const int UI_LOG_DEBUG = 1;
const int UI_LOG_INFO = 2;
const int UI_LOG_WARNING = 3;
const int UI_LOG_ERROR = 4;

const int UI_EVENT_NONE = 0;
const int UI_EVENT_LOG = 1;
const int UI_EVENT_FRAME_REQUESTED = 2;
const int UI_EVENT_WAKE = 3;

final class UiContext extends Opaque {}

final class UiContextDesc extends Struct {
  @Uint32()
  external int size;
  @Int32()
  external int minLogLevel;
}

final class UiSurfaceDesc extends Struct {
  @Uint32()
  external int size;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Float()
  external double scale;
  @Float()
  external double refreshRateHz;
}

final class UiAllocationStats extends Struct {
  @Uint32()
  external int size;
  @Uint32()
  external int reserved;
  @Uint64()
  external int liveAllocations;
  @Uint64()
  external int liveBytes;
  @Uint64()
  external int peakAllocations;
  @Uint64()
  external int peakBytes;
  @Uint64()
  external int totalAllocations;
}

final class UiEvent extends Struct {
  @Uint32()
  external int size;
  @Int32()
  external int type;
  @Int32()
  external int code;
  @Int32()
  external int level;
  @Uint64()
  external int surface;
  external Pointer<Char> message;
  @Uint32()
  external int messageLength;
  @Uint32()
  external int reserved;
}

typedef UiLogCallbackNative = Void Function(Pointer<Void>, Int32, Int32, Pointer<Char>, Uint32);
typedef UiLogCallbackDart = void Function(Pointer<Void>, int, int, Pointer<Char>, int);
typedef UiEventCallbackNative = Void Function(Pointer<Void>, Pointer<UiEvent>);
typedef UiEventCallbackDart = void Function(Pointer<Void>, Pointer<UiEvent>);
typedef UiWakeCallbackNative = Void Function(Pointer<Void>);
typedef UiWakeCallbackDart = void Function(Pointer<Void>);

class UiBindings {
  UiBindings(DynamicLibrary library)
      : getVersion = library.lookupFunction<Uint32 Function(), int Function()>('ui_get_version'),
        errorCodeName = library.lookupFunction<Pointer<Char> Function(Int32), Pointer<Char> Function(int)>('ui_error_code_name'),
        contextCreate = library.lookupFunction<Int32 Function(Pointer<UiContextDesc>, Pointer<Pointer<UiContext>>), int Function(Pointer<UiContextDesc>, Pointer<Pointer<UiContext>>)>('ui_context_create'),
        contextDestroy = library.lookupFunction<Void Function(Pointer<UiContext>), void Function(Pointer<UiContext>)>('ui_context_destroy'),
        contextSetLogCallback = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<NativeFunction<UiLogCallbackNative>>, Pointer<Void>, Int32), int Function(Pointer<UiContext>, Pointer<NativeFunction<UiLogCallbackNative>>, Pointer<Void>, int)>('ui_context_set_log_callback'),
        contextSetEventCallback = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<NativeFunction<UiEventCallbackNative>>, Pointer<Void>), int Function(Pointer<UiContext>, Pointer<NativeFunction<UiEventCallbackNative>>, Pointer<Void>)>('ui_context_set_event_callback'),
        contextSetWakeCallback = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<NativeFunction<UiWakeCallbackNative>>, Pointer<Void>), int Function(Pointer<UiContext>, Pointer<NativeFunction<UiWakeCallbackNative>>, Pointer<Void>)>('ui_context_set_wake_callback'),
        contextPollEvent = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<UiEvent>), int Function(Pointer<UiContext>, Pointer<UiEvent>)>('ui_context_poll_event'),
        contextWake = library.lookupFunction<Int32 Function(Pointer<UiContext>), int Function(Pointer<UiContext>)>('ui_context_wake'),
        contextGetAllocationStats = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<UiAllocationStats>), int Function(Pointer<UiContext>, Pointer<UiAllocationStats>)>('ui_context_get_allocation_stats'),
        surfaceCreate = library.lookupFunction<Int32 Function(Pointer<UiContext>, Pointer<UiSurfaceDesc>, Pointer<Uint64>), int Function(Pointer<UiContext>, Pointer<UiSurfaceDesc>, Pointer<Uint64>)>('ui_surface_create'),
        surfaceDestroy = library.lookupFunction<Int32 Function(Pointer<UiContext>, Uint64), int Function(Pointer<UiContext>, int)>('ui_surface_destroy'),
        surfaceResize = library.lookupFunction<Int32 Function(Pointer<UiContext>, Uint64, Uint32, Uint32, Float), int Function(Pointer<UiContext>, int, int, int, double)>('ui_surface_resize'),
        surfaceFrame = library.lookupFunction<Int32 Function(Pointer<UiContext>, Uint64, Int64), int Function(Pointer<UiContext>, int, int)>('ui_surface_frame'),
        utf8ToUtf16Offset = library.lookupFunction<Int32 Function(Pointer<Char>, Uint32, Uint32, Pointer<Uint32>), int Function(Pointer<Char>, int, int, Pointer<Uint32>)>('ui_utf8_to_utf16_offset'),
        utf16ToUtf8Offset = library.lookupFunction<Int32 Function(Pointer<Char>, Uint32, Uint32, Pointer<Uint32>), int Function(Pointer<Char>, int, int, Pointer<Uint32>)>('ui_utf16_to_utf8_offset');

  /// The version of the linked library, packed like UI_MAKE_VERSION. Compare with UI_VERSION to detect a header/library mismatch.
  final int Function() getVersion;
  /// Stable name of an error code ("UI_ERROR_STALE_HANDLE"); "UI_ERROR_UNKNOWN" for unknown values.
  final Pointer<Char> Function(int) errorCodeName;
  /// Creates a context. The calling thread becomes its owner: every other function taking this context must be called on that thread, except ui_context_wake().
  final int Function(Pointer<UiContextDesc>, Pointer<Pointer<UiContext>>) contextCreate;
  /// Destroys a context and everything in it. NULL is ignored. Must not be called from inside a callback.
  final void Function(Pointer<UiContext>) contextDestroy;
  /// Routes diagnostics to callback (NULL: they are queued as UI_EVENT_LOG instead).
  final int Function(Pointer<UiContext>, Pointer<NativeFunction<UiLogCallbackNative>>, Pointer<Void>, int) contextSetLogCallback;
  /// Delivers events to callback immediately instead of queuing them (NULL: queue).
  final int Function(Pointer<UiContext>, Pointer<NativeFunction<UiEventCallbackNative>>, Pointer<Void>) contextSetEventCallback;
  /// Sets the host's thread-safe wake-up hook used by ui_context_wake(). Set it before other threads may call ui_context_wake().
  final int Function(Pointer<UiContext>, Pointer<NativeFunction<UiWakeCallbackNative>>, Pointer<Void>) contextSetWakeCallback;
  /// Removes the oldest queued event into out_event. Sets out_event->type to UI_EVENT_NONE when the queue is empty.
  final int Function(Pointer<UiContext>, Pointer<UiEvent>) contextPollEvent;
  /// Thread-safe: queues one UI_EVENT_WAKE (coalesced) and calls the wake callback. The only function that may be called from any thread.
  final int Function(Pointer<UiContext>) contextWake;
  /// Memory currently and historically allocated by the context.
  final int Function(Pointer<UiContext>, Pointer<UiAllocationStats>) contextGetAllocationStats;
  /// Registers a host-owned surface. The engine will ask for its first frame with UI_EVENT_FRAME_REQUESTED.
  final int Function(Pointer<UiContext>, Pointer<UiSurfaceDesc>, Pointer<Uint64>) surfaceCreate;
  /// Unregisters a surface. Called during dispatch (from a callback), destruction is deferred until dispatch ends; the handle is stale immediately.
  final int Function(Pointer<UiContext>, int) surfaceDestroy;
  /// Reports a new physical size and scale factor.
  final int Function(Pointer<UiContext>, int, int, int, double) surfaceResize;
  /// Delivers the frame requested with UI_EVENT_FRAME_REQUESTED. timestamp_ns is the host's monotonic clock.
  final int Function(Pointer<UiContext>, int, int) surfaceFrame;
  /// Converts a byte offset into UTF-8 text to the matching UTF-16 code-unit offset. Fails with UI_ERROR_INVALID_ARGUMENT for invalid UTF-8 or an offset that is not on a code-point boundary.
  final int Function(Pointer<Char>, int, int, Pointer<Uint32>) utf8ToUtf16Offset;
  /// Converts a UTF-16 code-unit offset to the matching byte offset into UTF-8 text. Fails with UI_ERROR_INVALID_ARGUMENT for invalid UTF-8 or an offset inside a surrogate pair or past the end.
  final int Function(Pointer<Char>, int, int, Pointer<Uint32>) utf16ToUtf8Offset;
}
