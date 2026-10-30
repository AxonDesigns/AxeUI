// Binding smoke test: loads the shared library, calls the version function,
// registers the log ("error") callback and triggers an error with an invalid
// handle. Run by CTest in native builds:
//
//   dart run bin/smoke.dart <path/to/libaxeui.so|.dylib|axeui.dll>

import 'dart:ffi';
import 'dart:io';

import 'package:axeui_bindings/axeui.dart';
import 'package:ffi/ffi.dart';

void check(bool condition, String what) {
  if (!condition) {
    stderr.writeln('FAILED: $what');
    exit(1);
  }
}

final logs = <(int, int, String)>[];

void onLog(Pointer<Void> userData, int level, int code, Pointer<Char> message, int length) {
  logs.add((level, code, message.cast<Utf8>().toDartString(length: length)));
}

void main(List<String> args) {
  check(args.length == 1, 'usage: dart run bin/smoke.dart <library>');
  final ui = UiBindings(DynamicLibrary.open(args[0]));

  check(ui.getVersion() == UI_VERSION, 'library version matches the bindings');
  check(ui.errorCodeName(UI_ERROR_STALE_HANDLE).cast<Utf8>().toDartString() ==
      'UI_ERROR_STALE_HANDLE', 'error names');

  final contextOut = calloc<Pointer<UiContext>>();
  check(ui.contextCreate(nullptr, contextOut) == UI_OK, 'context created');
  final context = contextOut.value;

  final callback = NativeCallable<UiLogCallbackNative>.isolateLocal(onLog);
  check(ui.contextSetLogCallback(context, callback.nativeFunction, nullptr, UI_LOG_TRACE) == UI_OK,
      'log callback registered');

  final desc = calloc<UiSurfaceDesc>();
  desc.ref
    ..size = sizeOf<UiSurfaceDesc>()
    ..width = 320
    ..height = 200
    ..scale = 1.5
    ..refreshRateHz = 60;
  final surfaceOut = calloc<Uint64>();
  check(ui.surfaceCreate(context, desc, surfaceOut) == UI_OK, 'surface created');
  final surface = surfaceOut.value;
  check(surface > 0 && surface < (1 << 53), 'handles fit in 53 bits');

  final event = calloc<UiEvent>();
  event.ref.size = sizeOf<UiEvent>();
  check(ui.contextPollEvent(context, event) == UI_OK, 'poll');
  check(event.ref.type == UI_EVENT_FRAME_REQUESTED && event.ref.surface == surface,
      'first frame requested for the surface');

  check(ui.surfaceDestroy(context, surface) == UI_OK, 'surface destroyed');
  // Invalid handle: an error code plus a diagnostic through the callback.
  check(ui.surfaceDestroy(context, surface) == UI_ERROR_STALE_HANDLE, 'stale handle rejected');
  check(logs.length == 1, 'one diagnostic reported (got ${logs.length})');
  check(logs[0].$1 == UI_LOG_ERROR && logs[0].$2 == UI_ERROR_STALE_HANDLE,
      'diagnostic level and code');
  check(logs[0].$3.startsWith('ui_surface_destroy'), 'diagnostic names the function');

  ui.contextDestroy(context);
  callback.close();
  calloc
    ..free(contextOut)
    ..free(desc)
    ..free(surfaceOut)
    ..free(event);
  print('Dart binding smoke test passed');
}
