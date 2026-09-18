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


# The response envelope: the keys a reply carries because the response builder
# writes them, and the keys a request carries because the host writes them —
# neither is declared by a command. Declared in protocol.toml's [envelope]
# section — one entry per key, carrying the wire type, the JSON key it travels
# under, the `appears` column (in which messages of each side the key shows up,
# and how completely), the description a reader gets, and (as the section key)
# the field name every target writes — so the leading keys of both messages come
# from the source of truth like the rest of their shape. The emitters below read
# them from there rather than each carrying a literal of its own;
# validate_envelope() holds the declaration to what they need, and refuses a
# command that declares one of these keys on the side it appears on.
#
# The keys an error reply adds behind `status` are declared the same way, in
# protocol.toml's [error_reply], and held by validate_error_reply() below: one
# shape for the envelope, one for what an error reply adds to it.

# The two sides a message has, named as the `appears` column names them.
ENVELOPE_SIDES = ("request", "reply")

# How completely a side's messages carry a key, and the whole vocabulary both
# cells of `appears` draw on. One vocabulary for the two sides, because the
# question is the same on each: is the key in every message of this side, in
# some of them, or in none. The emitters answer it with one rule, applied on
# whichever side the key appears — `always` is a required field, `some` an
# optional one (`Option<T>` / `?:`), `never` no field for that side at all.
#
# The vocabulary belongs to the message and not to this section: [error_reply]
# declares its keys in the same three words — nothing is emitted from those, and
# there the column decides only which side's fields the keys are reserved
# against — so validate_error_reply() reads the words with the constants below
# rather than spelling a second vocabulary of its own.
#
# What `some` does not say is which of the side's messages carry the key, or
# that two keys of `some` are missing from the same ones: it is per key and per
# side, not per message shape. The reply shapes that omit a key are firmware
# evidence (src/test/dispatcher.cpp:55, :75 and src/test/profile_cmd_handler.cpp:246
# for the two spellings of `cmd`/`queued`'s absence) and stay in the comments of
# the section rather than becoming a second thing this column has to express.
ENVELOPE_ALWAYS, ENVELOPE_SOME, ENVELOPE_NEVER = "always", "some", "never"
ENVELOPE_APPEARANCES = (ENVELOPE_ALWAYS, ENVELOPE_SOME, ENVELOPE_NEVER)


def envelope_fields(proto: dict) -> List[Dict[str, Any]]:
    """The declared response envelope, in declaration order — the wire order.

    The section key supplies the field name, and it wins over anything the entry
    itself carries: validate_envelope() refuses an entry that declares a `name`
    column, so a key of the entry's own can never reach a target as a field name
    while the shadow check below still reserves the section key.
    """
    return [{**entry, "name": name} for name, entry in proto.get("envelope", {}).items()]


def envelope_on_side(proto: dict, side: str) -> List[Dict[str, Any]]:
    """The envelope keys one side's messages carry, in declaration order.

    Each entry carries the section's own columns plus `required`: whether *every*
    message of that side carries the key, which is the one thing an emitter needs
    beyond the key's name and type. A key of `appears.<side> = "some"` is written
    optional in both targets, a key of `"always"` required, and one of `"never"`
    is not written for that side at all — so this is what keeps a reply's leading
    fields from claiming more than the firmware writes (the dispatcher's error
    replies carry `status` alone of the envelope's keys) and a request's leading
    fields from claiming a key no request has (`status`).

    That a side's messages carry all of them is therefore explicitly not assumed:
    a request carries `cmd` and `queued` and no `status`, a reply carries `status`
    and — on the firmware's format strings — `cmd`/`queued` only when it answers
    a request it could read. `side` is one of ENVELOPE_SIDES; validate_envelope()
    is what holds the column to that vocabulary and to the two sides, so a
    declaration reaching here has both cells.
    """
    return [{**field, "required": field["appears"][side] == ENVELOPE_ALWAYS}
            for field in envelope_fields(proto)
            if field["appears"][side] != ENVELOPE_NEVER]


def envelope_json_keys(proto: dict) -> Tuple[str, ...]:
    """The JSON keys of the declared envelope, as gen_ts compares a field against them."""
    return tuple(field["json"] for field in envelope_fields(proto))


def validate_envelope(proto: dict) -> None:
    """Abort unless [envelope] is a declaration the emitters can act on, and no command shadows it.

    The envelope is the leading part of every reply and of every request with a
    payload, and eight things about it are checked here — each one a way the
    declaration would read as describing the messages while nothing writes or
    types them that way:

    1. The section exists and declares at least one key. An envelope with no
       entry leaves gen_rust and gen_ts writing no leading field and no leading
       line at all, while the firmware still writes the keys.
    2. Every entry is a table carrying the four columns the emitters read: the
       JSON key it travels under, the wire type, the `appears` column, and the
       description a reader is given. An entry short of a JSON key is a key
       nothing can be paired with a value; short of a type, a line neither
       language's emitter can write; short of `appears`, a key whose optionality
       is unknown (item 4); and two entries under one JSON key are one key
       written twice.
    3. Every declared type is one the three language maps carry and one a printf
       conversion writes. The three maps, because gen_rust and gen_ts look the
       name up in their own and a type missing from one degrades to that
       target's untyped value while the source still reads as declared; the
       printf map, because these keys are written by the response builder, so a
       key of no printf form is one no format string can carry — `any` is
       exactly such a type.
    4. Every entry's `appears` column names the two sides and gives each one of
       three values, the whole vocabulary: `always` (every message of that side
       carries the key), `some` (some do and some do not) or `never` (none
       does). This is the column the emitters read to decide a field's
       optionality, on either side: `always` is written required, `some`
       optional, `never` not written for that side. An entry without the column
       leaves both targets writing the key as required on every side — the shape
       the firmware's error replies then fail to deserialize into, since they
       carry `status` alone of these keys (src/test/dispatcher.cpp:55, :75) —
       and a value outside the vocabulary leaves the emitters with nothing to
       decide from. What the column is not held to is *which* of a side's
       messages omit a key of `some`: that is firmware evidence under src/test/
       and not something this file can read (see item 4's counterparts in the
       section's comments).
    5. No command declares a response field under an envelope key, by either
       column. Both, because each emitter pairs a field with the envelope
       differently: a field whose `json` is an envelope key is left to the
       envelope by gen_ts and loses its own declared type and optionality there,
       and a field whose `name` is one would be a second `pub status` in
       gen_rust's response struct.
    6. No command declares a *request* field under a key the request side
       carries. The request types write the request side of this section ahead
       of the fields a command declares for them, exactly as the response types
       write the reply side ahead of theirs, so the same collision is possible on
       the other side — a second `pub cmd` in gen_rust's request struct, a second
       `cmd:` line in gen_ts's request interface. Only the keys of
       `appears.request` that are not `never` are reserved here, because those
       are the lines the request types write; a key of `never` on that side
       writes no line and shadows nothing.
    7. Every entry's JSON key is its section key. The two targets read a
       different column for the same field — gen_rust writes the section key as
       the field name, gen_ts writes the `json` column as the wire key — so a
       pair that differs gives one reply two spellings, one per target, and
       nothing fails: the Rust struct keeps the field it always had while the TS
       interface renames it. No rename buys anything here either, because the
       envelope is written by the response builder and not by a command: the
       section key is the field name and the JSON key, so the two are one name.
    8. No entry declares a `name` column. The section key is the field name
       envelope_fields() hands the emitters, so a `name` column is a second
       answer to the same question, and it is the one that reaches the targets —
       while check 5 above still reserves the section key, so the shadowing it
       catches and the field a target writes would be two different names.

    What is not checked, because nothing in the source states it: whether these
    are the keys the firmware's format strings write. Those copies are still
    written by hand — 46 format strings across src/test/testsys.cpp,
    testcmds.cpp, profile_cmd_handler.cpp, config_cmd_handler.cpp and
    dispatcher.cpp — and comparing them is a review and a device-side check, not
    something this file can read.
    """
    section = proto.get("envelope", {})
    if not isinstance(section, dict):
        print("ERROR: response envelope — [envelope] is not a table of "
              "per-key entries (type / json / description).", file=sys.stderr)
        sys.exit(1)

    problems: List[str] = []
    if not section:
        problems.append("the [envelope] section is missing or declares no key, "
                        "so every generated reply would lose its leading keys")
    for name, entry in section.items():
        if not isinstance(entry, dict):
            problems.append(f"[envelope].{name} is not a table "
                            f"(expected type / json / appears / description)")
            continue
        for column in ("json", "type", "description"):
            if not entry.get(column):
                problems.append(f"[envelope].{name} declares no {column!r}")
        # The `appears` column: one cell per side, each in the one vocabulary the
        # emitters decide a field's optionality from. Checked here rather than
        # left to the emitters, because the two failures below both reach a
        # target as a key written on a side where the firmware does not write it:
        # a missing column and a misspelled side read the same way to an emitter
        # looking a side up, and a value outside the vocabulary is one no
        # comparison in either emitter can be true of.
        appears = entry.get("appears")
        if not isinstance(appears, dict):
            problems.append(f"[envelope].{name} declares no 'appears' column "
                            f"(the key's occurrence per side, e.g. "
                            f"{{ request = \"always\", reply = \"some\" }}) — "
                            f"both targets take optionality from it, so without "
                            f"it they write the key as required on every side")
        else:
            for side in ENVELOPE_SIDES:
                if side not in appears:
                    problems.append(f"[envelope].{name}.appears declares no "
                                    f"{side!r} cell — the same question is asked "
                                    f"of every key on both sides, and a side "
                                    f"left out is one the targets write the key "
                                    f"on without an answer to it")
                elif appears[side] not in ENVELOPE_APPEARANCES:
                    problems.append(f"[envelope].{name}.appears declares "
                                    f"{side} = {appears[side]!r}, which is not "
                                    f"one of {list(ENVELOPE_APPEARANCES)} — the "
                                    f"value is what tells the targets to write "
                                    f"the key required, optional, or not at all "
                                    f"on that side")
            unknown = sorted(set(appears) - set(ENVELOPE_SIDES))
            if unknown:
                problems.append(f"[envelope].{name}.appears names {unknown}, "
                                f"which is not a side a message has "
                                f"(sides: {list(ENVELOPE_SIDES)}) — a "
                                f"misspelled side leaves the cell it was meant "
                                f"to be the targets never read")
        # The section key is the field name a target writes, so the JSON key a
        # reply travels under is the same name — gen_rust reads the section key
        # and gen_ts the column, and a pair that differs is one field under two
        # spellings, one per target, with neither emitter reporting it.
        if entry.get("json") and entry["json"] != name:
            problems.append(f"[envelope].{name} declares json = {entry['json']!r}, "
                            f"a key of its own where its section key is already "
                            f"{name!r} — gen_rust writes the field as {name!r} "
                            f"while gen_ts writes the key {entry['json']!r}, so "
                            f"the two targets would name one field differently")
        # The section key is already this entry's field name; a `name` column is
        # a second answer, and the one envelope_fields() would hand the targets —
        # while the shadow check below goes on reserving the section key.
        if "name" in entry:
            problems.append(f"[envelope].{name} declares a 'name' column "
                            f"({entry['name']!r}) — its section key is the field "
                            f"name every target writes, so the column is a "
                            f"second name for one field and the one gen_rust "
                            f"would write while the reserved-name check below "
                            f"still reserves {name!r}")

    entries = [entry for entry in section.values() if isinstance(entry, dict)]
    json_keys = [entry["json"] for entry in entries if entry.get("json")]
    duplicates = sorted({key for key in json_keys if json_keys.count(key) > 1})
    if duplicates:
        problems.append(f"two envelope entries travel under the same JSON "
                        f"key(s): {duplicates}")
    declared_types = sorted({entry["type"] for entry in entries if entry.get("type")})
    unmapped = sorted(t for t in declared_types
                      if any(t not in table for _, table in TYPE_MAPS))
    if unmapped:
        problems.append(f"envelope type(s) the language type maps do not carry: "
                        f"{unmapped}")
    no_printf = sorted(t for t in declared_types if t not in PRINTF_TYPE_MAP)
    if no_printf:
        problems.append(f"envelope type(s) no printf conversion writes: "
                        f"{no_printf} — the response builder writes these keys, "
                        f"so a key of such a type is one no reply can carry "
                        f"(types a conversion writes: {sorted(PRINTF_TYPE_MAP)})")

    reserved_json, reserved_name = set(json_keys), set(section)
    offenders = [
        f"{cmd['domain']}.{cmd['name']}:{field.get('name')} — "
        + (f"its json key {field.get('json')!r} is an envelope key"
           if field.get("json") in reserved_json
           else "its field name is an envelope field name")
        for cmd in proto.get("commands", [])
        for field in cmd.get("response", [])
        if field.get("json") in reserved_json or field.get("name") in reserved_name
    ]
    if offenders:
        problems.append(f"a command declares a response field the envelope "
                        f"already writes: {offenders}. The envelope "
                        f"({', '.join(sorted(reserved_json))}) belongs to the response "
                        f"builder; declaring one takes the field's own type and "
                        f"optionality away in the targets that write the "
                        f"envelope separately, and a second field of the same "
                        f"name breaks the target that writes the name it "
                        f"declares.")

    # The other side of the same rule, and the reason it is the request side of
    # the column that decides what is reserved: the request types write the keys
    # a request carries ahead of the fields a command declares for them, exactly
    # as the response types write the reply side ahead of theirs. A key of
    # `appears.request = "never"` writes no line there and shadows nothing, so it
    # is not reserved — the request type is where that key would collide, and it
    # is not written.
    request_names = {name for name, entry in section.items()
                     if isinstance(entry, dict)
                     and isinstance(entry.get("appears"), dict)
                     and entry["appears"].get("request") != ENVELOPE_NEVER}
    request_json = {entry["json"] for name, entry in section.items()
                    if name in request_names and entry.get("json")}
    request_offenders = [
        f"{cmd['domain']}.{cmd['name']}:{field.get('name')} — "
        + (f"its json key {field.get('json')!r} is an envelope key a request carries"
           if field.get("json") in request_json
           else "its field name is an envelope field name a request carries")
        for cmd in proto.get("commands", [])
        for field in cmd.get("request", [])
        if field.get("json") in request_json or field.get("name") in request_names
    ]
    if request_offenders:
        problems.append(f"a command declares a request field the envelope "
                        f"already writes: {request_offenders}. The request side "
                        f"of the envelope "
                        f"({', '.join(sorted(request_json))}) is written by the "
                        f"host ahead of the fields a command declares, so "
                        f"declaring one is a second field of the same name in "
                        f"the request type — a duplicate `pub` in gen_rust's "
                        f"struct and a duplicate key in gen_ts's interface.")

    if problems:
        for problem in problems:
            print(f"ERROR: response envelope — {problem}", file=sys.stderr)
        print(f"       [envelope] declares: "
              f"{ {name: entry for name, entry in section.items()} }",
              file=sys.stderr)
        sys.exit(1)


# The error reply: the keys a reply carries because it failed — the code for
# what went wrong and the sentence saying it. Declared in protocol.toml's
# [error_reply] section, one entry per key, with the columns [envelope] gives
# its keys, so the shape of a reply that failed is stated in the source of truth
# and not only in the firmware's format strings. No emitter in this file writes
# the keys from either section: this one is the contract those hand-written
# copies are read against. validate_error_reply() holds the declaration to what
# a reader of it needs, and reserves its keys against a command that declares
# one — they belong to the error reply builders, so a command field under one
# would be a second answer to where the key on the wire comes from.


def error_reply_fields(proto: dict) -> List[Dict[str, Any]]:
    """The declared error-reply keys, in declaration order, section key as field name.

    The counterpart of envelope_fields(), and deliberately the same shape: the
    section key supplies the field name, and validate_error_reply() refuses an
    entry that declares a `name` column of its own, so the name a reader takes
    from here is the key the section declares.
    """
    return [{**entry, "name": name} for name, entry in proto.get("error_reply", {}).items()]


def error_reply_on_side(proto: dict, side: str) -> List[Dict[str, Any]]:
    """The error-reply keys one side's messages carry, in declaration order.

    The same question the envelope's `appears` column asks, answered in the same
    three words, which is why the same constants serve both: a key whose cell
    for that side is `never` is one that side's messages do not carry, and one
    the reservation in validate_error_reply() therefore holds nothing against
    there. `side` is one of ENVELOPE_SIDES — held to that vocabulary by
    validate_error_reply(), which is also why an entry whose `appears` column is
    missing, or short the cell for a side, is left out of the answer instead of
    raising: the column checks report that fault, and this is asked beside them
    so a declaration with two faults is reported as both rather than as
    whichever one stopped the run.
    """
    return [field for field in error_reply_fields(proto)
            if isinstance(field.get("appears"), dict)
            and field["appears"].get(side) not in (None, ENVELOPE_NEVER)]


def validate_error_reply(proto: dict) -> None:
    """Abort unless [error_reply] is a declaration a reader can act on, and no command shadows it.

    The error reply is the shape a reply takes when the request failed: the
    envelope's `status` says so, and these keys say what went wrong. Six things
    about the section are checked here — the first five the counterparts of what
    validate_envelope() holds its own section to, because the two declare one
    kind of thing in one shape:

    1. The section exists and declares at least one key. An error reply the
       source of truth does not describe is a shape its consumers can only read
       out of the firmware's format strings, which is what this section exists
       to end.
    2. Every entry is a table carrying the four columns a reader needs: the JSON
       key it travels under, the wire type, the `appears` column, and the
       description. An entry short of a JSON key is a key nothing pairs with a
       value, short of a type one no target can type, and short of `appears` one
       whose occurrence is unknown — which is the half of the declaration the
       reservation below is read from.
    3. Every entry's `appears` column names the two sides and gives each one of
       the three words [envelope] documents, read with the same constants: the
       question is the same one, so a word of this section's own would be a
       second vocabulary for it. The sides are the same two messages either way
       — a request carries no error code and no reason.
    4. Every entry's JSON key is its section key. Nothing renames a key on the
       way to the wire, so a pair that differed would be one key under two
       spellings, one per reader.
    5. No entry declares a `name` column. The section key is the field name
       error_reply_fields() hands a reader, so a column of its own is a second
       answer to the same question and the one that would be read.
    6. Every declared type is one the three language maps carry and one a printf
       conversion writes. The type maps, because that is the vocabulary a field
       of this file is declared with, so a name outside them is not a type any
       other declaration here could use either; the printf map, because these
       keys are written by the firmware's own printf format strings, so a key of
       no printf form is one no reply can carry.

    On top of those, and the reason this validator is more than a reader's aid:
    no command may declare a field under one of these keys, by either column, on
    a side the column says the key appears on. The keys belong to the error
    reply builders — a handler's sendError() and the dispatcher's own refusals —
    so a command that declares one is a second answer to where the key on the
    wire comes from, and the two answers disagree: the command's field carries
    its own type and optionality, while the builder writes the key on whichever
    reply failed. Which side is reserved is the `appears` column's business, as
    it is for the envelope: a key of `never` on a side writes no line there and
    shadows nothing, so a column that says a key appears on no request leaves
    the request types free of it.

    What is not checked, for the reason the envelope's validator gives: whether
    these are the keys the firmware's format strings write. Those copies are
    still written by hand — 13 format strings write `error_code` and 15 write
    `reason` — so comparing them is a review and a device-side check, not
    something this file can read.
    """
    section = proto.get("error_reply", {})
    if not isinstance(section, dict):
        print("ERROR: error reply — [error_reply] is not a table of per-key "
              "entries (type / json / appears / description).", file=sys.stderr)
        sys.exit(1)

    problems: List[str] = []
    if not section:
        problems.append("the [error_reply] section is missing or declares no "
                        "key, so the keys every error reply carries would be "
                        "declared nowhere in the source of truth")

    for name, entry in section.items():
        if not isinstance(entry, dict):
            problems.append(f"[error_reply].{name} is not a table "
                            f"(expected type / json / appears / description)")
            continue
        for column in ("json", "type", "description"):
            if not entry.get(column):
                problems.append(f"[error_reply].{name} declares no {column!r}")
        # The `appears` column, in the envelope's three words: what it answers
        # per side is which sides reserve the key below, so a missing or
        # misspelled one is a key reserved against the wrong side's fields —
        # and, for a reader, a key of unknown occurrence.
        appears = entry.get("appears")
        if not isinstance(appears, dict):
            problems.append(f"[error_reply].{name} declares no 'appears' column "
                            f"(the key's occurrence per side, e.g. "
                            f"{{ request = \"never\", reply = \"some\" }}) — "
                            f"without it the key's occurrence is unstated and "
                            f"the reservation below has no side to hold it to")
        else:
            for side in ENVELOPE_SIDES:
                if side not in appears:
                    problems.append(f"[error_reply].{name}.appears declares no "
                                    f"{side!r} cell — the same question is asked "
                                    f"of every key on both sides, and a side "
                                    f"left out is one whose messages the "
                                    f"declaration says nothing about")
                elif appears[side] not in ENVELOPE_APPEARANCES:
                    problems.append(f"[error_reply].{name}.appears declares "
                                    f"{side} = {appears[side]!r}, which is not "
                                    f"one of {list(ENVELOPE_APPEARANCES)} — the "
                                    f"same vocabulary [envelope] declares its "
                                    f"keys in")
            unknown = sorted(set(appears) - set(ENVELOPE_SIDES))
            if unknown:
                problems.append(f"[error_reply].{name}.appears names {unknown}, "
                                f"which is not a side a message has "
                                f"(sides: {list(ENVELOPE_SIDES)}) — a "
                                f"misspelled side leaves the cell it was meant "
                                f"to be never read")
        if entry.get("json") and entry["json"] != name:
            problems.append(f"[error_reply].{name} declares json = "
                            f"{entry['json']!r}, a key of its own where its "
                            f"section key is already {name!r} — the section key "
                            f"is the name a reader pairs with this entry, and "
                            f"nothing renames a key on the way to the wire")
        if "name" in entry:
            problems.append(f"[error_reply].{name} declares a 'name' column "
                            f"({entry['name']!r}) — its section key is the field "
                            f"name error_reply_fields() hands a reader, so the "
                            f"column is a second name for one key and the one "
                            f"that would be read")

    entries = [entry for entry in section.values() if isinstance(entry, dict)]
    json_keys = [entry["json"] for entry in entries if entry.get("json")]
    duplicates = sorted({key for key in json_keys if json_keys.count(key) > 1})
    if duplicates:
        problems.append(f"two error-reply entries travel under the same JSON "
                        f"key(s): {duplicates}")
    declared_types = sorted({entry["type"] for entry in entries if entry.get("type")})
    unmapped = sorted(t for t in declared_types
                      if any(t not in table for _, table in TYPE_MAPS))
    if unmapped:
        problems.append(f"error-reply type(s) the language type maps do not "
                        f"carry: {unmapped} — a type name outside that "
                        f"vocabulary is not one any other declaration in this "
                        f"file could use either")
    no_printf = sorted(t for t in declared_types if t not in PRINTF_TYPE_MAP)
    if no_printf:
        problems.append(f"error-reply type(s) no printf conversion writes: "
                        f"{no_printf} — the firmware writes these keys through "
                        f"its own format strings, so a key of such a type is "
                        f"one no error reply can carry (types a conversion "
                        f"writes: {sorted(PRINTF_TYPE_MAP)})")

    # The keys are the error reply builders', not a command's, on whichever side
    # the column says they appear: the reserved set is what that side carries,
    # so a key of `never` there reserves nothing. Reported per side, because the
    # two sides are two different arrays in the TOML and two different types in
    # the targets.
    for field_side, message_side in (("request", "request"), ("response", "reply")):
        carried = error_reply_on_side(proto, message_side)
        if not carried:
            continue
        reserved_json = {field["json"] for field in carried if field.get("json")}
        reserved_name = {field["name"] for field in carried}
        offenders = [
            f"{cmd['domain']}.{cmd['name']}:{field.get('name')} — "
            + (f"its json key {field.get('json')!r} is an error-reply key"
               if field.get("json") in reserved_json
               else "its field name is an error-reply field name")
            for cmd in proto.get("commands", [])
            for field in cmd.get(field_side, [])
            if field.get("json") in reserved_json or field.get("name") in reserved_name
        ]
        if offenders:
            problems.append(
                f"a command declares a {field_side} field the error reply "
                f"already writes: {offenders}. The error reply "
                f"({', '.join(sorted(reserved_json))}) is written by the error "
                f"reply builders — a handler's sendError() and the dispatcher's "
                f"own refusals — and not by a command: a field declared here is "
                f"a second answer to where the key on the wire comes from, with "
                f"the command's type and optionality where the builder writes "
                f"the key on whichever reply failed.")

    if problems:
        for problem in problems:
            print(f"ERROR: error reply — {problem}", file=sys.stderr)
        print(f"       [error_reply] declares: {section}", file=sys.stderr)
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

    The exception list's own names are reconciled with the vocabulary and with
    the side it excepts, because nothing above reads them: every name in
    PRINTF_LESS_TYPES has to be a type the three tables map — a name they do
    not map is not a type a field can be declared with, so the entry is a
    misspelling that no lookup elsewhere meets — and it has to be a type some
    response field names, because an entry no response field draws on is a
    decision about a type that is no longer written, left standing exactly
    where a reader of the response side looks for what has no printf form.
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

    # The exception list's own names, against the two things an entry can be
    # wrong about: whether the name is a type at all, and whether the response
    # side still declares a field of it. Neither is visible from the lists
    # above, which look a *used* type up in the four maps and never read the
    # exception list itself — a name in it is met by no lookup but gen_resp()'s,
    # so a misspelling sits there unnamed. All three tables, because that is
    # what a type a field may be declared with has to be in; the response side,
    # because a response field is the one thing the list is an exception for.
    not_a_type = [t for t in sorted(PRINTF_LESS_TYPES)
                  if any(t not in table for _, table in TYPE_MAPS)]
    no_field = [t for t in sorted(PRINTF_LESS_TYPES) if t not in response]
    if not_a_type or no_field:
        if not_a_type:
            print(f"ERROR: PRINTF_LESS_TYPES names types the type maps do not "
                  f"carry: {not_a_type}", file=sys.stderr)
            print("       A field's type is read out of those tables, so a name "
                  "they do not map is not a type protocol.toml can be declared "
                  "with: the entry excepts nothing and no other check reads it.",
                  file=sys.stderr)
        if no_field:
            print(f"ERROR: PRINTF_LESS_TYPES names types no response field "
                  f"declares: {no_field}", file=sys.stderr)
            print("       The list is the response side's exception — the types "
                  "no printf conversion writes — so an entry no response field "
                  "draws on excepts nothing: it reads as a decision about a "
                  "type while the field it was written for is gone.",
                  file=sys.stderr)
        print(f"       PRINTF_LESS_TYPES: {list(PRINTF_LESS_TYPES)}",
              file=sys.stderr)
        print(f"       Types the type maps carry: "
              f"{sorted(set().union(*(set(t) for _, t in TYPE_MAPS)))}",
              file=sys.stderr)
        print(f"       Types a response field declares: {sorted(response)}",
              file=sys.stderr)
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
    every check downstream follows the declaration and agrees with it — a
    `role` is one of the reasons omission_reason() honours, and
    field_coverage_errors() takes a reason as the license to leave the field
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


def omission_reason(field: Dict[str, Any]) -> Optional[str]:
    """Why protocol.toml says a field may be left out of an emitted list, or None.

    Each of the three is written on the field itself in the source, so an
    emitter that drops the field is following a declaration rather than making a
    choice nothing else can see:

      ``role``               the field is conditional, and a view of its side
                             that excludes it is the design
      ``omit_in_serialize``  the field is not part of the shape that travels
      ``json = ""``          the field has no key to be written under

    Returned as the declaration quoted back rather than as a flag, because what
    a caller reports is that the field carries nothing licensing its absence.
    """
    if "role" in field:
        return f"role = {field['role']!r}"
    if field.get("omit_in_serialize"):
        return "omit_in_serialize"
    if not field.get("json"):
        return 'json = ""'
    return None


def field_coverage_errors(owner: str, side: str, declared: List[dict],
                          emitted: List[str]) -> List[str]:
    """Every way one emitted field list fails to cover the declarations behind it.

    ``owner`` names what the fields belong to — a command as ``"<domain>.<name>"``
    or a shared type as ``"type <Name>"`` — and ``side`` what the list is a view
    of: ``"request"`` / ``"response"`` for a command, ``"struct"`` / ``"serialize"``
    / ``"deserialize"`` / ``"interface"`` for a type. Both are carried into every
    problem reported: two views that lost the same field are not the same loss,
    and a list that dropped one reads identically either way — a request field
    dropped is a command that no longer reads an argument it declares, a response
    field dropped a response that no longer carries a value, a type field dropped
    from a serialization a value the wire never writes.

    ``declared`` is the array as the TOML spells it, and ``emitted`` the field
    names an emitter derived from it — the two halves are taken from different
    places on purpose, the first from the source and the second from the
    emitter's own output, so the comparison can come out equal only when the
    emitter kept every field. A field the emitter left out is not a loss when
    the field itself carries a reason: omission_reason() is that test, and a
    field with no reason has nothing that could excuse its absence. That is the
    case neither the TOML nor a digest can see — an emitter that drops such a
    field writes a manifest, a response table, a docs table and language
    bindings that agree with each other and are all short the same field, and
    every consumer of them loses it with them.

    A side the owner does not have is not a case here: an empty ``declared``
    list has nothing to be short of, and the comparison comes out equal.

    The other direction is checked for the same reason: a field emitted under a
    name the TOML does not declare is a key no consumer can pair with a value,
    arriving on the wire as a field the protocol never had.
    """
    declared_names = {f["name"] for f in declared}
    emitted_names = set(emitted)
    problems = [f"{owner}: {side} field '{f['name']}' declared but never emitted, "
                f"and carries no reason (`role` / `omit_in_serialize` / empty "
                f"`json`) to be left out"
                for f in declared
                if f["name"] not in emitted_names and omission_reason(f) is None]
    problems += [f"{owner}: {side} field '{name}' emitted but not declared"
                 for name in emitted if name not in declared_names]
    return problems


def fail_uncovered_fields(problems: List[str]) -> None:
    """Report the declared fields an emitter left uncovered and stop.

    One exit for both callers — a pre-flight validator and an emitter that built
    its own field list — so a gap reads the same wherever it is found.
    """
    if not problems:
        return
    for problem in problems:
        print(f"ERROR: emitter coverage — {problem}", file=sys.stderr)
    print("       A declared field has to reach every emitted field list derived "
          "from it unless the field itself carries a reason to be left out "
          "(`role`, `omit_in_serialize` or an empty `json` key); one filtered out "
          "of an emitter is lost by every artifact derived from it, and no "
          "consumer of them can tell.", file=sys.stderr)
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

    What runs here is the manifest emitter's half. The code emitters — gen_cpp,
    gen_rust, gen_ts — derive field lists of their own, each with filters of its
    own (a field marked ``omit_in_serialize`` or carrying no JSON key, the
    response envelope gen_ts writes ahead of the declared fields), so each of
    them calls field_coverage_errors() on the list it is about to write, from
    inside itself. Comparing them against a list re-derived here would be a
    second copy of every filter, which is the shape this file exists to remove:
    the check has to read the list the emitter writes, not a description of it.
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


def validate_command_error_codes(proto: dict) -> None:
    """Abort unless every code a command declares is a key of [error_codes], at that key's number.

    A command's `error_codes` array is the half of the source of truth that says
    which failures *that* command's reply can carry, against [error_codes],
    which gives every code its one name and its one number. Two things are
    checked of each entry, which are the two ways the array and the table can
    drift apart: the entry's `name` has to be a key of the table, and its `code`
    has to be the number that key carries. Both, because either drifts alone —
    a name renamed in the table leaves a command pointing at a code that no
    longer exists, and a number edited in one of the two places leaves one code
    name meaning two things, depending on which reader is asked.

    The failure mode this closes is silent by construction: nothing consumes
    these arrays. The emitters write the global table — proto.h's error codes,
    proto.rs's, types.ts's ErrorCode enum and the manifest alike read
    `proto["error_codes"]` and only that — so an entry naming a code that does
    not exist, or naming one with another code's number, reaches no artifact,
    no build and no reader. It sits in the TOML reading as a permission the
    command does not have, and every consumer of the protocol stays green,
    because none of them ever looks at it.

    What this deliberately does not do, in either direction: it does not
    compare the arrays against a second copy of the table. Copying every
    command's entries into [error_codes] (or the table's entries into every
    command) would make a second place that has to be kept in step with the
    first — which is the fault being checked for, not the check. The table
    stays the only place a code is named and numbered, and an array only ever
    refers to it; an entry declaring a column this check does not read is left
    to whatever reads it, which today is nothing.

    It also says nothing about *which* codes a command declares. Whether
    sys.ping can answer ERR_NOT_SUPPORTED, or whether config.save can answer
    the codes its handler writes, is a claim about the firmware's handlers:
    a code the table carries is one the array can name, and no text in this
    file says which of them a reply actually carries. So an array that omits a
    code a handler returns, or names one it never returns, passes here — as it
    must, since there is nothing in the TOML to hold that claim against. What
    is compared is the array against the table, and the only two answers are
    whether the name exists and whether the number agrees.
    """
    table = proto.get("error_codes", {})
    if not isinstance(table, dict):
        table = {}

    problems: List[str] = []
    for cmd in proto.get("commands", []):
        owner = f"{cmd.get('domain')}.{cmd.get('name')}"
        for entry in cmd.get("error_codes", []):
            if not isinstance(entry, dict):
                problems.append(f"{owner}: an error_codes entry is not a table "
                                f"({entry!r}) — expected name / code, the shape "
                                f"[error_codes] declares its codes in")
                continue
            name = entry.get("name")
            if not name:
                problems.append(f"{owner}: an error_codes entry declares no "
                                f"name, so the code it refers to is unnamed and "
                                f"cannot be held to the table")
                continue
            if name not in table:
                problems.append(f"{owner}: declares error code {name!r}, which "
                                f"[error_codes] does not carry (declared: "
                                f"{sorted(table)}) — the array names codes, and "
                                f"a name the table does not have refers to a code "
                                f"no target and no reader knows")
                continue
            declared = table[name].get("code") if isinstance(table[name], dict) else None
            code = entry.get("code")
            if not isinstance(code, int) or isinstance(code, bool):
                problems.append(f"{owner}: {name} declares no integer code "
                                f"({code!r}), while [error_codes].{name} carries "
                                f"{declared!r} — a copy of that number which states "
                                f"none cannot be held to it")
            elif code != declared:
                problems.append(f"{owner}: {name} is declared with code {code}, "
                                f"while [error_codes].{name} carries {declared} — "
                                f"one code name has to mean one number, and the "
                                f"table is where it is given")

    if problems:
        for problem in problems:
            print(f"ERROR: command error codes — {problem}", file=sys.stderr)
        print("       A command's `error_codes` array names the codes its reply "
              "can carry, so every entry has to be a key of [error_codes] at the "
              "number that key carries. Nothing emits the arrays, so an entry "
              "that drifts — a name the table does not have, or a number the "
              "table gives another name — reads as a permission the command does "
              "not have and no artifact or reader would ever show it.",
              file=sys.stderr)
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
        # Every declared field is carried, and the list is built before the
        # lines are written so the coverage check reads the list this emitter
        # writes rather than a description of it.
        struct_fields = list(t["fields"])
        fail_uncovered_fields(field_coverage_errors(
            f"type {name}", "struct", t["fields"],
            [f["name"] for f in struct_fields]))
        for f in struct_fields:
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
        # A field is written unless protocol.toml says it is not part of the
        # serialized shape (`omit_in_serialize`) or gives it no key to be written
        # under (`json = ""`). Built before the writes and compared against the
        # declared array: a filter added here is a field the binding stops
        # carrying, which the check reports instead of leaving it silent.
        serialize_fields = [f for f in t["fields"]
                            if f["json"] and not f.get("omit_in_serialize")]
        fail_uncovered_fields(field_coverage_errors(
            f"type {name}", "serialize", t["fields"],
            [f["name"] for f in serialize_fields]))
        for f in serialize_fields:
            sw(f'        obj["{f["json"]}"] = v.{f["name"]};')
        sw("    }")
        sw()

        # Deserialize
        sw(f"    // Deserialize {name} from a JsonDocument")
        sw(f"    inline static void deserialize{name}(const JsonDocument &doc, {fq} &v) {{")
        # The same reading as the serialized shape and a list of its own: the two
        # loops are separate, so a filter that reaches only one of them is a
        # filter only one of the two checks can report.
        deserialize_fields = [f for f in t["fields"]
                              if f["json"] and not f.get("omit_in_serialize")]
        fail_uncovered_fields(field_coverage_errors(
            f"type {name}", "deserialize", t["fields"],
            [f["name"] for f in deserialize_fields]))
        for f in deserialize_fields:
            json_key = f["json"]
            field_name = f["name"]
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
    # A message's leading fields, as protocol.toml declares them, so this emitter
    # writes a request struct's and a response struct's first fields from the
    # source of truth — envelope_on_side() per side, called where each is
    # written.

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
        # Every declared field is carried, and the list is built before the lines
        # so the coverage check reads what this emitter writes.
        struct_fields = list(t["fields"])
        fail_uncovered_fields(field_coverage_errors(
            f"type {name}", "struct", t["fields"],
            [f["name"] for f in struct_fields]))
        for f in struct_fields:
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
        # A request carries the request side of [envelope] — the keys
        # protocol.toml declares as appearing in one — ahead of the fields the
        # command declares, so these lines come from that section like the
        # response struct's leading fields do, and the column decides their
        # optionality: a key of `appears.request = "always"` is required here,
        # one of `"some"` optional, and one of `"never"` is not written at all.
        for f in envelope_on_side(proto, "request"):
            t = RUST_TYPE_MAP[f["type"]]
            if f["required"]:
                w(f"    pub {rust_ident(f['name'])}: {t},")
            else:
                w(f"    pub {rust_ident(f['name'])}: Option<{t}>,")
        # Each declared request field becomes a field of the struct; the list is
        # built and compared before the lines are written, so a filter added here
        # is reported rather than leaving the binding short a field.
        request_fields = list(req)
        fail_uncovered_fields(field_coverage_errors(
            f"{cmd_info['domain']}.{cmd_info['name']}", "request", req,
            [f["name"] for f in request_fields]))
        for f in request_fields:
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
        # The envelope leads every response struct — the keys the response
        # builder writes, in the order protocol.toml declares them, read from
        # that section rather than from a literal of this emitter's own, and
        # with the optionality its `appears` column gives them: a key every
        # reply carries (`status`) is written plain, one only some replies carry
        # (`cmd`, `queued` — the error replies have neither) as Option, so the
        # struct deserializes the replies the firmware really sends.
        for f in envelope_on_side(proto, "reply"):
            t = RUST_TYPE_MAP[f["type"]]
            if f["required"]:
                w(f"    pub {rust_ident(f['name'])}: {t},")
            else:
                w(f"    pub {rust_ident(f['name'])}: Option<{t}>,")
        # Each declared response field becomes an optional field of the struct —
        # every one of them, so the list is the declared array and the check
        # holds it there.
        response_fields = list(resp)
        fail_uncovered_fields(field_coverage_errors(
            f"{domain}.{name}", "response", resp,
            [f["name"] for f in response_fields]))
        for f in response_fields:
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

# The response envelope is declared in protocol.toml ([envelope]) and read out of
# it by envelope_on_side(), so the request types and the response types of
# gen_rust and gen_ts write a message's leading keys from the source of truth
# instead of each carrying a literal copy of them — and so the `appears` column
# decides, per side, whether a key is required, optional or not written there.
# See validate_envelope() for what the declaration is held to.


def gen_ts(proto: dict, out: Optional[Path] = None) -> str:
    lines: List[str] = []
    types = proto.get("types", [])
    enums = proto.get("enums", [])
    commands = proto.get("commands", [])
    error_codes = proto.get("error_codes", {})
    # The reply's leading keys as gen_ts compares a declared field against them:
    # a field whose key the envelope already writes is left to the envelope.
    envelope_keys = envelope_json_keys(proto)

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
        # A field marked `omit_in_serialize` is not part of the shape that
        # travels. Built before the lines are written and compared against the
        # declared array, so a filter added here is reported.
        interface_fields = [f for f in t["fields"] if not f.get("omit_in_serialize")]
        fail_uncovered_fields(field_coverage_errors(
            f"type {name}", "interface", t["fields"],
            [f["name"] for f in interface_fields]))
        for f in interface_fields:
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
            # The request's leading keys come from [envelope]'s request side, not
            # from two lines written here — and a key of `appears.request =
            # "some"` would be written optional, the same rule the response
            # interface below applies to the reply side.
            for f in envelope_on_side(proto, "request"):
                optional = "" if f["required"] else "?"
                w(f"  {f['json']}{optional}: {TS_TYPE_MAP[f['type']]};")
            # Each declared request field becomes a property of the interface;
            # the list is built and compared before the lines are written, so a
            # filter added here is reported rather than leaving the binding short
            # an argument.
            request_fields = list(req)
            fail_uncovered_fields(field_coverage_errors(
                full_name, "request", req,
                [f["name"] for f in request_fields]))
            for f in request_fields:
                ts_type = TS_TYPE_MAP.get(f["type"], "any")
                optional = "" if f.get("required", False) else "?"
                w(f"  {f['json']}{optional}: {ts_type};")
            w("}")
        else:
            # A command with no request fields still has a request, and its
            # leading keys are the envelope's request side — the same source as
            # the interface above, so the inline shape cannot drift from it.
            head = " ".join(
                f"{f['json']}{'?' if not f['required'] else ''}: {TS_TYPE_MAP[f['type']]};"
                for f in envelope_on_side(proto, "request"))
            w(f"export type {to_pascal(domain)}{to_pascal(name)}Request = {{ {head} }};")
        w()

        # Response interface
        resp = cmd_info.get("response", [])
        if resp:
            w(f"export interface {to_pascal(domain)}{to_pascal(name)}Response {{")
            # The reply's leading keys, from [envelope]'s reply side: a key every
            # reply carries is written plain and one only some replies carry
            # optional (`cmd?`, `queued?` — the error replies carry `status`
            # alone of these).
            for f in envelope_on_side(proto, "reply"):
                optional = "" if f["required"] else "?"
                w(f"  {f['json']}{optional}: {TS_TYPE_MAP[f['type']]};")
            # A field whose key the envelope above writes is left to it; every
            # other declared field gets an optional line of its own. The list is
            # built before the lines are written, and what the interface carries
            # is taken from the two places it writes keys — so a field dropped
            # from the list is reported instead of being noticed in the output.
            # (validate_envelope() refuses such a declaration, so the first half
            # of the coverage input is empty in a protocol that generates.)
            fields = [f for f in resp if f["json"] not in envelope_keys]
            fail_uncovered_fields(field_coverage_errors(
                full_name, "response", resp,
                [f["name"] for f in resp if f["json"] in envelope_keys]
                + [f["name"] for f in fields]))
            for f in fields:
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
    w("# CDC response field tables (derived from `protocol/protocol.toml`)")
    w()
    w("The output of `python3 scripts/gen_proto.py --target fields-md`: one table per\n"
      "command that carries a response payload. Field names (the JSON keys), types and\n"
      "order are taken verbatim from the `response` arrays in `protocol.toml` — a type\n"
      "is spelled as the TOML declares it (`u32` / `string` / …) rather than translated,\n"
      "so the two can be compared word for word; the order is the order the fields are\n"
      "written in.")
    w()
    w("**This file is a derivative; editing it does nothing.** Change `protocol.toml`\n"
      "and run the generator again.")
    w()
    w(f"Protocol version: `{proto.get('meta', {}).get('version', '')}`")
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
        w("| Field | Type | Source or note |")
        w("|------|------|--------------|")
        for row in rows:
            w(f"| `{row['json']}` | {row['type']} | {row['note']} |")
        w()

    empty = [f"{c['domain']}.{c['name']}" for c in commands if not c.get("response")]
    if empty:
        w("## Commands with no response payload")
        w()
        # The keys named here are the envelope as protocol.toml declares it, not
        # three names written into this sentence: a reply without payload still
        # carries them, and which keys those are is the section's business.
        envelope_keys = "`" + "` / `".join(f["json"] for f in envelope_fields(proto)) + "`"
        w("These commands declare an empty `response`: their reply carries the envelope\n"
          f"fields only ({envelope_keys}):")
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
    validate_envelope(proto)
    validate_error_reply(proto)
    validate_types(proto)
    validate_field_roles(proto)
    validate_field_coverage(proto)
    validate_command_error_codes(proto)

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
