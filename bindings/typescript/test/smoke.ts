// Binding smoke test: loads the wasm ES module, calls the version function,
// registers the log ("error") callback and triggers an error with an invalid
// handle. Run by CTest in the Emscripten build:
//
//   node test/smoke.ts <path/to/axeui.mjs>

import { pathToFileURL } from "node:url";

import { Ui, UiErrorCode, UiEventType, UiLogLevel, UI_VERSION } from "../ui.ts";
import type { UiModuleFactory } from "../ui_module.d.ts";

function check(condition: boolean, what: string): void {
  if (!condition) {
    console.error(`FAILED: ${what}`);
    process.exit(1);
  }
}

const modulePath = process.argv[2];
check(modulePath !== undefined, "usage: node test/smoke.ts <axeui.mjs>");
const factory = (await import(pathToFileURL(modulePath).href)).default as UiModuleFactory;
const ui = new Ui(await factory());

check(ui.getVersion() === UI_VERSION, "library version matches the bindings");
check(ui.errorCodeName(UiErrorCode.ERROR_STALE_HANDLE) === "UI_ERROR_STALE_HANDLE", "error names");

const created = ui.contextCreate({ minLogLevel: UiLogLevel.LOG_TRACE });
check(created.code === UiErrorCode.OK && created.outContext !== null, "context created");
const context = created.outContext!;

const logs: { level: number; code: number; message: string }[] = [];
check(
  ui.contextSetLogCallback(context, (level, code, message) => logs.push({ level, code, message }),
                           UiLogLevel.LOG_TRACE) === UiErrorCode.OK,
  "log callback registered");

const surface = ui.surfaceCreate(context, { width: 320, height: 200, scale: 1.5, refreshRateHz: 60 });
check(surface.code === UiErrorCode.OK && surface.outSurface !== null, "surface created");
const handle = surface.outSurface!;
check(Number.isSafeInteger(handle) && handle > 0, "handles are exact JavaScript numbers");

const event = ui.contextPollEvent(context);
check(event.code === UiErrorCode.OK && event.outEvent?.type === UiEventType.EVENT_FRAME_REQUESTED,
      "first frame requested");
check(event.outEvent?.surface === handle, "event carries the surface handle");

check(ui.surfaceDestroy(context, handle) === UiErrorCode.OK, "surface destroyed");
// Invalid handles: an error code plus a diagnostic through the callback.
check(ui.surfaceDestroy(context, handle) === UiErrorCode.ERROR_STALE_HANDLE, "stale handle rejected");
check(ui.surfaceFrame(context, 2 ** 53 - 1, 0) !== UiErrorCode.OK, "forged handle rejected");
check(logs.length === 2, `two diagnostics reported (got ${logs.length})`);
check(logs[0].code === UiErrorCode.ERROR_STALE_HANDLE && logs[0].message.startsWith("ui_surface_destroy"),
      "diagnostic names the function and the error");
check(logs[0].level === UiLogLevel.LOG_ERROR, "diagnostic level");

const offsets = ui.utf8ToUtf16Offset("a😀b", 5);
check(offsets.code === UiErrorCode.OK && offsets.outUtf16Offset === 3, "UTF-8 -> UTF-16 offsets");

ui.contextDestroy(context);
console.log("TypeScript binding smoke test passed");
