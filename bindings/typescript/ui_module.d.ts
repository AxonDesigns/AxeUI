// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.
//
// Types of the raw Emscripten module (ui.mjs, built with -sMODULARIZE
// -sEXPORT_ES6). 64-bit integers are BigInt (WASM_BIGINT); pointers and
// callbacks are numbers (wasm32 addresses and function-table indices).

export interface UiRawModule {
  _ui_get_version(): number;
  _ui_error_code_name(code: number): number;
  _ui_context_create(desc: number, out_context: number): number;
  _ui_context_destroy(context: number): void;
  _ui_context_set_log_callback(context: number, callback: number, user_data: number, min_level: number): number;
  _ui_context_set_event_callback(context: number, callback: number, user_data: number): number;
  _ui_context_set_wake_callback(context: number, callback: number, user_data: number): number;
  _ui_context_poll_event(context: number, out_event: number): number;
  _ui_context_wake(context: number): number;
  _ui_context_get_allocation_stats(context: number, out_stats: number): number;
  _ui_surface_create(context: number, desc: number, out_surface: number): number;
  _ui_surface_destroy(context: number, surface: bigint): number;
  _ui_surface_resize(context: number, surface: bigint, width: number, height: number, scale: number): number;
  _ui_surface_frame(context: number, surface: bigint, timestamp_ns: bigint): number;
  _ui_utf8_to_utf16_offset(text: number, text_length: number, utf8_offset: number, out_utf16_offset: number): number;
  _ui_utf16_to_utf8_offset(text: number, text_length: number, utf16_offset: number, out_utf8_offset: number): number;
  _malloc(size: number): number;
  _free(pointer: number): void;
  HEAPU8: Uint8Array;
  addFunction(fn: (...args: never[]) => unknown, signature: string): number;
  removeFunction(index: number): void;
  UTF8ToString(pointer: number, maxBytes?: number): string;
}

export type UiModuleFactory = (options?: Record<string, unknown>) => Promise<UiRawModule>;
declare const createUiModule: UiModuleFactory;
export default createUiModule;
