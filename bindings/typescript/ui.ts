// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.
//
// Thin TypeScript wrapper over the wasm module: converts handles between
// BigInt and number (handles are < 2^53, so they are exact numbers),
// strings between JS and UTF-8, and marshals structs, out-parameters and
// callbacks. Error codes are returned, not thrown.

import type { UiRawModule } from "./ui_module.d.ts";

export const UI_VERSION = 512;
export const UI_NULL_HANDLE = 0;

export type Handle = number;
export type ContextPtr = number;

export const UiErrorCode = {
  OK: 0,
  ERROR_INVALID_ARGUMENT: 1,
  ERROR_OUT_OF_MEMORY: 2,
  ERROR_INVALID_HANDLE: 3,
  ERROR_STALE_HANDLE: 4,
  ERROR_WRONG_HANDLE_TYPE: 5,
  ERROR_CAPACITY_EXCEEDED: 6,
  ERROR_UNSUPPORTED: 7,
  ERROR_UNAVAILABLE: 8,
  ERROR_INVALID_STATE: 9,
  ERROR_INTERNAL: 10,
  ERROR_GPU: 11,
  ERROR_DEVICE_LOST: 12,
  ERROR_WRONG_THREAD: 13,
} as const;

export const UiLogLevel = {
  LOG_TRACE: 0,
  LOG_DEBUG: 1,
  LOG_INFO: 2,
  LOG_WARNING: 3,
  LOG_ERROR: 4,
} as const;

export const UiEventType = {
  EVENT_NONE: 0,
  EVENT_LOG: 1,
  EVENT_FRAME_REQUESTED: 2,
  EVENT_WAKE: 3,
} as const;

export interface UiContextDesc {
  minLogLevel: number;
}
const CONTEXT_DESC_SIZE = 8;

export interface UiSurfaceDesc {
  width: number;
  height: number;
  scale: number;
  refreshRateHz: number;
}
const SURFACE_DESC_SIZE = 20;

export interface UiAllocationStats {
  liveAllocations: number;
  liveBytes: number;
  peakAllocations: number;
  peakBytes: number;
  totalAllocations: number;
}
const ALLOCATION_STATS_SIZE = 48;

export interface UiEvent {
  type: number;
  code: number;
  level: number;
  surface: number;
  message: string | null;
}
const EVENT_SIZE = 40;

const MAX_SAFE_HANDLE = 2n ** 53n;

function toHandle(value: bigint): Handle {
  if (value < 0n || value >= MAX_SAFE_HANDLE) {
    throw new RangeError(`handle ${value} does not fit in 53 bits`);
  }
  return Number(value);
}

export class Ui {
  private readonly callbacks = new Map<string, number>();
  private readonly encoder = new TextEncoder();
  private readonly decoder = new TextDecoder();

  readonly raw: UiRawModule;

  constructor(raw: UiRawModule) {
    this.raw = raw;
  }

  private view(): DataView {
    return new DataView(this.raw.HEAPU8.buffer);
  }

  private readString(pointer: number, length: number): string {
    return this.decoder.decode(this.raw.HEAPU8.subarray(pointer, pointer + length));
  }

  private writeString(text: string): [number, number] {
    const bytes = this.encoder.encode(text);
    const pointer = this.raw._malloc(Math.max(bytes.length, 1));
    this.raw.HEAPU8.set(bytes, pointer);
    return [pointer, bytes.length];
  }

  private writeUiContextDesc(value: UiContextDesc): number {
    const pointer = this.raw._malloc(8);
    const v = this.view();
    v.setUint32(pointer + 0, 8, true);
    v.setInt32(pointer + 4, value.minLogLevel, true);
    return pointer;
  }

  private writeUiSurfaceDesc(value: UiSurfaceDesc): number {
    const pointer = this.raw._malloc(20);
    const v = this.view();
    v.setUint32(pointer + 0, 20, true);
    v.setUint32(pointer + 4, value.width, true);
    v.setUint32(pointer + 8, value.height, true);
    v.setFloat32(pointer + 12, value.scale, true);
    v.setFloat32(pointer + 16, value.refreshRateHz, true);
    return pointer;
  }

  private readUiAllocationStats(pointer: number): UiAllocationStats {
    const v = this.view();
    return {
      liveAllocations: Number(v.getBigUint64(pointer + 8, true)),
      liveBytes: Number(v.getBigUint64(pointer + 16, true)),
      peakAllocations: Number(v.getBigUint64(pointer + 24, true)),
      peakBytes: Number(v.getBigUint64(pointer + 32, true)),
      totalAllocations: Number(v.getBigUint64(pointer + 40, true)),
    };
  }

  private readUiEvent(pointer: number): UiEvent {
    const v = this.view();
    return {
      type: v.getInt32(pointer + 4, true),
      code: v.getInt32(pointer + 8, true),
      level: v.getInt32(pointer + 12, true),
      surface: toHandle(v.getBigUint64(pointer + 16, true)),
      message: v.getUint32(pointer + 24, true) === 0 ? null : this.readString(v.getUint32(pointer + 24, true), v.getUint32(pointer + 28, true)),
    };
  }

  getVersion() {
    const code = this.raw._ui_get_version();
    return code;
  }

  errorCodeName(code: number) {
    return this.raw.UTF8ToString(this.raw._ui_error_code_name(code));
  }

  contextCreate(desc: UiContextDesc | null) {
    const descPtr = desc === null ? 0 : this.writeUiContextDesc(desc);
    const outContextPtr = this.raw._malloc(8);
    this.view().setBigUint64(outContextPtr, 0n, true);
    const code = this.raw._ui_context_create(descPtr, outContextPtr);
    const outContextValue = code === UiErrorCode.OK ? this.view().getUint32(outContextPtr, true) : null;
    if (descPtr !== 0) this.raw._free(descPtr);
    this.raw._free(outContextPtr);
    return { code, outContext: outContextValue };
  }

  contextDestroy(context: number) {
    this.raw._ui_context_destroy(context);
  }

  contextSetLogCallback(context: number, callback: UiLogCallback | null, minLevel: number) {
    const previous = this.callbacks.get("context_set_log_callback");
    let callbackIndex = 0;
    if (callback !== null) {
      callbackIndex = this.raw.addFunction(
        (p0: number, p1: number, p2: number, p3: number, p4: number) => callback(p1, p2, this.readString(p3, p4)),
        "viiiii");
    }
    const code = this.raw._ui_context_set_log_callback(context, callbackIndex, 0, minLevel);
    if (code === UiErrorCode.OK) {
      if (previous !== undefined) this.raw.removeFunction(previous);
      if (callbackIndex !== 0) this.callbacks.set("context_set_log_callback", callbackIndex);
      else this.callbacks.delete("context_set_log_callback");
    } else if (callbackIndex !== 0) {
      this.raw.removeFunction(callbackIndex);
    }
    return code;
  }

  contextSetEventCallback(context: number, callback: UiEventCallback | null) {
    const previous = this.callbacks.get("context_set_event_callback");
    let callbackIndex = 0;
    if (callback !== null) {
      callbackIndex = this.raw.addFunction(
        (p0: number, p1: number) => callback(this.readUiEvent(p1)),
        "vii");
    }
    const code = this.raw._ui_context_set_event_callback(context, callbackIndex, 0);
    if (code === UiErrorCode.OK) {
      if (previous !== undefined) this.raw.removeFunction(previous);
      if (callbackIndex !== 0) this.callbacks.set("context_set_event_callback", callbackIndex);
      else this.callbacks.delete("context_set_event_callback");
    } else if (callbackIndex !== 0) {
      this.raw.removeFunction(callbackIndex);
    }
    return code;
  }

  contextSetWakeCallback(context: number, callback: UiWakeCallback | null) {
    const previous = this.callbacks.get("context_set_wake_callback");
    let callbackIndex = 0;
    if (callback !== null) {
      callbackIndex = this.raw.addFunction(
        (p0: number) => callback(),
        "vi");
    }
    const code = this.raw._ui_context_set_wake_callback(context, callbackIndex, 0);
    if (code === UiErrorCode.OK) {
      if (previous !== undefined) this.raw.removeFunction(previous);
      if (callbackIndex !== 0) this.callbacks.set("context_set_wake_callback", callbackIndex);
      else this.callbacks.delete("context_set_wake_callback");
    } else if (callbackIndex !== 0) {
      this.raw.removeFunction(callbackIndex);
    }
    return code;
  }

  contextPollEvent(context: number) {
    const outEventPtr = this.raw._malloc(40);
    this.raw.HEAPU8.fill(0, outEventPtr, outEventPtr + 40);
    this.view().setUint32(outEventPtr, 40, true);
    const code = this.raw._ui_context_poll_event(context, outEventPtr);
    const outEventValue = code === UiErrorCode.OK ? this.readUiEvent(outEventPtr) : null;
    this.raw._free(outEventPtr);
    return { code, outEvent: outEventValue };
  }

  contextWake(context: number) {
    const code = this.raw._ui_context_wake(context);
    return code;
  }

  contextGetAllocationStats(context: number) {
    const outStatsPtr = this.raw._malloc(48);
    this.raw.HEAPU8.fill(0, outStatsPtr, outStatsPtr + 48);
    this.view().setUint32(outStatsPtr, 48, true);
    const code = this.raw._ui_context_get_allocation_stats(context, outStatsPtr);
    const outStatsValue = code === UiErrorCode.OK ? this.readUiAllocationStats(outStatsPtr) : null;
    this.raw._free(outStatsPtr);
    return { code, outStats: outStatsValue };
  }

  surfaceCreate(context: number, desc: UiSurfaceDesc) {
    const descPtr = this.writeUiSurfaceDesc(desc);
    const outSurfacePtr = this.raw._malloc(8);
    this.view().setBigUint64(outSurfacePtr, 0n, true);
    const code = this.raw._ui_surface_create(context, descPtr, outSurfacePtr);
    const outSurfaceValue = code === UiErrorCode.OK ? toHandle(this.view().getBigUint64(outSurfacePtr, true)) : null;
    if (descPtr !== 0) this.raw._free(descPtr);
    this.raw._free(outSurfacePtr);
    return { code, outSurface: outSurfaceValue };
  }

  surfaceDestroy(context: number, surface: number) {
    const code = this.raw._ui_surface_destroy(context, BigInt(surface));
    return code;
  }

  surfaceResize(context: number, surface: number, width: number, height: number, scale: number) {
    const code = this.raw._ui_surface_resize(context, BigInt(surface), width, height, scale);
    return code;
  }

  surfaceFrame(context: number, surface: number, timestampNs: number) {
    const code = this.raw._ui_surface_frame(context, BigInt(surface), BigInt(timestampNs));
    return code;
  }

  utf8ToUtf16Offset(text: string, utf8Offset: number) {
    const [textPtr, textLen] = this.writeString(text);
    const outUtf16OffsetPtr = this.raw._malloc(8);
    this.view().setBigUint64(outUtf16OffsetPtr, 0n, true);
    const code = this.raw._ui_utf8_to_utf16_offset(textPtr, textLen, utf8Offset, outUtf16OffsetPtr);
    const outUtf16OffsetValue = code === UiErrorCode.OK ? this.view().getUint32(outUtf16OffsetPtr, true) : null;
    this.raw._free(textPtr);
    this.raw._free(outUtf16OffsetPtr);
    return { code, outUtf16Offset: outUtf16OffsetValue };
  }

  utf16ToUtf8Offset(text: string, utf16Offset: number) {
    const [textPtr, textLen] = this.writeString(text);
    const outUtf8OffsetPtr = this.raw._malloc(8);
    this.view().setBigUint64(outUtf8OffsetPtr, 0n, true);
    const code = this.raw._ui_utf16_to_utf8_offset(textPtr, textLen, utf16Offset, outUtf8OffsetPtr);
    const outUtf8OffsetValue = code === UiErrorCode.OK ? this.view().getUint32(outUtf8OffsetPtr, true) : null;
    this.raw._free(textPtr);
    this.raw._free(outUtf8OffsetPtr);
    return { code, outUtf8Offset: outUtf8OffsetValue };
  }

}

export type UiLogCallback = (level: number, code: number, message: string) => void;
export type UiEventCallback = (event: UiEvent) => void;
export type UiWakeCallback = () => void;
