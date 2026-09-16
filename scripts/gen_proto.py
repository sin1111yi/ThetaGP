#!/usr/bin/env python3
"""
ThetaGP Protocol Code Generator

Reads protocol/protocol.toml and generates type-safe serialization code:
  - C++   header (protocol/proto.h)  — device side, ArduinoJson v7
  - Rust  module (protocol/proto.rs) — Tauri backend, serde
  - TS    types   (protocol/types.ts) — frontend
  - JSON  manifest (protocol/proto_fields.json) — ordered request/response
          field lists per command, for consumers that must not hand-copy the
          protocol shape: the CDC test suite reads it, the docs table can too
  - C++   header (protocol/proto_resp.h) — response payload field tables in
          declaration order, as X-macros, for the firmware that writes a
          response: the order, the JSON keys and the printf conversions come
          from here instead of from a format string written by hand

Usage:
  python3 scripts/gen_proto.py                       # all targets
  python3 scripts/gen_proto.py --target cpp           # C++ only
  python3 scripts/gen_proto.py --target rust           # Rust only
  python3 scripts/gen_proto.py --target ts             # TS only
  python3 scripts/gen_proto.py --target fields         # field manifest only
  python3 scripts/gen_proto.py --target resp           # response tables only
  python3 scripts/gen_proto.py --dry-run               # print to stdout
  python3 scripts/gen_proto.py --protocol custom.toml  # custom path

Requires: Python 3.11+ (uses stdlib tomllib)
"""

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple

# ── Try tomllib (3.11+), fallback to tomli ──────────────────────────────────
try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        print("ERROR: Python 3.11+ (stdlib tomllib) or 'tomli' pip package required.",
              file=sys.stderr)
        sys.exit(1)


# ═════════════════════════════════════════════════════════════════════════════
# Type helpers
# ═════════════════════════════════════════════════════════════════════════════

CPP_TYPE_MAP = {
    "u8":     "uint8_t",
    "u16":    "uint16_t",
    "u32":    "uint32_t",
    "i32":    "int32_t",
    "bool":   "bool",
    "string": "const char *",
    "any":    "JsonVariant",
}

RUST_TYPE_MAP = {
    "u8":     "u8",
    "u16":    "u16",
    "u32":    "u32",
    "i32":    "i32",
    "bool":   "bool",
    "string": "String",
    "any":    "serde_json::Value",
}

TS_TYPE_MAP = {
    "u8":     "number",
    "u16":    "number",
    "u32":    "number",
    "i32":    "number",
    "bool":   "boolean",
    "string": "string",
    "any":    "any",
}

# The three tables above are the whole vocabulary a field's `type` may draw on,
# and validate_types() holds the TOML to it: a type named in protocol.toml that
# no table maps is not an error any generator reports — each one falls back to
# its escape hatch (JsonVariant / serde_json::Value / any) and emits a field
# that deserializes as an untyped blob while protocol.toml still reads as if it
# were declared. That is a silent loss of the very typing this generator exists
# to provide, so it stops generation instead.
# Named, because validate_types() reports which of them is short a type.
TYPE_MAPS = (("CPP_TYPE_MAP", CPP_TYPE_MAP),
             ("RUST_TYPE_MAP", RUST_TYPE_MAP),
             ("TS_TYPE_MAP", TS_TYPE_MAP))

# A response field's type → (the printf argument type, the conversion for it).
# Decided once per type, here, so a field's specifier can never drift from the
# type protocol.toml declares it with: the type is read from the source and the
# pair is derived from it, both when the response table is generated and when
# the firmware expands that table. A type with no pair is a field no table can
# carry (its value has no printf form); the response table generator reports it.
PRINTF_TYPE_MAP = {
    "u8":     ("uint32_t", "%u"),
    "u16":    ("uint32_t", "%u"),
    "u32":    ("uint32_t", "%u"),
    "i32":    ("int32_t", "%d"),
    "bool":   ("int", "%B"),
    "string": ("const char *", "%Q"),
}

# A field's `role` is a property that outlives its type and its position: who
# else has to know about that field.
#   accounting    — a member of the per-task accounting subset the CDC test
#                   suite requires in every build
#   task_counters — reported only in a build that compiles the task counters in
#                   (USE_TASK_COUNTERS), so a response omits the field without
#                   them
ROLE_ACCOUNTING = "accounting"
ROLE_TASK_COUNTERS = "task_counters"

# The roles that make a response field conditional, and what the condition is:
# the name of the flag the generated response table entry carries, and the
# firmware macro that flag is defined from. A role absent here is always
# present, and its table entry carries the constant 1.
ROLE_PRESENCE = {
    ROLE_TASK_COUNTERS: ("THETAGP_RESP_HAS_TASK_COUNTERS", "USE_TASK_COUNTERS"),
}


def to_pascal(name: str) -> str:
    """snake_case or SCREAMING_SNAKE → PascalCase"""
    return "".join(word.capitalize() for word in name.replace("-", "_").split("_"))


def to_snake(name: str) -> str:
    """PascalCase → snake_case (handles acronyms, does not modify SCREAMING_SNAKE)"""
    # If already snake_case (has underscores), return as-is
    if "_" in name:
        return name.lower()
    # Insert underscore before each uppercase letter that is:
    # - preceded by a lowercase letter (camelCase boundary)
    # - preceded by an uppercase and followed by lowercase (acronym tail)
    result = re.sub(r"(?<=[a-z])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", "_", name)
    return result.lower()


def sanitize_cpp_comment(text: str) -> str:
    """Sanitize text for use in C++ // or /* */ comments."""
    return text.replace("*/", "* /").replace("\n", " ")


RUST_KEYWORDS = {
    "as", "break", "const", "continue", "crate", "else", "enum", "extern",
    "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod",
    "move", "mut", "pub", "ref", "return", "self", "Self", "static", "struct",
    "super", "trait", "true", "type", "union", "unsafe", "use", "where", "while",
    "abstract", "become", "box", "do", "final", "macro", "override", "priv",
    "try", "typeof", "unsized", "virtual", "yield",
}


def rust_ident(name: str) -> str:
    """Return a valid Rust identifier, escaping keywords with r#."""
    if name in RUST_KEYWORDS:
        return f"r#{name}"
    return name


# ═════════════════════════════════════════════════════════════════════════════
# TOML Loading
# ═════════════════════════════════════════════════════════════════════════════

def load_protocol(path: str) -> dict:
    with open(path, "rb") as f:
        return tomllib.load(f)


def file_sha256(path: Path) -> str:
    """SHA-256 of a file's bytes — the digest a generated artifact records.

    Used to fingerprint the TOML a generated artifact was derived from, so a
    consumer can tell an artifact that matches its source from one left behind
    by a later edit to that source.
    """
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# The generator's own sources: every file whose bytes take part in deriving the
# artifacts this script writes. Their digest is recorded in the field manifest
# beside its source's, because the manifest's shape comes from the emitter as
# much as from the TOML: an emitter that starts filtering fields, renaming keys
# or changing an order writes a different manifest from the same input, and a
# consumer holding only the source digest would keep checking the protocol
# against the older shape and pass.
#
# The list *is* the coverage the digest is computed over, and the manifest
# carries it verbatim so a consumer recomputes the same digest without a second
# copy of the list here. It starts with the entry point: splitting this file
# into modules extends the list rather than replacing the entry, and the entry
# point's own bytes change when it starts importing the new module, so a
# manifest a pre-split generator left behind fails the check on the entry point
# alone — before the added file's coverage is even reached.
REPO_ROOT = Path(__file__).resolve().parents[1]
GENERATOR_SOURCES = ("scripts/gen_proto.py",)


def generator_sha256(sources: Tuple[str, ...] = GENERATOR_SOURCES,
                     root: Path = REPO_ROOT) -> str:
    """One SHA-256 over the generator's own sources, as the manifest records it.

    Each file contributes its path relative to the repository root and its
    bytes, NUL-separated, in the order named. The path is part of the digest, so
    the same bytes under another name (a moved or renamed emitter) do not read
    as the same generator, and a file has to be named to be covered rather than
    being covered by having been found.
    """
    digest = hashlib.sha256()
    for name in sources:
        digest.update(name.encode("utf-8"))
        digest.update(b"\0")
        digest.update((root / name).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


# Domains carrying no [[commands]] entry (registered by the firmware dispatcher).
NON_COMMAND_DOMAINS = {"test", "profile"}


def validate_domains(proto: dict) -> None:
    """Abort unless [domains] covers every [[commands]].domain and describes no unknown domain."""
    declared, used = set(proto.get("domains", {})), {c.get("domain") for c in proto.get("commands", [])}
    miss, unk = sorted(used - declared), sorted(declared - used - NON_COMMAND_DOMAINS)
    absent = sorted(NON_COMMAND_DOMAINS - declared)
    if miss or unk or absent:
        print(f"ERROR: [domains] mismatch — used-by-commands-but-undeclared: {miss or 'none'}; declared-but-unknown: {unk or 'none'}; registered-non-command-but-undeclared: {absent or 'none'}", file=sys.stderr)
        sys.exit(1)


def validate_types(proto: dict) -> None:
    """Abort unless every `type` protocol.toml uses is mapped by all three tables.

    The counterparts of validate_domains() and validate_field_roles(), for the
    one name in the TOML the generators otherwise only read: a type is looked up
    with a fallback, so one that is unnamed in a table does not fail — it
    degrades to the untyped escape hatch of that target and the build keeps
    going. A misspelling (`u64` for `u32`) or a type nobody added yet therefore
    reaches the generated artifacts as a field of no static type, which is
    indistinguishable from a deliberate `any`. Reported per table, because a
    name can be added to one of them and missed in another, and exit 1 like the
    other validators: the input is wrong, so no artifact should be written from
    it.
    """
    used = sorted(
        {f["type"] for t in proto.get("types", []) for f in t.get("fields", [])}
        | {f["type"] for cmd in proto.get("commands", [])
           for side in ("request", "response") for f in cmd.get(side, [])}
    )
    unmapped = [(name, [t for t in used if t not in table])
                for name, table in TYPE_MAPS]
    if any(missing for _, missing in unmapped):
        for name, missing in unmapped:
            if missing:
                print(f"ERROR: {name} has no mapping for type(s): {missing}",
                      file=sys.stderr)
        print(f"       Types used by protocol.toml: {used}", file=sys.stderr)
        print(f"       Mapped types: {sorted(set().union(*(set(t) for _, t in TYPE_MAPS)))}",
              file=sys.stderr)
        print("       A type a table does not map is emitted as that target's "
              "untyped value (JsonVariant / serde_json::Value / any) instead of "
              "the declared one; add the mapping or fix the type name.",
              file=sys.stderr)
        sys.exit(1)


def validate_field_roles(proto: dict) -> None:
    """Abort unless every field `role` is one this generator acts on.

    A role is a name this file and the artifacts it writes agree on, and one it
    does not know is a name nothing derives anything from: the field would read
    as marked while the suite's expectation and the response table of its
    command both stayed as if it were not, so a misspelling has to stop
    generation rather than leave a marking that does nothing.
    """
    known = {ROLE_ACCOUNTING} | set(ROLE_PRESENCE)
    unknown = sorted({
        f["role"]
        for cmd in proto.get("commands", [])
        for side in ("request", "response")
        for f in cmd.get(side, [])
        if "role" in f and f["role"] not in known
    })
    if unknown:
        print(f"ERROR: unknown field role(s): {unknown} — known roles: {sorted(known)}", file=sys.stderr)
        sys.exit(1)


# ═════════════════════════════════════════════════════════════════════════════
# C++ Generator
# ═════════════════════════════════════════════════════════════════════════════

def gen_cpp(proto: dict, out: Optional[Path] = None) -> str:
    lines: List[str] = []
    types = proto.get("types", [])
    enums = proto.get("enums", [])
    commands = proto.get("commands", [])
    error_codes = proto.get("error_codes", {})

    # Build type name → fields lookup for nested serialization
    type_fields: Dict[str, list] = {t["name"]: t["fields"] for t in types}
    type_namespaces: Dict[str, str] = {t["name"]: t.get("namespace", "") for t in types}

    def w(line: str = "") -> None:
        lines.append(line)

    # ── Header ──────────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w("// Source: protocol/protocol.toml")
    w(f"// Generated: {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w("#pragma once")
    w('#include "utils/json/json.h"')
    w("#include <cstdint>")
    w("#include <cstring>")


    # Add includes for codebase-owned types
    for t in types:
        if not t.get("codebase_owned", False):
            continue
        ns = t.get("namespace", "")
        name = t["name"]
        if ns == "ThetaGP::Gamepad" and name == "GamepadRawInput":
            w("#include \"gamepad/gamepadstate.h\"")
        elif ns == "ThetaGP" and name == "HIDReport":
            w("#include \"drivers/gpdriver/hid/HIDDescriptors.h\"")

    w()

    # ── Error codes ──────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Error Codes")
    w("// =============================================================================")
    class ProtoErrorCodeTag: pass  # marker for the class context

    # ── Enums ────────────────────────────────────────────────────────────────
    for enum_info in enums:
        name = enum_info["name"]
        w(f"// ---------------------------------------------------------------------------")
        w(f"// {name} — {sanitize_cpp_comment(enum_info.get('description', ''))}")
        w(f"// ---------------------------------------------------------------------------")
        w(f"enum class {name} : uint8_t {{")
        for val in enum_info["values"]:
            desc = sanitize_cpp_comment(val.get("description", ""))
            w(f"    {val['name']} = {val['value']}, // {desc}")
        w("};")
        w()
        w(f"inline const char *{name}Name({name} v) {{")
        w(f"    switch (v) {{")
        for val in enum_info["values"]:
            w(f"        case {name}::{val['name']}: return \"{val['name']}\";")
        w(f"        default: return \"UNKNOWN\";")
        w(f"    }}")
        w(f"}}")
        w()

    # ── Enum converters ──────────────────────────────────────────────────────

    # ── Struct definitions ───────────────────────────────────────────────────
    for t in types:
        if t.get("codebase_owned", False):
            continue  # skip struct def; type exists in codebase
        name = t["name"]
        ns = t.get("namespace", "")
        desc = sanitize_cpp_comment(t.get("description", ""))
        w(f"// ---------------------------------------------------------------------------")
        w(f"// {desc}")
        w(f"// ---------------------------------------------------------------------------")
        # Open namespace
        if ns:
            ns_parts = ns.split("::")
            for part in ns_parts:
                w(f"namespace {part} {{")
        w(f"struct {name} {{")
        for f in t["fields"]:
            if f.get("omit_in_serialize"):
                # Still emit the field (used internally) but mark it
                pass
            cpp_type = CPP_TYPE_MAP.get(f["type"], f"/* unknown: {f['type']} */")
            desc = sanitize_cpp_comment(f.get("description", ""))
            default = ""
            if f["type"] in ("u8", "u16", "u32"):
                default_val = f.get("default", 0)
                if ns == "ThetaGP::Gamepad" and name == "GamepadRawInput":
                    mid_defaults = {"lx": "GAMEPAD_JOYSTICK_MID", "ly": "GAMEPAD_JOYSTICK_MID",
                                    "rx": "GAMEPAD_JOYSTICK_MID", "ry": "GAMEPAD_JOYSTICK_MID"}
                    if f["name"] in mid_defaults:
                        default = f"{{{mid_defaults[f['name']]}}}"
                    else:
                        default = f"{{{default_val}}}"
                else:
                    default = f"{{{default_val}}}"
            elif f["type"] == "bool":
                default = f"{{{str(f.get('default', 'false')).lower()}}}"
            elif f["type"] == "string":
                default = "{}"
            else:
                default = "{}"
            w(f"    {cpp_type} {f['name']}{default}; // {desc}")
        w("};")
        if ns:
            for _ in ns_parts:
                w("}")
        w()

    # Resolve fully-qualified C++ name for a type
    def fq_type_name(name: str, ns: str) -> str:
        if name == "HIDReport":
            return "HIDReport"  # C-style typedef at global scope, accessible from ThetaGP::
        if ns:
            return f"::{ns}::{name}"
        return name

    # ── Serialize helpers (accumulated into buffer, emitted inside class) ────
    _serialize_lines: List[str] = []

    def sw(line: str = "") -> None:
        _serialize_lines.append(line)

    for t in types:
        name = t["name"]
        ns = t.get("namespace", "")
        fq = fq_type_name(name, ns)
        desc = sanitize_cpp_comment(t.get("description", ""))
        sw(f"    // Serialize {name} into a JsonObject")
        sw(f"    inline static void serialize{name}(JsonObject obj, const {fq} &v) {{")
        for f in t["fields"]:
            if f.get("omit_in_serialize"):
                continue
            json_key = f["json"]
            field_name = f["name"]
            if json_key:
                sw(f'        obj["{json_key}"] = v.{field_name};')
        sw("    }")
        sw()

        # Deserialize
        sw(f"    // Deserialize {name} from a JsonDocument")
        sw(f"    inline static void deserialize{name}(const JsonDocument &doc, {fq} &v) {{")
        for f in t["fields"]:
            if f.get("omit_in_serialize"):
                continue
            json_key = f["json"]
            field_name = f["name"]
            if not json_key:
                continue
            default_val = f.get("default", 0)
            # Map mid defaults for GamepadRawInput joystick
            if ns == "ThetaGP::Gamepad" and name == "GamepadRawInput":
                mid_defaults = {"lx": "GAMEPAD_JOYSTICK_MID", "ly": "GAMEPAD_JOYSTICK_MID",
                                "rx": "GAMEPAD_JOYSTICK_MID", "ry": "GAMEPAD_JOYSTICK_MID"}
                if field_name in mid_defaults:
                    sw(f'        v.{field_name} = doc["{json_key}"] | {mid_defaults[field_name]};')
                    continue
            if f["type"] == "string":
                sw(f'        v.{field_name} = doc["{json_key}"] | "";')
            elif f["type"] == "bool":
                sw(f'        v.{field_name} = doc["{json_key}"] | false;')
            else:
                sw(f'        v.{field_name} = doc["{json_key}"] | {default_val};')
        sw("    }")
        sw()

    # ── Proto class ──────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Proto class — command dispatch + serialization")
    w("// =============================================================================")
    w("namespace ThetaGP {")
    w()
    # ErrorCode inside class
    w("class Proto {")
    w("public:")
    w("    // Error Codes")
    w("    enum class ErrorCode : uint8_t {")
    for name, info in error_codes.items():
        w(f"        {name} = {info['code']}, // {sanitize_cpp_comment(info['description'])}")
    w("    };")
    w()
    w("    // Command handler signature (new: uses our own Json class)")
    w("    using Handler = void (*)(const char *cmd, const Json &json);")
    w()
    w("    // ── Handler registration ──")
    for cmd_info in commands:
        domain = cmd_info["domain"]
        var_name = f"_{domain}{to_pascal(cmd_info['name'])}Handler"
        func_name = f"register{to_pascal(domain)}{to_pascal(cmd_info['name'])}"
        w(f"    inline static void {func_name}(Handler h) {{ {var_name} = h; }}")
    w()
    w("    // ── Dispatch ──")
    w("    inline static bool dispatch(const char *cmd, const Json &json) {")
    for i, cmd_info in enumerate(commands):
        domain = cmd_info["domain"]
        full_name = f"{domain}.{cmd_info['name']}"
        var_name = f"_{domain}{to_pascal(cmd_info['name'])}Handler"
        if i == 0:
            w(f'        if (strcmp(cmd, "{full_name}") == 0)              {{ if ({var_name}) {{ {var_name}(cmd, json); return true; }} }}')
        else:
            w(f'        else if (strcmp(cmd, "{full_name}") == 0) {{ if ({var_name}) {{ {var_name}(cmd, json); return true; }} }}')
    w("        return false;")
    w("    }")
    w()
    w("private:")
    for cmd_info in commands:
        domain = cmd_info["domain"]
        var_name = f"_{domain}{to_pascal(cmd_info['name'])}Handler"
        w(f"    inline static Handler {var_name} = nullptr;")
    w()
    w("}; // class Proto")
    w()
    w("} // namespace ThetaGP")
    w()
    w("#ifdef __GNUC__")
    w("#pragma GCC diagnostic push")
    w('#pragma GCC diagnostic ignored "-Wunused-parameter"')
    w("#endif")
    w()
    w("#ifdef __GNUC__")
    w("#pragma GCC diagnostic pop")
    w("#endif")
    w()

    result = "\n".join(lines)

    if out:
        out.write_text(result)
        print(f"  [cpp] wrote {out}", file=sys.stderr)

    return result


# ═════════════════════════════════════════════════════════════════════════════
# Rust Generator
# ═════════════════════════════════════════════════════════════════════════════

def gen_rust(proto: dict, out: Optional[Path] = None) -> str:
    lines: List[str] = []
    types = proto.get("types", [])
    enums = proto.get("enums", [])
    commands = proto.get("commands", [])
    error_codes = proto.get("error_codes", {})

    def w(line: str = "") -> None:
        lines.append(line)

    # ── Header ──────────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w("// Source: protocol/protocol.toml")
    w(f"// Generated: {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w()
    w("use serde::{Deserialize, Serialize};")
    w("use serde_json::Value;")
    w()

    # ── Error codes ──────────────────────────────────────────────────────────
    w("// ---------------------------------------------------------------------------")
    w("// Error Codes")
    w("// ---------------------------------------------------------------------------")
    w("#[derive(Debug, Clone, Copy, PartialEq, Eq)]")
    w("#[repr(u8)]")
    w("pub enum ErrorCode {")
    for name, info in error_codes.items():
        desc = sanitize_cpp_comment(info.get("description", ""))
        w(f"    /// {desc}")
        w(f"    {to_pascal(to_snake(name))} = {info['code']},")
    w("}")
    w()

    w("impl ErrorCode {")
    w("    pub fn from_u8(v: u8) -> Option<Self> {")
    w("        match v {")
    for name, info in error_codes.items():
        w(f"            {info['code']} => Some(Self::{to_pascal(to_snake(name))}),")
    w("            _ => None,")
    w("        }")
    w("    }")
    w("}")
    w()

    # ── Enums ────────────────────────────────────────────────────────────────
    for enum_info in enums:
        name = enum_info["name"]
        desc = sanitize_cpp_comment(enum_info.get("description", ""))
        w(f"/// {desc}")
        w("#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]")
        w("#[repr(u8)]")
        w(f"pub enum {name} {{")
        for val in enum_info["values"]:
            desc = sanitize_cpp_comment(val.get("description", ""))
            w(f"    /// {desc}")
            w(f"    #[serde(rename = \"{val['name'].lower()}\")]")
            w(f"    {to_pascal(val['name'].lower())} = {val['value']},")
        w("}")
        w()

    # ── Structs ──────────────────────────────────────────────────────────────
    for t in types:
        name = t["name"]
        desc = sanitize_cpp_comment(t.get("description", ""))
        w(f"/// {desc}")
        w("#[derive(Debug, Clone, Serialize, Deserialize)]")
        w(f"pub struct {name} {{")
        for f in t["fields"]:
            if f.get("omit_in_serialize"):
                rust_type = RUST_TYPE_MAP.get(f["type"], "Value")
            else:
                rust_type = RUST_TYPE_MAP.get(f["type"], "Value")
            desc = sanitize_cpp_comment(f.get("description", ""))
            serde_attr = f'#[serde(rename = "{f["json"]}")]'
            w(f"    /// {desc}")
            w(f"    {serde_attr}")
            w(f"    pub {rust_ident(f['name'])}: {rust_type},")
        w("}")
        w()

    # ── Command enum ─────────────────────────────────────────────────────────
    w("// ---------------------------------------------------------------------------")
    w("// Command types")
    w("// ---------------------------------------------------------------------------")
    w()

    # Generate request types for commands with request fields
    for cmd_info in commands:
        req = cmd_info.get("request", [])
        if not req:
            continue
        domain = cmd_info["domain"]
        name = cmd_info["name"]
        struct_name = f"{to_pascal(domain)}{to_pascal(name)}Request"
        w("#[derive(Debug, Clone, Serialize, Deserialize)]")
        w(f"pub struct {struct_name} {{")
        w('    pub cmd: String,')
        w('    pub queued: u32,')
        for f in req:
            t = RUST_TYPE_MAP.get(f["type"], "Value")
            serde_attr = f'#[serde(rename = "{f["json"]}")]'
            w(f"    {serde_attr}")
            if f.get("required", False):
                w(f"    pub {rust_ident(f['name'])}: {t},")
            else:
                w(f"    #[serde(default)]")
                w(f"    pub {rust_ident(f['name'])}: {t},")
        w("}")
        w()

    for cmd_info in commands:
        resp = cmd_info.get("response", [])
        if not resp:
            continue
        domain = cmd_info["domain"]
        name = cmd_info["name"]
        struct_name = f"{to_pascal(domain)}{to_pascal(name)}Response"
        w("#[derive(Debug, Clone, Serialize, Deserialize)]")
        w(f"pub struct {struct_name} {{")
        w('    pub status: String,')
        w('    pub cmd: String,')
        w('    pub queued: u32,')
        for f in resp:
            t = RUST_TYPE_MAP.get(f["type"], "Value")
            serde_attr = f'#[serde(rename = "{f["json"]}")]'
            w(f"    {serde_attr}")
            w(f"    pub {rust_ident(f['name'])}: Option<{t}>,")
        w("}")
        w()

    result = "\n".join(lines)

    if out:
        out.write_text(result)
        print(f"  [rust] wrote {out}", file=sys.stderr)

    return result


# ═════════════════════════════════════════════════════════════════════════════
# TypeScript Generator
# ═════════════════════════════════════════════════════════════════════════════

def gen_ts(proto: dict, out: Optional[Path] = None) -> str:
    lines: List[str] = []
    types = proto.get("types", [])
    enums = proto.get("enums", [])
    commands = proto.get("commands", [])
    error_codes = proto.get("error_codes", {})

    def w(line: str = "") -> None:
        lines.append(line)

    # ── Header ──────────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w("// Source: protocol/protocol.toml")
    w(f"// Generated: {__import__('datetime').datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w()

    # ── Error codes ──────────────────────────────────────────────────────────
    w("// ---------------------------------------------------------------------------")
    w("// Error Codes")
    w("// ---------------------------------------------------------------------------")
    w("export const enum ErrorCode {")
    for name, info in error_codes.items():
        w(f"  {name} = {info['code']},")
    w("}")
    w()

    # ── Enums ────────────────────────────────────────────────────────────────
    for enum_info in enums:
        name = enum_info["name"]
        desc = sanitize_cpp_comment(enum_info.get("description", ""))
        w(f"// {desc}")
        w(f"export const enum {name} {{")
        for val in enum_info["values"]:
            desc = sanitize_cpp_comment(val.get("description", ""))
            w(f"  // {desc}")
            w(f"  {val['name']} = {val['value']},")
        w("}")
        w()

    # ── Interfaces ───────────────────────────────────────────────────────────
    for t in types:
        name = t["name"]
        desc = sanitize_cpp_comment(t.get("description", ""))
        w(f"// {desc}")
        w(f"export interface {name} {{")
        for f in t["fields"]:
            if f.get("omit_in_serialize"):
                continue
            ts_type = TS_TYPE_MAP.get(f["type"], "any")
            desc = sanitize_cpp_comment(f.get("description", ""))
            w(f"  // {desc}")
            w(f"  {f['json']}: {ts_type};")
        w("}")
        w()

    # ── Command request/response interfaces ─────────────────────────────────
    for cmd_info in commands:
        domain = cmd_info["domain"]
        name = cmd_info["name"]
        full_name = f"{domain}.{name}"
        desc = sanitize_cpp_comment(cmd_info.get("description", ""))

        # Request interface
        req = cmd_info.get("request", [])
        w(f"// {full_name} — Request ({desc})")
        if req:
            w(f"export interface {to_pascal(domain)}{to_pascal(name)}Request {{")
            w("  cmd: string;")
            w("  queued: number;")
            for f in req:
                ts_type = TS_TYPE_MAP.get(f["type"], "any")
                optional = "" if f.get("required", False) else "?"
                w(f"  {f['json']}{optional}: {ts_type};")
            w("}")
        else:
            w(f"export type {to_pascal(domain)}{to_pascal(name)}Request = {{ cmd: string; queued: number; }};")
        w()

        # Response interface
        BOILERPLATE_TS_RESPONSE_FIELDS = {"status", "cmd", "queued"}
        resp = cmd_info.get("response", [])
        if resp:
            w(f"export interface {to_pascal(domain)}{to_pascal(name)}Response {{")
            w("  status: string;")
            w("  cmd: string;")
            w("  queued: number;")
            for f in resp:
                if f["json"] in BOILERPLATE_TS_RESPONSE_FIELDS:
                    continue
                ts_type = TS_TYPE_MAP.get(f["type"], "any")
                w(f"  {f['json']}?: {ts_type};")
            w("}")
        else:
            # Generic response with status/cmd/queued
            pass
        w()

    result = "\n".join(lines)

    if out:
        out.write_text(result)
        print(f"  [ts]  wrote {out}", file=sys.stderr)

    return result


# ═════════════════════════════════════════════════════════════════════════════
# Field manifest generator (JSON)
# ═════════════════════════════════════════════════════════════════════════════

def command_fields(entries: List[dict]) -> List[Dict[str, Any]]:
    """One command's field array as a list of plain, ordered records.

    Keeps only what describes the wire shape (name / type / json, plus the
    `required`, `description` and `role` the TOML happens to carry), so a
    consumer needs no TOML parser and no knowledge of this file's other tables.
    """
    out_fields: List[Dict[str, Any]] = []
    for f in entries:
        record: Dict[str, Any] = {"name": f["name"], "type": f["type"], "json": f["json"]}
        if "required" in f:
            record["required"] = bool(f["required"])
        if "description" in f:
            record["description"] = f["description"]
        if "role" in f:
            record["role"] = f["role"]
        out_fields.append(record)
    return out_fields


def gen_fields(proto: dict, out: Optional[Path] = None,
               source_path: Optional[Path] = None) -> str:
    """Ordered request/response field lists for every command, as JSON.

    Same shape as the code generators, but language neutral: this is the
    derivation path for consumers that would otherwise hand-copy the field
    shapes out of protocol.toml. Order is the declaration order of the TOML
    arrays — the order the firmware writes the fields in and the order the
    consumers read them in — never a sorted or re-grouped one.

    ``source_sha256`` fingerprints the TOML this manifest was derived from, so
    a consumer holding a manifest is not left believing it describes the
    protocol.toml on disk: the manifest is a generated artifact, and a stale
    one would let a consumer check the protocol against the shape it no longer
    has and pass.

    ``generator_sha256`` fingerprints the generator it was emitted by, over the
    files ``generator_sources`` names. Two digests rather than one, because the
    shape of this manifest is derived from two things that move independently:
    the TOML says which fields there are, the emitter says which of them it
    writes and under which keys. Either one changing makes the field lists below
    wrong for a consumer, and the source digest alone misses the emitter's half
    — an emitter that stopped emitting a field would leave this manifest valid
    against an unchanged protocol.toml, and a consumer comparing a response
    against the shorter list that was regenerated would find nothing missing.
    Regenerating after either change is what clears both digests.
    """
    commands = proto.get("commands", [])
    source = Path(source_path) if source_path is not None else None

    manifest = {
        "generated_by": "scripts/gen_proto.py — DO NOT EDIT MANUALLY",
        # What it was read from, and the digest of exactly those bytes.
        "source": str(source) if source is not None else "protocol/protocol.toml",
        "source_sha256": file_sha256(source) if source is not None else "",
        # What emitted it, and the digest of exactly those bytes. The list is
        # what the digest was computed over, so a consumer recomputes the same
        # digest from it rather than from a list of its own that could lag a
        # generator split.
        "generator_sources": list(GENERATOR_SOURCES),
        "generator_sha256": generator_sha256(),
        "protocol_version": proto.get("meta", {}).get("version", ""),
        # Keyed "<domain>.<name>", the same full command name the wire uses.
        # Insertion order = the TOML's [[commands]] order.
        "commands": {
            f"{cmd['domain']}.{cmd['name']}": {
                "domain": cmd["domain"],
                "name": cmd["name"],
                "description": cmd.get("description", ""),
                "request": command_fields(cmd.get("request", [])),
                "response": command_fields(cmd.get("response", [])),
            }
            for cmd in commands
        },
    }

    result = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"

    if out:
        out.write_text(result)
        print(f"  [fields] wrote {out}", file=sys.stderr)

    return result


# ═════════════════════════════════════════════════════════════════════════════
# Response field table generator (C++ header, X-macro)
# ═════════════════════════════════════════════════════════════════════════════

def resp_macro(domain: str, name: str) -> str:
    """The name of the X-macro table of command <domain>.<name>."""
    return f"THETAGP_RESP_{domain.upper()}_{name.replace('-', '_').upper()}"


def gen_resp(proto: dict, out: Optional[Path] = None) -> str:
    """Response payload field tables, one X-macro per command, as C++.

    Each table lists the response fields of one command in the order
    protocol.toml declares them — the order the response writes them in — and
    each entry carries the JSON key, the printf argument type, the conversion
    for that type and the presence of the field. A consumer expands a table
    with a macro of its own, so the same list drives the bytes on the wire and
    anything that has to agree with them; the firmware's copy of the order, the
    keys and the specifiers is this file, not a format string written by hand.

    What the table cannot carry is the value of a field: that is firmware state,
    and it is what the consumer's macro supplies. It supplies it *by name* —
    X(<json name>, ...) selects the value for that name — so the two sides meet
    on the name and not on position, and a field added to or renamed in
    protocol.toml is a value the consumer does not have rather than a value
    printed under the wrong key.

    A field whose type has no printf form is a field no table can carry, and its
    response would come out short of it; commands with such a field get no table
    and are named in the header instead.
    """
    commands = proto.get("commands", [])
    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w("// Source: protocol/protocol.toml")
    w(f"// Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w("#pragma once")
    w("#include <cstdint>")
    w()
    w("// Response payload fields of a command, in the order the response writes")
    w("// them, one table per command. A table is expanded with a macro of the")
    w("// consumer's own, called once per field as")
    w("//     X(<json name>, <printf argument type>, <conversion>, <presence>)")
    w("// where the name selects the value to write, the type is what that value")
    w("// has to be, the conversion is the printf specifier for it, and the")
    w("// presence is 1 or a flag that is 0 in a build without the field.")
    w()

    # Presence flags, once per role a response field carries.
    roles = {f.get("role") for cmd in commands for f in cmd.get("response", [])}
    for role in sorted(r for r in roles if r in ROLE_PRESENCE):
        flag, guard = ROLE_PRESENCE[role]
        w(f"// role = \"{role}\": the field is written only in a build where")
        w(f"// {guard} is defined.")
        w(f"#ifdef {guard}")
        w(f"#define {flag} 1")
        w("#else")
        w(f"#define {flag} 0")
        w("#endif")
        w()

    unformattable: List[str] = []
    for cmd in commands:
        resp = cmd.get("response", [])
        if not resp:
            continue
        full_name = f"{cmd['domain']}.{cmd['name']}"
        missing = [f["name"] for f in resp if f["type"] not in PRINTF_TYPE_MAP]
        if missing:
            unformattable.append(f"{full_name} ({', '.join(missing)})")
            continue
        w(f"// {full_name} — {sanitize_cpp_comment(cmd.get('description', ''))}")
        w(f"#define {resp_macro(cmd['domain'], cmd['name'])}(X) \\")
        for i, f in enumerate(resp):
            ctype, spec = PRINTF_TYPE_MAP[f["type"]]
            flag = ROLE_PRESENCE.get(f.get("role"), ("1",))[0]
            continuation = " \\" if i + 1 < len(resp) else ""
            w(f'    X({f["name"]}, {ctype}, "{spec}", {flag}){continuation}')
        w()

    if unformattable:
        w("// No table, because a response field of no printf form would be missing")
        w("// from every response written through one:")
        for entry in unformattable:
            w(f"//   {entry}")
        w()

    result = "\n".join(lines) + "\n"

    if out:
        out.write_text(result)
        print(f"  [resp] wrote {out}", file=sys.stderr)

    return result


# ═════════════════════════════════════════════════════════════════════════════
# CLI
# ═════════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="ThetaGP Protocol Code Generator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 scripts/gen_proto.py
  python3 scripts/gen_proto.py --target cpp --dry-run
  python3 scripts/gen_proto.py --target rust,ts
        """,
    )
    parser.add_argument("--protocol", default="protocol/protocol.toml",
                        help="Path to protocol.toml (default: protocol/protocol.toml)")
    parser.add_argument("--target", default="cpp,rust,ts,fields,resp",
                        help="Comma-separated targets: cpp,rust,ts,fields,resp (default: all)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print generated code to stdout instead of writing files")
    parser.add_argument("--outdir-cpp", default="protocol",
                        help="Output dir for C++ generated header")
    parser.add_argument("--outdir-rust", default="protocol",
                        help="Output dir for Rust generated module")
    parser.add_argument("--outdir-ts", default="protocol",
                        help="Output dir for TS generated types")
    parser.add_argument("--outdir-fields", default="protocol",
                        help="Output dir for the generated JSON field manifest")
    parser.add_argument("--outdir-resp", default="protocol",
                        help="Output dir for the generated C++ response field tables")

    args = parser.parse_args()
    targets = [t.strip() for t in args.target.split(",")]
    proto_path = Path(args.protocol)

    if not proto_path.exists():
        print(f"ERROR: Protocol file not found: {proto_path}", file=sys.stderr)
        sys.exit(1)

    proto = load_protocol(str(proto_path))
    validate_domains(proto)
    validate_types(proto)
    validate_field_roles(proto)

    # Print summary
    types_list = proto.get("types", [])
    cmds_list = proto.get("commands", [])
    enums_list = proto.get("enums", [])
    print(f"ThetaGP Protocol Code Generator", file=sys.stderr)
    print(f"  Source:     {proto_path}", file=sys.stderr)
    print(f"  Types:      {len(types_list)}", file=sys.stderr)
    print(f"  Commands:   {len(cmds_list)}", file=sys.stderr)
    print(f"  Enums:      {len(enums_list)}", file=sys.stderr)
    print(f"  Targets:    {', '.join(targets)}", file=sys.stderr)
    print(file=sys.stderr)

    if args.dry_run:
        out_target = None
    else:
        out_target = object()  # truthy, but we use Path below

    for tgt in targets:
        if tgt == "cpp":
            out_file = None if args.dry_run else Path(args.outdir_cpp) / "proto.h"
            gen_cpp(proto, out_file)
            if args.dry_run:
                print(gen_cpp(proto))
        elif tgt == "rust":
            out_file = None if args.dry_run else Path(args.outdir_rust) / "proto.rs"
            gen_rust(proto, out_file)
            if args.dry_run:
                print(gen_rust(proto))
        elif tgt == "ts":
            out_file = None if args.dry_run else Path(args.outdir_ts) / "types.ts"
            gen_ts(proto, out_file)
            if args.dry_run:
                print(gen_ts(proto))
        elif tgt == "fields":
            out_file = None if args.dry_run else Path(args.outdir_fields) / "proto_fields.json"
            gen_fields(proto, out_file, proto_path)
            if args.dry_run:
                print(gen_fields(proto, source_path=proto_path))
        elif tgt == "resp":
            out_file = None if args.dry_run else Path(args.outdir_resp) / "proto_resp.h"
            gen_resp(proto, out_file)
            if args.dry_run:
                print(gen_resp(proto))
        else:
            print(f"WARNING: Unknown target '{tgt}' (supported: cpp, rust, ts, fields, resp)",
                  file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
