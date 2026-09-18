"""
The protocol source of truth, read: the vocabulary every other module draws on.

protocol.toml declares the messages; this module is what the rest of the
generator reads them through — the three type maps plus PRINTF_TYPE_MAP, the
role registry and the presence it derives, the [envelope] and [error_reply]
readers, and the helpers the emitters and the validators both use
(fail, omission_reason, field_coverage_errors, fail_uncovered_fields,
command_fields, optional_flag).

Moved out of scripts/gen_proto.py without a change to any of it: the module
split is mechanical, and nothing here behaves differently for having a file of
its own. The one thing added since is fail(): the exit every error site in the
generation run goes through, so no validator and no emitter writes a print and
an exit of its own.
"""
import hashlib
import re
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


# ═════════════════════════════════════════════════════════════════════════════
# Stopping
# ═════════════════════════════════════════════════════════════════════════════

def fail(*lines: str) -> None:
    """Report a failure — one line, or several — and stop the run with exit 1.

    Every line reaches stderr as it stands, in the order given, and the run
    then ends non-zero. The text is the caller's, and that is deliberate: the
    `ERROR: ` prefix a report's subject line carries and the seven-space indent
    of the detail lines under it are part of the strings the call site builds,
    so they are spelled where the text is known rather than added here.

    Because the reports are not all one line and not all one problem. A
    validator that collected several problems writes one prefixed line per
    problem and one indented note under them all (validate_envelope,
    validate_types, validate_command_error_codes); the coverage check writes
    indented notes alone (fail_uncovered_fields); a validator with a single
    fault writes the one line that states it (validate_domains). No prefix this
    function added could leave all of those as they are — and they are read
    both by a person and, word for word, by the suite's negative cases, so the
    shape of a failure is decided once, at the site that knows it.

    What every call site shares is this: its lines reach stderr and the run
    stops here, so reporting an error is one call and nothing else.
    """
    for line in lines:
        print(line, file=sys.stderr)
    sys.exit(1)


# ── Try tomllib (3.11+), fallback to tomli ──────────────────────────────────
try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        fail("ERROR: Python 3.11+ (stdlib tomllib) or 'tomli' pip package "
             "required.")


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
REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_SOURCES = ("scripts/gen_proto.py",
                     "scripts/proto_gen/__init__.py",
                     "scripts/proto_gen/model.py",
                     "scripts/proto_gen/validate.py",
                     "scripts/proto_gen/emit_cpp.py",
                     "scripts/proto_gen/emit_rust.py",
                     "scripts/proto_gen/emit_ts.py",
                     "scripts/proto_gen/emit_resp.py",
                     "scripts/proto_gen/emit_fields.py")


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
    fail(*[f"ERROR: emitter coverage — {problem}" for problem in problems],
         "       A declared field has to reach every emitted field list derived "
         "from it unless the field itself carries a reason to be left out "
         "(`role`, `omit_in_serialize` or an empty `json` key); one filtered out "
         "of an emitter is lost by every artifact derived from it, and no "
         "consumer of them can tell.")


def command_fields(entries: List[dict]) -> List[Dict[str, Any]]:
    """One command's field array as a list of plain, ordered records.

    Keeps only what describes the wire shape (name / type / json, plus the
    `required`, `optional`, `description` and `role` the TOML happens to carry),
    so a consumer needs no TOML parser and no knowledge of this file's other
    tables.

    `optional` is written when the TOML carries it and not as a default, so a
    field that declares nothing records nothing: a consumer reads the absent
    column as required, which is what every declared response field was before
    the column existed, and the manifest of a protocol that declares no
    optionality is the one it always was. It is written as a boolean for the
    reason validate_field_optionality() insists the TOML's value be one: a
    consumer tests it for truth.
    """
    out_fields: List[Dict[str, Any]] = []
    for f in entries:
        record: Dict[str, Any] = {"name": f["name"], "type": f["type"], "json": f["json"]}
        if "required" in f:
            record["required"] = bool(f["required"])
        if "optional" in f:
            record["optional"] = bool(f["optional"])
        if "description" in f:
            record["description"] = f["description"]
        if "role" in f:
            record["role"] = f["role"]
        out_fields.append(record)
    return out_fields


def optional_flag(domain: str, name: str, field: str) -> str:
    """The name of the compile-time flag a response field declared `optional` carries.

    One flag per field, so the write site the declaration is about can name it:
    what the flag carries is "a reply of this command may leave this key out",
    and it is the whole of what this emitter can derive from the column — the
    rule saying *when* the key is left out is firmware prose, written where the
    write is (protocol.toml, [commands] config.save). Naming it at that site is
    what holds the declaration to the code, and it is why the name is derived
    here rather than spelled there: an `optional = true` that comes off the
    field takes the flag with it and stops that site from compiling
    (src/test/config_cmd_handler.cpp).
    """
    return (f"THETAGP_RESP_OPTIONAL_{domain.upper()}_"
            f"{name.replace('-', '_').upper()}_{field.upper()}")
