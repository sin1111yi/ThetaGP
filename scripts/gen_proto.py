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
  - MD    table    (protocol/protocol-fields.md) — the response fields of
          every command as the Markdown table the docs point at, so the table a
          reader is handed is derived from the same TOML as the firmware's; it
          sits beside protocol.toml and is tracked, because it is the one
          artifact read by a person rather than compiled by a build
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
  python3 scripts/gen_proto.py --target fields-md      # Markdown tables only
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

# The types a *response* field may name that have no printf form at all, and so
# are a gap in nothing: PRINTF_TYPE_MAP pairs a declared type with the one C
# type an argument of it has and the one conversion that writes it, and `any` is
# a value whose JSON type is not known until it is read — the value of one key
# is a number and of another a string (docs/cdc-json-protocol.md, config.get_key,
# whose example response carries "value":1, a number). The firmware's only
# printf is frozen's json_printf (src/utils/json/json.h), whose specifiers
# (%B, %Q, %.*Q, %V, %H, %M) each take a fixed C type chosen at the call site,
# and a response field table entry is `static_assert`ed against one: no
# conversion writes a value of unknown type, so a mapping for `any` would have to
# invent a C type for it and would then write a JSON string where the protocol
# says number. A response field of such a type is therefore not mapped but
# reported instead: gen_resp() writes no table for its command and names it in
# the header, so the omission is in the artifact and not in the rule.
# Names, not a blanket "unmappable" exemption: a type nobody has decided about
# is still a type PRINTF_TYPE_MAP has to map or the run stops.
PRINTF_LESS_TYPES = ("any",)

# A field's `role` is a property that outlives its type and its position: who
# else has to know about that field.
#   accounting    — a member of the per-task accounting subset the CDC test
#                   suite requires in every build
#   task_counters — reported only in a build that compiles the task counters in
#                   (USE_TASK_COUNTERS), so a response omits the field without
#                   them
ROLE_ACCOUNTING = "accounting"
ROLE_TASK_COUNTERS = "task_counters"

# The registry of roles, and the whole of what this generator knows about one:
# the compile switch the generated artifacts carry for that role — the name of
# the flag a generated response table entry is written with, and the firmware
# macro that flag is defined from — or None for a role no switch decides.
#
# A registry and not a set of names, because a role is only worth tagging a
# field with when something downstream acts on it: a name this table does not
# carry is a name nothing derives anything from, and validate_field_roles()
# stops generation on it rather than let the field read as marked while every
# artifact and every consumer stays as if it were not.
#
# `accounting`'s None is deliberate, and is the one entry with no switch to
# name: the fields it marks are meant to be in every build (the CDC test
# suite's per-task accounting checks require them unconditionally), so a
# response table writes them with the constant presence 1, and what acts on the
# role is that suite, which reads the mark out of the field manifest. Giving it
# a switch would invent a condition the protocol does not have.
ROLE_SWITCHES: Dict[str, Optional[Tuple[str, str]]] = {
    ROLE_TASK_COUNTERS: ("THETAGP_RESP_HAS_TASK_COUNTERS", "USE_TASK_COUNTERS"),
    ROLE_ACCOUNTING: None,
}

# The roles that make a response field conditional, and what the condition is:
# the switch and the guard ROLE_SWITCHES registers for them. Derived from the
# registry rather than written beside it, so the two cannot disagree — a role a
# table entry can be conditional on is exactly a role the registry gives a
# switch. A role absent here is always present, and its table entry carries the
# constant 1.
ROLE_PRESENCE = {role: switch for role, switch in ROLE_SWITCHES.items() if switch}


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
    """Abort unless every `type` protocol.toml uses is mapped by the tables its use needs.

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

    Which tables a type has to be in depends on where the TOML uses it. A
    request field is read by each target out of its own table — CPP, RUST, TS —
    and needs no printf form. A *response* field is written by the firmware
    through a printf conversion as well, so its type has to be in
    PRINTF_TYPE_MAP too, and a rule that stopped at the three would pass exactly
    the case of a name added to them and missed in the fourth. That fourth
    mapping is the one with no fallback: a response field of a type it does not
    map leaves gen_resp() no table to write for its command, so the command
    keeps a response the firmware writes by hand. The exception is
    PRINTF_LESS_TYPES, the named types for which no conversion exists at all.

    Those last two response-side tables are checked against each other as well:
    a type in PRINTF_LESS_TYPES is one no conversion writes, so a type in both
    it and PRINTF_TYPE_MAP is a field gen_resp() would write a table for while
    the exception list says no table can be written from it. Only one of the
    two can be right, and which one is a decision about the type — so an
    overlap is reported rather than resolved.
    """
    request = {f["type"] for cmd in proto.get("commands", [])
               for f in cmd.get("request", [])}
    response = {f["type"] for cmd in proto.get("commands", [])
                for f in cmd.get("response", [])}
    nested = {f["type"] for t in proto.get("types", []) for f in t.get("fields", [])}
    used = sorted(nested | request | response)
    unmapped = [(name, [t for t in used if t not in table])
                for name, table in TYPE_MAPS]
    # Only the response side: a request field is not written by a printf, and
    # requiring a conversion for it would report types nothing is missing.
    no_printf = [t for t in sorted(response)
                 if t not in PRINTF_TYPE_MAP and t not in PRINTF_LESS_TYPES]
    # The reverse of the line above, and the one direction the TOML cannot
    # state: a type in PRINTF_LESS_TYPES is one the response side has decided no
    # conversion writes, and one in PRINTF_TYPE_MAP is one it has a (printf
    # argument type, conversion) pair for. A type in both would say at once that
    # the firmware cannot write a value of that type and which conversion
    # writes it — and gen_resp() follows the map, so the field would be given
    # exactly the table the exception list says cannot exist, with the list
    # unchanged and nothing else to notice. Disjointness is a property of those
    # two tables and of nothing in protocol.toml, so it is checked here, beside
    # them, rather than by any rule about the types a field may name.
    overlap = sorted(set(PRINTF_LESS_TYPES) & set(PRINTF_TYPE_MAP))
    if any(missing for _, missing in unmapped) or no_printf or overlap:
        for name, missing in unmapped:
            if missing:
                print(f"ERROR: {name} has no mapping for type(s): {missing}",
                      file=sys.stderr)
        if overlap:
            print(f"ERROR: type(s) in both PRINTF_TYPE_MAP and "
                  f"PRINTF_LESS_TYPES: {overlap}", file=sys.stderr)
            print("       PRINTF_LESS_TYPES names the types no printf "
                  "conversion writes, so a type it names does not belong in "
                  "PRINTF_TYPE_MAP: a response field of such a type would be "
                  "written through the mapping, which is the table the "
                  "exception list says cannot be written. Drop it from one of "
                  "the two.", file=sys.stderr)
        if no_printf:
            print(f"ERROR: PRINTF_TYPE_MAP has no mapping for type(s): {no_printf}",
                  file=sys.stderr)
        print(f"       Types used by protocol.toml: {used}", file=sys.stderr)
        print(f"       Mapped types: {sorted(set().union(*(set(t) for _, t in TYPE_MAPS)))}",
              file=sys.stderr)
        print("       A type a table does not map is emitted as that target's "
              "untyped value (JsonVariant / serde_json::Value / any) instead of "
              "the declared one; add the mapping or fix the type name.",
              file=sys.stderr)
        if no_printf:
            print("       PRINTF_TYPE_MAP is the response side's fourth table: a "
                  "response field is written through a printf conversion, so a "
                  "field of a type it does not map leaves its command no "
                  "response table (gen_resp() names it in the header) and the "
                  "build still succeeds.", file=sys.stderr)
            print(f"       Types with no printf form at all, not a gap in it: "
                  f"{list(PRINTF_LESS_TYPES)}", file=sys.stderr)
        sys.exit(1)


def validate_field_roles(proto: dict) -> None:
    """Abort unless every field `role` is a role this generator is registered for.

    A role is a name this file and the artifacts it writes agree on, so the
    name is looked up in the registry (ROLE_SWITCHES) once per field and once
    per side, and a field tagged with a name that is not there stops generation
    naming the command, the side, the field and the role. That much a generator
    can hold: a registered role is one it has something to derive for — the
    compile switch that decides whether the field is written — and a name it
    does not carry is one no artifact acts on, so the field would read as
    marked while the response table of its command and the suite's expectation
    both stayed as if it were not. A misspelling has to stop generation rather
    than leave a marking that does nothing, and so has a `role` spelled empty.

    What this cannot hold is whether a field should carry a role at all. That
    is a statement about the field and not about the name, and neither the TOML
    nor this file states the field's intent: a registered role on a field that
    does not in fact vary with its condition still reads as conditional, and
    every check downstream follows the declaration and agrees with it —
    field_coverage_errors() takes a `role` as the license to leave the field
    out of an emitted list, which is exactly why a wrong license is not
    something generation can see. It has to be reviewed where the field is
    declared; here, only a role nobody registered is refused.
    """
    unregistered: List[str] = []
    for cmd in proto.get("commands", []):
        command = f"{cmd['domain']}.{cmd['name']}"
        for side in ("request", "response"):
            for f in cmd.get(side, []):
                role = f.get("role")
                if role is not None and role not in ROLE_SWITCHES:
                    unregistered.append(
                        f"{command}: {side} field '{f['name']}' carries "
                        f"role {role!r}, which is not registered")
    if unregistered:
        for entry in unregistered:
            print(f"ERROR: unregistered field role — {entry}", file=sys.stderr)
        print("       Registered roles, each with the compile switch the "
              f"generated artifacts carry for it: {ROLE_SWITCHES}", file=sys.stderr)
        print("       A role is a name this generator and the artifacts it "
              "writes agree on; register it in ROLE_SWITCHES with the switch "
              "it decides, or drop the tag from the field.", file=sys.stderr)
        print("       Not checked, and not checkable here: whether the field "
              "should carry a role at all. A role is what lets an emitter "
              "leave a field out of its lists, so a registered role on a field "
              "that does not vary with it reads as conditional to every check "
              "downstream and to the review of the declaration.", file=sys.stderr)
        sys.exit(1)


def field_coverage_errors(command: str, side: str, declared: List[dict],
                          emitted: List[str]) -> List[str]:
    """Every way one emitted field list fails to cover one side of a command.

    ``side`` is ``"request"`` or ``"response"``, and is carried into every
    problem it reports: the two sides are compared the same way but are not the
    same loss, and a field list that lost one reads identically either way — a
    request field dropped is a command that no longer reads an argument it
    declares, a response field dropped a response that no longer carries a
    value. Naming the side is what lets the reader tell them apart.

    ``declared`` is that side's array as the TOML spells it, and ``emitted``
    the field names an emitter derived from it — the two halves are taken from
    different places on purpose, the first from the source and the second from
    the emitter's own output, so the comparison can come out equal only when
    the emitter kept every field. A field carrying a ``role`` is not a loss
    when an emitted list leaves it out: the role is what makes the field
    conditional, and a view that excludes it is the design. A field with no
    role has nothing that could excuse its absence, and that is the case
    neither the TOML nor the digest can see — an emitter that drops it writes a
    manifest, a response table and a docs table that agree with each other and
    are all short the same field, and the firmware loses it with them.

    A side the command does not have is not a case here: an empty ``declared``
    list has nothing to be short of, and the comparison comes out equal.

    The other direction is checked for the same reason: a field emitted under a
    name the TOML does not declare is a key no consumer can pair with a value,
    arriving on the wire as a field the protocol never had.
    """
    declared_names = {f["name"] for f in declared}
    emitted_names = set(emitted)
    problems = [f"{command}: {side} field '{f['name']}' declared but never emitted"
                for f in declared
                if f["name"] not in emitted_names and "role" not in f]
    problems += [f"{command}: {side} field '{name}' emitted but not declared"
                 for name in emitted if name not in declared_names]
    return problems


def fail_uncovered_fields(problems: List[str]) -> None:
    """Report the declared fields an emitter left uncovered and stop.

    One exit for both callers — the pre-flight validator and an emitter that
    built its own field list — so a gap reads the same wherever it is found.
    """
    if not problems:
        return
    for problem in problems:
        print(f"ERROR: emitter coverage — {problem}", file=sys.stderr)
    print("       A request or response field with no `role` has to reach every "
          "emitted field list of its side; one filtered out of the emitter is "
          "lost by every artifact derived from it, and no consumer of them can "
          "tell.", file=sys.stderr)
    sys.exit(1)


def validate_field_coverage(proto: dict) -> None:
    """Abort unless the manifest emitter emits every field of both sides it declares.

    The counterpart of validate_domains() and validate_types() for the one claim
    neither the TOML nor a digest can carry: that an emitter *emits* what the
    TOML declares. The manifest records the generator's digest, so a change to
    an emitter is visible, but a digest only says the emitter moved — it cannot
    say whether the list it wrote is short a field, and an emitter that stopped
    emitting one writes artifacts that are mutually consistent and all missing
    it. So the names themselves are compared, before anything is written: the
    declared side from the TOML, the emitted side from command_fields(), the
    records every consumer of the manifest reads.

    Both sides of every command are compared, each under its own name. The
    request side is the one this check used to leave out, which left a request
    field an emitter dropped audible in nothing at all.
    """
    problems = [
        problem
        for cmd in proto.get("commands", [])
        for side in ("request", "response")
        for problem in field_coverage_errors(
            f"{cmd['domain']}.{cmd['name']}", side, cmd.get(side, []),
            [r["name"] for r in command_fields(cmd.get(side, []))])
    ]
    fail_uncovered_fields(problems)


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

    # Both field lists of a command are built and checked before any of them is
    # written, so the comparison is between what this emitter is about to write
    # and what the TOML declares, and not between the declared array and
    # itself: a record dropped while the lists are built is a field this
    # manifest stops carrying while protocol.toml still has it, and a consumer
    # reading the manifest cannot see it — the list it reads is the one that
    # lost the field. Both sides, because a request field list is one too: a
    # request field dropped here is a command whose arguments no longer match
    # the ones the protocol declares, and it used to be dropped in silence.
    entries: Dict[str, Dict[str, Any]] = {}
    for cmd in commands:
        full_name = f"{cmd['domain']}.{cmd['name']}"
        sides = {side: command_fields(cmd.get(side, []))
                 for side in ("request", "response")}
        for side, emitted in sides.items():
            fail_uncovered_fields(field_coverage_errors(
                full_name, side, cmd.get(side, []), [r["name"] for r in emitted]))
        entries[full_name] = {
            "domain": cmd["domain"],
            "name": cmd["name"],
            "description": cmd.get("description", ""),
            "request": sides["request"],
            "response": sides["response"],
        }

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
        "commands": entries,
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
# Response field table generator (Markdown, for the docs)
# ═════════════════════════════════════════════════════════════════════════════

def md_cell(text: str) -> str:
    """One cell's text, folded onto a single line and escaped for a table.

    A description carrying a `|` or a newline would otherwise split the row or
    end the table; both are legal in a TOML string, and neither is a reason for
    the generated table to come out broken.
    """
    return text.replace("|", "\\|").replace("\n", " ").strip()


def gen_fields_md(proto: dict, out: Optional[Path] = None,
                  source_path: Optional[Path] = None) -> str:
    """The response fields of every command as Markdown tables, one per command.

    The derivation of gen_fields(), addressed to a reader instead of to a
    program: the field table under a command's section in the docs is a copy of
    that command's `response` array, so a field the TOML gains is a field the
    table keeps missing until someone notices. This emitter writes the tables
    the docs point at, from the same source and in the same order, so the two
    cannot disagree.

    Three columns, and no fourth: the field's JSON key, the `type`
    protocol.toml declares it with, and its `description` when it has one. The
    type is written verbatim rather than translated into a language's name
    (`int`, `string`), because a translation is a second table to keep in step
    with the first and the artifact's value is that it can be compared against
    the TOML character by character.

    What no field of the TOML carries, and so no table here can, is the other
    half of a doc table: the unit of a value, the firmware symbol it is read
    from, the caveat that makes the reading mean something. Those stay in the
    surrounding prose — this artifact is a table to point at, not a replacement
    for the section it sits in.

    A command with no response fields gets no table, and is named at the end
    rather than left out silently: a reader can then tell a command that has no
    payload from one the emitter dropped.
    """
    commands = proto.get("commands", [])
    source = Path(source_path) if source_path is not None else None
    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

    w("<!--")
    w("  Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w(f"  Source: {source if source is not None else 'protocol/protocol.toml'}")
    if source is not None:
        w(f"  Source sha256: {file_sha256(source)}")
    w("-->")
    w()
    w("# CDC 响应字段表（派生自 `protocol/protocol.toml`）")
    w()
    w("`python3 scripts/gen_proto.py --target fields-md` 的输出：每个有响应负载的命令")
    w("一张表。字段名（JSON 键）、类型与顺序逐字取自 `protocol.toml` 的 `response`")
    w("数组 —— 类型写的是 TOML 里声明的名字（`u32` / `string` / …），不经翻译，")
    w("以便逐字对拍；顺序即响应写出字段的顺序。")
    w()
    w("**本文件是派生物，手改无效**：协议改动一律改 `protocol.toml` 再重跑生成器。")
    w()
    w(f"协议版本：`{proto.get('meta', {}).get('version', '')}`")
    w()

    for cmd in commands:
        resp = cmd.get("response", [])
        if not resp:
            continue
        full_name = f"{cmd['domain']}.{cmd['name']}"
        # The rows are built before any of them is written, so that the coverage
        # check below compares what this emitter writes against what the TOML
        # declares and not the declared array against itself: a row dropped
        # while building them is a field the table stops carrying while
        # protocol.toml still has it, which is the loss the table cannot show.
        rows = [{
            "name": f["name"],
            "json": md_cell(f["json"]),
            "type": md_cell(f["type"]),
            # `—` and not an empty cell: a placeholder shows the column was
            # considered for the field and the TOML carries nothing for it.
            "note": md_cell(f.get("description", "")) or "—",
        } for f in resp]
        fail_uncovered_fields(
            field_coverage_errors(full_name, "response", resp,
                                  [r["name"] for r in rows]))
        w(f"## `{full_name}`")
        w()
        desc = md_cell(cmd.get("description", ""))
        if desc:
            w(desc)
            w()
        w("| 字段 | 类型 | 数据源或说明 |")
        w("|------|------|--------------|")
        for row in rows:
            w(f"| `{row['json']}` | {row['type']} | {row['note']} |")
        w()

    empty = [f"{c['domain']}.{c['name']}" for c in commands if not c.get("response")]
    if empty:
        w("## 无响应负载的命令")
        w()
        w("这些命令的 `response` 为空：响应只有通用字段（`status` / `cmd` / `queued`）：")
        w()
        for name in empty:
            w(f"- `{name}`")
        w()

    result = "\n".join(lines) + "\n"

    if out:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(result, encoding="utf-8")
        print(f"  [fields-md] wrote {out}", file=sys.stderr)

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
    parser.add_argument("--target", default="cpp,rust,ts,fields,resp,fields-md",
                        help="Comma-separated targets: cpp,rust,ts,fields,resp,fields-md (default: all)")
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
    parser.add_argument("--outdir-fields-md", default="protocol",
                        help="Output dir for the generated Markdown response field tables")

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
    validate_field_coverage(proto)

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
        elif tgt == "fields-md":
            # The Markdown tables are read, not compiled, so they sit beside the
            # TOML they are derived from and in the repository, not with the
            # generated code: docs/ is not tracked, and the one artifact meant
            # for a reader has to be readable in the repository without running
            # this script first.
            out_file = (None if args.dry_run
                        else Path(args.outdir_fields_md) / "protocol-fields.md")
            gen_fields_md(proto, out_file, proto_path)
            if args.dry_run:
                print(gen_fields_md(proto, source_path=proto_path))
        else:
            print(f"WARNING: Unknown target '{tgt}' (supported: cpp, rust, ts, fields, resp, fields-md)",
                  file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
