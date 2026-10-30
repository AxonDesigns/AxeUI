#!/usr/bin/env python3
"""Generates the public C header and language bindings from api/ui_api.json.

    tools/gen_api.py            # write generated files
    tools/gen_api.py --check    # exit 1 if any generated file is out of date (CI)

Outputs:
    include/ui/ui.h                          C11 header (the C API)
    bindings/typescript/ui_module.d.ts       types of the raw Emscripten module
    bindings/typescript/ui.ts                thin TypeScript wrapper
    bindings/dart/lib/src/ui_bindings.dart   dart:ffi bindings
    bindings/wasm/exported_functions.json    -sEXPORTED_FUNCTIONS list

The ABI rules (docs/architecture.md, "Language bindings") are enforced here:
no structs or unions by value, fixed-width integers only, enums as int32_t,
booleans as uint8_t, every struct starts with `uint32_t size`, and no interior
padding on 64-bit or wasm32 targets. Uses only the Python standard library.
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
API_FILE = ROOT / "api" / "ui_api.json"

# --- Type model ---------------------------------------------------------------

# Scalar field/parameter types: C type, (size, align), TS raw type, Dart native, Dart type.
SCALARS = {
    "uint8": ("uint8_t", 1, "number", "Uint8", "int"),
    "bool": ("uint8_t", 1, "number", "Uint8", "int"),
    "int32": ("int32_t", 4, "number", "Int32", "int"),
    "uint32": ("uint32_t", 4, "number", "Uint32", "int"),
    "int64": ("int64_t", 8, "bigint", "Int64", "int"),
    "uint64": ("uint64_t", 8, "bigint", "Uint64", "int"),
    "float32": ("float", 4, "number", "Float", "double"),
    "handle": ("ui_handle", 8, "bigint", "Uint64", "int"),
}
POINTER_KINDS = ("string_ptr", "user_data", "context", "const_context", "cstring")
ALLOWED_FIELD_TYPES = set(SCALARS) | {"string_ptr"}


class ApiError(Exception):
    pass


def split(t):
    return t.split(":", 1) if ":" in t else (t, None)


def snake_to_camel(name, upper=False):
    parts = name.split("_")
    out = parts[0] + "".join(p.capitalize() for p in parts[1:])
    return out[0].upper() + out[1:] if upper else out


def type_name(api, kind_name):
    return f"{api['prefix']}_{kind_name}"


# --- Validation ---------------------------------------------------------------


def field_layout(api, field_type, pointer_size):
    kind, arg = split(field_type)
    if kind in SCALARS:
        size = SCALARS[kind][1]
        return size, size
    if kind == "enum":
        return 4, 4
    if kind == "string_ptr":
        return pointer_size, pointer_size
    raise ApiError(f"type '{field_type}' is not allowed in a struct")


def struct_layout(api, struct, pointer_size):
    """Returns ([(field, offset)], size, align); raises on interior padding."""
    offset = 0
    align = 1
    out = []
    for field in struct["fields"]:
        size, field_align = field_layout(api, field["type"], pointer_size)
        if offset % field_align != 0:
            raise ApiError(
                f"struct {struct['name']}: implicit padding before '{field['name']}' "
                f"({pointer_size * 8}-bit); add an explicit reserved field")
        out.append((field, offset))
        offset += size
        align = max(align, field_align)
    total = (offset + align - 1) // align * align
    return out, total, align


def validate(api):
    enums = {e["name"] for e in api["enums"]}
    structs = {s["name"]: s for s in api["structs"]}
    callbacks = {c["name"] for c in api["callbacks"]}

    for enum in api["enums"]:
        for v in enum["values"]:
            if not -(2**31) <= v["value"] < 2**31:
                raise ApiError(f"enum {enum['name']}.{v['name']} does not fit in int32")

    for struct in api["structs"]:
        fields = struct["fields"]
        if not fields or fields[0]["name"] != "size" or fields[0]["type"] != "uint32":
            raise ApiError(f"struct {struct['name']} must start with 'uint32 size'")
        for f in fields:
            kind, arg = split(f["type"])
            if kind == "enum":
                if arg not in enums:
                    raise ApiError(f"struct {struct['name']}: unknown enum {arg}")
            elif kind not in ALLOWED_FIELD_TYPES:
                raise ApiError(f"struct {struct['name']}.{f['name']}: type '{f['type']}' not allowed "
                               "(fixed-width integers, float32, enums, handles, string pointers only)")
            if kind == "string_ptr" and "ownership" not in f:
                raise ApiError(f"struct {struct['name']}.{f['name']}: strings must document ownership")
        for pointer_size in (8, 4):
            struct_layout(api, struct, pointer_size)

    def check_param(owner, p):
        kind, arg = split(p["type"])
        if kind in SCALARS or kind in POINTER_KINDS:
            pass
        elif kind == "enum":
            if arg not in enums:
                raise ApiError(f"{owner}: unknown enum {arg}")
        elif kind in ("const_struct_ptr", "out_struct_ptr"):
            if arg not in structs:
                raise ApiError(f"{owner}: unknown struct {arg}")
        elif kind == "callback":
            if arg not in callbacks:
                raise ApiError(f"{owner}: unknown callback {arg}")
        elif kind == "out":
            if arg not in ("context", "handle", "uint32"):
                raise ApiError(f"{owner}: unsupported out type {arg}")
        elif kind == "struct":
            raise ApiError(f"{owner}: structs are passed by pointer, never by value")
        else:
            raise ApiError(f"{owner}: unknown type '{p['type']}'")
        if kind == "string_ptr" and "ownership" not in p:
            raise ApiError(f"{owner}.{p['name']}: strings must document ownership")

    for cb in api["callbacks"]:
        for p in cb["params"]:
            check_param(f"callback {cb['name']}", p)
    for fn in api["functions"]:
        for p in fn["params"]:
            check_param(f"function {fn['name']}", p)
        ret_kind, _ = split(fn["returns"])
        if fn["returns"] == "cstring" and "returns_ownership" not in fn:
            raise ApiError(f"function {fn['name']}: returned strings must document ownership")
        if ret_kind not in ("void", "enum", "uint32", "cstring"):
            raise ApiError(f"function {fn['name']}: unsupported return type {fn['returns']}")


# --- C header -----------------------------------------------------------------


def c_type(api, t):
    kind, arg = split(t)
    p = api["prefix"]
    if kind in SCALARS:
        return SCALARS[kind][0]
    return {
        "enum": f"{p}_{arg}",
        "string_ptr": "const char*",
        "cstring": "const char*",
        "user_data": "void*",
        "context": f"{p}_context*",
        "const_context": f"const {p}_context*",
        "const_struct_ptr": f"const {p}_{arg}*",
        "out_struct_ptr": f"{p}_{arg}*",
        "callback": f"{p}_{arg}",
        "out": {"context": f"{p}_context**", "handle": f"{p}_handle*", "uint32": "uint32_t*"}.get(arg),
        "void": "void",
    }[kind]


def c_doc(lines, indent=""):
    if not lines:
        return ""
    return "".join(f"{indent}/* {line} */\n" for line in lines)


def wrap(text, width=76):
    words = text.split()
    lines, line = [], ""
    for w in words:
        if len(line) + len(w) + 1 > width:
            lines.append(line)
            line = w
        else:
            line = f"{line} {w}" if line else w
    if line:
        lines.append(line)
    return lines


def block_comment(text, indent=""):
    lines = wrap(text, 74 - len(indent))
    if not lines:
        return ""
    if len(lines) == 1:
        return f"{indent}/* {lines[0]} */\n"
    body = "".join(f"{indent} * {l}\n" for l in lines)
    return f"{indent}/*\n{body}{indent} */\n"


def doc_block(paragraphs, details, indent=""):
    """A block comment: wrapped paragraphs, then one wrapped item per detail."""
    body = []
    for paragraph in paragraphs:
        body += wrap(paragraph, 74 - len(indent))
    if details:
        body.append("")
        for item in details:
            lines = wrap(item, 72 - len(indent))
            body.append(lines[0])
            body += ["  " + line for line in lines[1:]]
    text = "".join(f"{indent} *{(' ' + line) if line else ''}\n" for line in body)
    return f"{indent}/*\n{text}{indent} */\n"


HEADER_PREAMBLE = """\
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
"""


def generate_header(api):
    p = api["prefix"]
    P = p.upper()
    v = api["version"]
    out = [HEADER_PREAMBLE, f"""
#ifndef {P}_{P}_H
#define {P}_{P}_H

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
extern "C" {{
#endif

/* ---- Version ------------------------------------------------------------ */

#define UI_VERSION_MAJOR {v['major']}
#define UI_VERSION_MINOR {v['minor']}
#define UI_VERSION_PATCH {v['patch']}

/* Packs a version as 0x00MMmmpp (major, minor, patch; 8 bits each).
 * (`0u + x` converts to unsigned without a cast, so the macro stays
 * warning-free when used from C++ with -Wold-style-cast.) */
#define UI_MAKE_VERSION(major, minor, patch) \\
  (((0u + (major)) << 16) | ((0u + (minor)) << 8) | (0u + (patch)))

/* The version this header describes. */
#define UI_VERSION UI_MAKE_VERSION(UI_VERSION_MAJOR, UI_VERSION_MINOR, UI_VERSION_PATCH)

/* ---- Handles ------------------------------------------------------------ */

/* Opaque reference to an object inside a ui_context (< 2^53). Treat it as
 * an identifier only: do not decode or construct it. */
typedef uint64_t {p}_handle;

#define UI_NULL_HANDLE UINT64_C(0)

/* Opaque root object. */
typedef struct {p}_context {p}_context;
"""]

    out.append("\n/* ---- Enums (int32_t) ----------------------------------------------------- */\n")
    for enum in api["enums"]:
        out.append("\n")
        if "doc" in enum:
            out.append(block_comment(enum["doc"]))
        out.append(f"typedef int32_t {p}_{enum['name']};\nenum {{\n")
        for value in enum["values"]:
            doc = f"  /* {value['doc']} */" if "doc" in value else ""
            out.append(f"  {P}_{value['name']} = {value['value']},{doc}\n")
        out.append("};\n")

    out.append("\n/* ---- Structs ------------------------------------------------------------ */\n")
    for struct in api["structs"]:
        out.append("\n")
        direction = "Input" if struct["direction"] == "in" else "Output"
        out.append(block_comment(f"{direction}. {struct.get('doc', '')}"))
        out.append(f"typedef struct {p}_{struct['name']} {{\n")
        for field in struct["fields"]:
            doc = field.get("doc", "")
            if "ownership" in field:
                doc = f"{doc} Ownership: {field['ownership']}".strip()
            out.append(f"  {c_type(api, field['type'])} {field['name']};\n".replace("* ", "* ")
                       if not doc else block_comment(doc, "  ") +
                       f"  {c_type(api, field['type'])} {field['name']};\n")
        out.append(f"}} {p}_{struct['name']};\n")

    out.append("\n/* ---- Callbacks ---------------------------------------------------------- */\n")
    for cb in api["callbacks"]:
        out.append("\n")
        doc = cb.get("doc", "")
        for param in cb["params"]:
            if "ownership" in param:
                doc += f" `{param['name']}`: {param.get('doc', '')} Ownership: {param['ownership']}"
        out.append(block_comment(doc))
        params = ", ".join(f"{c_type(api, x['type'])} {x['name']}" for x in cb["params"])
        out.append(f"typedef void (*{p}_{cb['name']})({params});\n")

    out.append("\n/* ---- Functions ---------------------------------------------------------- */\n")
    for fn in api["functions"]:
        out.append("\n")
        paragraphs = [fn.get("doc", "")]
        details = ["Thread: any." if fn.get("thread") == "any" else "Thread: the context's owner."]
        for param in fn["params"]:
            notes = []
            if param.get("doc"):
                notes.append(param["doc"].rstrip("."))
            if param.get("nullable"):
                notes.append("may be NULL")
            if "ownership" in param:
                notes.append(f"ownership: {param['ownership'].rstrip('.')}")
            if notes:
                details.append(f"{param['name']}: {'; '.join(notes)}.")
        if "returns_ownership" in fn:
            details.append(f"Returns: {fn['returns_ownership']}")
        out.append(doc_block(paragraphs, details))
        params = ", ".join(f"{c_type(api, x['type'])} {x['name']}" for x in fn["params"]) or "void"
        out.append(f"UI_API {c_type(api, fn['returns'])} {p}_{fn['name']}({params});\n")

    out.append(f"""
#ifdef __cplusplus
}} /* extern "C" */
#endif

#endif /* {P}_{P}_H */
""")
    return "".join(out)


# --- TypeScript ---------------------------------------------------------------


def ts_raw_type(t):
    kind, _ = split(t)
    if kind in SCALARS:
        return SCALARS[kind][2]
    if kind == "enum":
        return "number"
    if kind == "void":
        return "void"
    return "number"  # Pointers and function-table indices on wasm32.


def wasm_sig_char(t):
    kind, _ = split(t)
    if kind in SCALARS:
        return {"int64": "j", "uint64": "j", "handle": "j", "float32": "f"}.get(kind, "i")
    if kind == "void":
        return "v"
    return "i"


def generate_ts_module_types(api):
    p = api["prefix"]
    lines = [
        "// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.",
        "//",
        "// Types of the raw Emscripten module (ui.mjs, built with -sMODULARIZE",
        "// -sEXPORT_ES6). 64-bit integers are BigInt (WASM_BIGINT); pointers and",
        "// callbacks are numbers (wasm32 addresses and function-table indices).",
        "",
        "export interface UiRawModule {",
    ]
    for fn in api["functions"]:
        params = ", ".join(f"{x['name']}: {ts_raw_type(x['type'])}" for x in fn["params"])
        lines.append(f"  _{p}_{fn['name']}({params}): {ts_raw_type(fn['returns'])};")
    lines += [
        "  _malloc(size: number): number;",
        "  _free(pointer: number): void;",
        "  HEAPU8: Uint8Array;",
        "  addFunction(fn: (...args: never[]) => unknown, signature: string): number;",
        "  removeFunction(index: number): void;",
        "  UTF8ToString(pointer: number, maxBytes?: number): string;",
        "}",
        "",
        "export type UiModuleFactory = (options?: Record<string, unknown>) => Promise<UiRawModule>;",
        "declare const createUiModule: UiModuleFactory;",
        "export default createUiModule;",
        "",
    ]
    return "\n".join(lines)


def ts_name(name):
    return snake_to_camel("ui_" + name, True)


def generate_ts_wrapper(api):
    p = api["prefix"]
    structs = {s["name"]: s for s in api["structs"]}
    callbacks = {c["name"]: c for c in api["callbacks"]}
    out = [
        "// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.",
        "//",
        "// Thin TypeScript wrapper over the wasm module: converts handles between",
        "// BigInt and number (handles are < 2^53, so they are exact numbers),",
        "// strings between JS and UTF-8, and marshals structs, out-parameters and",
        "// callbacks. Error codes are returned, not thrown.",
        "",
        'import type { UiRawModule } from "./ui_module.d.ts";',
        "",
        f"export const UI_VERSION = {(api['version']['major'] << 16) | (api['version']['minor'] << 8) | api['version']['patch']};",
        "export const UI_NULL_HANDLE = 0;",
        "",
        "export type Handle = number;",
        "export type ContextPtr = number;",
        "",
    ]
    for enum in api["enums"]:
        out.append(f"export const {ts_name(enum['name'])} = {{")
        for value in enum["values"]:
            out.append(f"  {value['name']}: {value['value']},")
        out.append("} as const;")
        out.append("")

    # Struct interfaces and (wasm32) layouts.
    for struct in api["structs"]:
        layout, size, _ = struct_layout(api, struct, 4)
        iface = ts_name(struct["name"])
        out.append(f"export interface {iface} {{")
        for field, _ in layout:
            if field["name"] in ("size", "reserved") or field["name"].endswith("_length"):
                continue
            kind, _ = split(field["type"])
            ts = "string | null" if kind == "string_ptr" else "number"
            out.append(f"  {snake_to_camel(field['name'])}: {ts};")
        out.append("}")
        out.append(f"const {struct['name'].upper()}_SIZE = {size};")
        out.append("")

    out += [
        "const MAX_SAFE_HANDLE = 2n ** 53n;",
        "",
        "function toHandle(value: bigint): Handle {",
        "  if (value < 0n || value >= MAX_SAFE_HANDLE) {",
        '    throw new RangeError(`handle ${value} does not fit in 53 bits`);',
        "  }",
        "  return Number(value);",
        "}",
        "",
        "export class Ui {",
        "  private readonly callbacks = new Map<string, number>();",
        "  private readonly encoder = new TextEncoder();",
        "  private readonly decoder = new TextDecoder();",
        "",
        "  readonly raw: UiRawModule;",
        "",
        "  constructor(raw: UiRawModule) {",
        "    this.raw = raw;",
        "  }",
        "",
        "  private view(): DataView {",
        "    return new DataView(this.raw.HEAPU8.buffer);",
        "  }",
        "",
        "  private readString(pointer: number, length: number): string {",
        "    return this.decoder.decode(this.raw.HEAPU8.subarray(pointer, pointer + length));",
        "  }",
        "",
        "  private writeString(text: string): [number, number] {",
        "    const bytes = this.encoder.encode(text);",
        "    const pointer = this.raw._malloc(Math.max(bytes.length, 1));",
        "    this.raw.HEAPU8.set(bytes, pointer);",
        "    return [pointer, bytes.length];",
        "  }",
        "",
    ]

    # Struct readers/writers.
    for struct in api["structs"]:
        layout, size, _ = struct_layout(api, struct, 4)
        iface = ts_name(struct["name"])
        name = ts_name(struct["name"])
        length_fields = {f["name"] for f, _ in layout if f["name"].endswith("_length")}
        if struct["direction"] == "out" or any(struct["name"] == split(x["type"])[1]
                                               for c in api["callbacks"] for x in c["params"]):
            out.append(f"  private read{name}(pointer: number): {iface} {{")
            out.append("    const v = this.view();")
            out.append("    return {")
            for field, offset in layout:
                fname = field["name"]
                if fname in ("size", "reserved") or fname in length_fields:
                    continue
                kind, _ = split(field["type"])
                key = snake_to_camel(fname)
                if kind == "string_ptr":
                    len_offset = next(o for f, o in layout if f["name"] == f"{fname}_length")
                    out.append(f"      {key}: v.getUint32(pointer + {offset}, true) === 0 ? null : "
                               f"this.readString(v.getUint32(pointer + {offset}, true), "
                               f"v.getUint32(pointer + {len_offset}, true)),")
                elif kind in ("uint64", "handle"):
                    conv = "toHandle" if kind == "handle" else "Number"
                    out.append(f"      {key}: {conv}(v.getBigUint64(pointer + {offset}, true)),")
                elif kind == "int64":
                    out.append(f"      {key}: Number(v.getBigInt64(pointer + {offset}, true)),")
                elif kind == "float32":
                    out.append(f"      {key}: v.getFloat32(pointer + {offset}, true),")
                elif kind in ("int32", "enum"):
                    out.append(f"      {key}: v.getInt32(pointer + {offset}, true),")
                elif kind in ("uint8", "bool"):
                    out.append(f"      {key}: v.getUint8(pointer + {offset}),")
                else:
                    out.append(f"      {key}: v.getUint32(pointer + {offset}, true),")
            out.append("    };")
            out.append("  }")
            out.append("")
        if struct["direction"] == "in":
            out.append(f"  private write{name}(value: {iface}): number {{")
            out.append(f"    const pointer = this.raw._malloc({size});")
            out.append("    const v = this.view();")
            for field, offset in layout:
                fname = field["name"]
                kind, _ = split(field["type"])
                key = snake_to_camel(fname)
                if fname == "size":
                    out.append(f"    v.setUint32(pointer + {offset}, {size}, true);")
                elif fname == "reserved":
                    out.append(f"    v.setUint32(pointer + {offset}, 0, true);")
                elif kind == "float32":
                    out.append(f"    v.setFloat32(pointer + {offset}, value.{key}, true);")
                elif kind in ("uint64", "handle", "int64"):
                    out.append(f"    v.setBigUint64(pointer + {offset}, BigInt(value.{key}), true);")
                elif kind in ("int32", "enum"):
                    out.append(f"    v.setInt32(pointer + {offset}, value.{key}, true);")
                else:
                    out.append(f"    v.setUint32(pointer + {offset}, value.{key}, true);")
            out.append("    return pointer;")
            out.append("  }")
            out.append("")

    # Functions.
    for fn in api["functions"]:
        method = snake_to_camel(fn["name"])
        params = fn["params"]
        sig_params, call_args, prologue, epilogue, outs = [], [], [], [], []
        skip = set()
        for i, param in enumerate(params):
            name = param["name"]
            kind, arg = split(param["type"])
            key = snake_to_camel(name)
            if name in skip:
                continue
            if kind == "string_ptr":
                length = params[i + 1]["name"]
                skip.add(length)
                sig_params.append(f"{key}: string")
                prologue.append(f"    const [{key}Ptr, {key}Len] = this.writeString({key});")
                epilogue.append(f"    this.raw._free({key}Ptr);")
                call_args += [f"{key}Ptr", f"{key}Len"]
            elif kind == "user_data":
                call_args.append("0")
            elif kind == "callback":
                cb = callbacks[arg]
                cb_type = ts_name(arg)
                sig_params.append(f"{key}: {cb_type} | null")
                sig = "v" + "".join(wasm_sig_char(x["type"]) for x in cb["params"])
                # The adapter receives raw wasm arguments p0..pN and converts
                # them (strings, structs) before calling the user's function.
                adapter_params = [f"p{j}: number" for j in range(len(cb["params"]))]
                adapter_args = []
                cb_skip = set()
                for j, cp in enumerate(cb["params"]):
                    ck, carg = split(cp["type"])
                    if cp["name"] in cb_skip or ck == "user_data":
                        continue
                    if ck == "string_ptr":
                        cb_skip.add(cb["params"][j + 1]["name"])
                        adapter_args.append(f"this.readString(p{j}, p{j + 1})")
                    elif ck == "const_struct_ptr":
                        adapter_args.append(f"this.read{ts_name(carg)}(p{j})")
                    else:
                        adapter_args.append(f"p{j}")
                prologue += [
                    f"    const previous = this.callbacks.get(\"{fn['name']}\");",
                    f"    let {key}Index = 0;",
                    f"    if ({key} !== null) {{",
                    f"      {key}Index = this.raw.addFunction(",
                    f"        ({', '.join(adapter_params)}) => {key}({', '.join(adapter_args)}),",
                    f"        \"{sig}\");",
                    "    }",
                ]
                epilogue += [
                    f"    if (code === UiErrorCode.OK) {{",
                    f"      if (previous !== undefined) this.raw.removeFunction(previous);",
                    f"      if ({key}Index !== 0) this.callbacks.set(\"{fn['name']}\", {key}Index);",
                    f"      else this.callbacks.delete(\"{fn['name']}\");",
                    f"    }} else if ({key}Index !== 0) {{",
                    f"      this.raw.removeFunction({key}Index);",
                    "    }",
                ]
                call_args.append(f"{key}Index")
            elif kind == "const_struct_ptr":
                iface = ts_name(arg)
                nullable = param.get("nullable")
                sig_params.append(f"{key}: {iface}{' | null' if nullable else ''}")
                expr = f"{key} === null ? 0 : this.write{iface}({key})" if nullable else f"this.write{iface}({key})"
                prologue.append(f"    const {key}Ptr = {expr};")
                epilogue.append(f"    if ({key}Ptr !== 0) this.raw._free({key}Ptr);")
                call_args.append(f"{key}Ptr")
            elif kind == "out_struct_ptr":
                size = struct_layout(api, structs[arg], 4)[1]
                prologue += [
                    f"    const {key}Ptr = this.raw._malloc({size});",
                    f"    this.raw.HEAPU8.fill(0, {key}Ptr, {key}Ptr + {size});",
                    f"    this.view().setUint32({key}Ptr, {size}, true);",
                ]
                outs.append((key, f"code === UiErrorCode.OK ? this.read{ts_name(arg)}({key}Ptr) : null"))
                epilogue.append(f"    this.raw._free({key}Ptr);")
                call_args.append(f"{key}Ptr")
            elif kind == "out":
                prologue += [f"    const {key}Ptr = this.raw._malloc(8);",
                             f"    this.view().setBigUint64({key}Ptr, 0n, true);"]
                reader = {
                    "context": f"this.view().getUint32({key}Ptr, true)",
                    "handle": f"toHandle(this.view().getBigUint64({key}Ptr, true))",
                    "uint32": f"this.view().getUint32({key}Ptr, true)",
                }[arg]
                outs.append((key, f"code === UiErrorCode.OK ? {reader} : null"))
                epilogue.append(f"    this.raw._free({key}Ptr);")
                call_args.append(f"{key}Ptr")
            elif kind in ("handle", "uint64", "int64"):
                sig_params.append(f"{key}: number")
                call_args.append(f"BigInt({key})")
            else:
                sig_params.append(f"{key}: number")
                call_args.append(key)

        ret_kind, _ = split(fn["returns"])
        call = f"this.raw._{p}_{fn['name']}({', '.join(call_args)})"
        out.append(f"  {method}({', '.join(sig_params)}) {{")
        out += prologue
        if ret_kind == "void":
            out.append(f"    {call};")
            out += epilogue
            out.append("  }")
        elif ret_kind == "cstring":
            out.append(f"    return this.raw.UTF8ToString({call});")
            out.append("  }")
        else:
            out.append(f"    const code = {call};")
            # Read outs before freeing their memory.
            for key, expr in outs:
                out.append(f"    const {key}Value = {expr};")
            out += epilogue
            if outs:
                fields = ", ".join(f"{key}: {key}Value" for key, _ in outs)
                out.append(f"    return {{ code, {fields} }};")
            else:
                out.append("    return code;")
            out.append("  }")
        out.append("")

    out.append("}")
    out.append("")
    # Callback types.
    for cb in api["callbacks"]:
        args = []
        skip = set()
        for j, cp in enumerate(cb["params"]):
            ck, carg = split(cp["type"])
            if cp["name"] in skip or ck == "user_data":
                continue
            if ck == "string_ptr":
                skip.add(cb["params"][j + 1]["name"])
                args.append(f"{snake_to_camel(cp['name'])}: string")
            elif ck == "const_struct_ptr":
                args.append(f"{snake_to_camel(cp['name'])}: {ts_name(carg)}")
            else:
                args.append(f"{snake_to_camel(cp['name'])}: number")
        out.append(f"export type {ts_name(cb['name'])} = ({', '.join(args)}) => void;")
    out.append("")
    return "\n".join(out)


# --- Dart ---------------------------------------------------------------------


def dart_native(api, t):
    kind, arg = split(t)
    if kind in SCALARS:
        return SCALARS[kind][3]
    return {
        "enum": "Int32",
        "string_ptr": "Pointer<Char>",
        "cstring": "Pointer<Char>",
        "user_data": "Pointer<Void>",
        "context": "Pointer<UiContext>",
        "const_context": "Pointer<UiContext>",
        "const_struct_ptr": f"Pointer<{snake_to_camel('ui_' + (arg or ''), True)}>",
        "out_struct_ptr": f"Pointer<{snake_to_camel('ui_' + (arg or ''), True)}>",
        "callback": f"Pointer<NativeFunction<{snake_to_camel('ui_' + (arg or ''), True)}Native>>",
        "out": {"context": "Pointer<Pointer<UiContext>>", "handle": "Pointer<Uint64>",
                "uint32": "Pointer<Uint32>"}.get(arg),
        "void": "Void",
    }[kind]


def dart_type(api, t):
    kind, _ = split(t)
    if kind in SCALARS:
        return SCALARS[kind][4]
    if kind == "enum":
        return "int"
    if kind == "void":
        return "void"
    return dart_native(api, t)


def generate_dart(api):
    p = api["prefix"]
    out = [
        "// GENERATED from api/ui_api.json by tools/gen_api.py. Do not edit.",
        "//",
        "// dart:ffi bindings for the AxeUI C API. Handles are Dart ints (< 2^53,",
        "// so they are exact on the web too).",
        "",
        "// ignore_for_file: constant_identifier_names, non_constant_identifier_names",
        "",
        "import 'dart:ffi';",
        "",
        f"const int UI_VERSION = {(api['version']['major'] << 16) | (api['version']['minor'] << 8) | api['version']['patch']};",
        "const int UI_NULL_HANDLE = 0;",
        "",
    ]
    for enum in api["enums"]:
        for value in enum["values"]:
            out.append(f"const int UI_{value['name']} = {value['value']};")
        out.append("")
    out.append("final class UiContext extends Opaque {}")
    out.append("")
    for struct in api["structs"]:
        out.append(f"final class {snake_to_camel('ui_' + struct['name'], True)} extends Struct {{")
        for field in struct["fields"]:
            kind, _ = split(field["type"])
            native = dart_native(api, field["type"])
            if kind == "string_ptr":
                out.append(f"  external {native} {snake_to_camel(field['name'])};")
            else:
                out.append(f"  @{native}()")
                out.append(f"  external {dart_type(api, field['type'])} {snake_to_camel(field['name'])};")
        out.append("}")
        out.append("")
    for cb in api["callbacks"]:
        name = snake_to_camel("ui_" + cb["name"], True)
        native_params = ", ".join(dart_native(api, x["type"]) for x in cb["params"])
        dart_params = ", ".join(dart_type(api, x["type"]) for x in cb["params"])
        out.append(f"typedef {name}Native = Void Function({native_params});")
        out.append(f"typedef {name}Dart = void Function({dart_params});")
    out.append("")
    out.append("class UiBindings {")
    out.append("  UiBindings(DynamicLibrary library)")
    inits = []
    for fn in api["functions"]:
        native = f"{dart_native(api, fn['returns'])} Function({', '.join(dart_native(api, x['type']) for x in fn['params'])})"
        dart = f"{dart_type(api, fn['returns'])} Function({', '.join(dart_type(api, x['type']) for x in fn['params'])})"
        inits.append(f"        {snake_to_camel(fn['name'])} = library.lookupFunction<{native}, {dart}>('{p}_{fn['name']}')")
    out.append("      : " + ",\n".join(inits).lstrip() + ";")
    out.append("")
    for fn in api["functions"]:
        dart = f"{dart_type(api, fn['returns'])} Function({', '.join(dart_type(api, x['type']) for x in fn['params'])})"
        doc = " ".join(wrap(fn.get("doc", ""), 1000))
        out.append(f"  /// {doc}")
        out.append(f"  final {dart} {snake_to_camel(fn['name'])};")
    out.append("}")
    out.append("")
    return "\n".join(out)


def generate_exports(api):
    names = [f"_{api['prefix']}_{fn['name']}" for fn in api["functions"]] + ["_malloc", "_free"]
    return json.dumps(names, indent=2) + "\n"


# --- Driver -------------------------------------------------------------------


def main(argv):
    check = "--check" in argv
    api = json.loads(API_FILE.read_text(encoding="utf-8"))
    try:
        validate(api)
    except ApiError as error:
        print(f"api/ui_api.json violates the ABI rules: {error}", file=sys.stderr)
        return 1
    outputs = {
        ROOT / "include" / "ui" / "ui.h": generate_header(api),
        ROOT / "bindings" / "typescript" / "ui_module.d.ts": generate_ts_module_types(api),
        ROOT / "bindings" / "typescript" / "ui.ts": generate_ts_wrapper(api),
        ROOT / "bindings" / "dart" / "lib" / "src" / "ui_bindings.dart": generate_dart(api),
        ROOT / "bindings" / "wasm" / "exported_functions.json": generate_exports(api),
    }
    stale = []
    for path, content in outputs.items():
        current = path.read_text(encoding="utf-8") if path.exists() else None
        if current == content:
            continue
        if check:
            stale.append(path.relative_to(ROOT))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
            print(f"wrote {path.relative_to(ROOT)}")
    if stale:
        print("generated files are out of date (run tools/gen_api.py):", file=sys.stderr)
        for path in stale:
            print(f"  {path}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
