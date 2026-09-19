"""
Every check that stops generation, and the messages it stops with.

One function per thing the source of truth has to be true of before an artifact
is written: the domains, the two declared message shapes ([envelope],
[error_reply]), the type vocabulary, the field roles, the optionality columns,
the declared-but-never-emitted fields, the coverage of every emitter's own
field lists, and the per-command error-code arrays. Each one reports what it
found through model.fail() — the lines it collected, then exit non-zero, in one
place for the whole run; main() calls them all before the first emitter runs,
so a protocol the artifacts cannot describe is not written at all.

Moved out of scripts/gen_proto.py without a change to any check or any message:
the suite's negative cases match these strings word for word.
"""
from typing import List

from proto_gen.model import (
    ENVELOPE_APPEARANCES,
    ENVELOPE_NEVER,
    ENVELOPE_SIDES,
    NON_COMMAND_DOMAINS,
    PRINTF_LESS_TYPES,
    PRINTF_TYPE_MAP,
    ROLE_SWITCHES,
    TYPE_MAPS,
    command_fields,
    error_reply_on_side,
    fail,
    fail_uncovered_fields,
    field_coverage_errors,
)

def validate_domains(proto: dict) -> None:
    """Abort unless [domains] covers every [[commands]].domain and describes no unknown domain."""
    declared, used = set(proto.get("domains", {})), {c.get("domain") for c in proto.get("commands", [])}
    miss, unk = sorted(used - declared), sorted(declared - used - NON_COMMAND_DOMAINS)
    absent = sorted(NON_COMMAND_DOMAINS - declared)
    if miss or unk or absent:
        fail(f"ERROR: [domains] mismatch — used-by-commands-but-undeclared: {miss or 'none'}; declared-but-unknown: {unk or 'none'}; registered-non-command-but-undeclared: {absent or 'none'}")


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
        fail("ERROR: response envelope — [envelope] is not a table of "
             "per-key entries (type / json / description).")

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
        fail(*[f"ERROR: response envelope — {problem}" for problem in problems],
             f"       [envelope] declares: "
             f"{ {name: entry for name, entry in section.items()} }")


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
        fail("ERROR: error reply — [error_reply] is not a table of per-key "
             "entries (type / json / appears / description).")

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
        fail(*[f"ERROR: error reply — {problem}" for problem in problems],
             f"       [error_reply] declares: {section}")


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
        # Built in the order the lines are read: one prefixed line per problem,
        # then the notes under them — which is the shape these reports have
        # always had, and the order the checks find them in.
        report: List[str] = []
        for name, missing in unmapped:
            if missing:
                report.append(f"ERROR: {name} has no mapping for type(s): {missing}")
        if overlap:
            report.append(f"ERROR: type(s) in both PRINTF_TYPE_MAP and "
                          f"PRINTF_LESS_TYPES: {overlap}")
            report.append("       PRINTF_LESS_TYPES names the types no printf "
                          "conversion writes, so a type it names does not belong in "
                          "PRINTF_TYPE_MAP: a response field of such a type would be "
                          "written through the mapping, which is the table the "
                          "exception list says cannot be written. Drop it from one of "
                          "the two.")
        if no_printf:
            report.append(f"ERROR: PRINTF_TYPE_MAP has no mapping for type(s): {no_printf}")
        report.append(f"       Types used by protocol.toml: {used}")
        report.append(f"       Mapped types: {sorted(set().union(*(set(t) for _, t in TYPE_MAPS)))}")
        report.append("       A type a table does not map is emitted as that target's "
                      "untyped value (JsonVariant / serde_json::Value / any) instead of "
                      "the declared one; add the mapping or fix the type name.")
        if no_printf:
            report.append("       PRINTF_TYPE_MAP is the response side's fourth table: a "
                          "response field is written through a printf conversion, so a "
                          "field of a type it does not map leaves its command no "
                          "response table (gen_resp() names it in the header) and the "
                          "build still succeeds.")
            report.append(f"       Types with no printf form at all, not a gap in it: "
                          f"{list(PRINTF_LESS_TYPES)}")
        fail(*report)

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
        report: List[str] = []
        if not_a_type:
            report.append(f"ERROR: PRINTF_LESS_TYPES names types the type maps do not "
                          f"carry: {not_a_type}")
            report.append("       A field's type is read out of those tables, so a name "
                          "they do not map is not a type protocol.toml can be declared "
                          "with: the entry excepts nothing and no other check reads it.")
        if no_field:
            report.append(f"ERROR: PRINTF_LESS_TYPES names types no response field "
                          f"declares: {no_field}")
            report.append("       The list is the response side's exception — the types "
                          "no printf conversion writes — so an entry no response field "
                          "draws on excepts nothing: it reads as a decision about a "
                          "type while the field it was written for is gone.")
        report.append(f"       PRINTF_LESS_TYPES: {list(PRINTF_LESS_TYPES)}")
        report.append(f"       Types the type maps carry: "
                      f"{sorted(set().union(*(set(t) for _, t in TYPE_MAPS)))}")
        report.append(f"       Types a response field declares: {sorted(response)}")
        fail(*report)


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
        fail(*[f"ERROR: unregistered field role — {entry}" for entry in unregistered],
             "       Registered roles, each with the compile switch the "
             f"generated artifacts carry for it: {ROLE_SWITCHES}",
             "       A role is a name this generator and the artifacts it "
             "writes agree on; register it in ROLE_SWITCHES with the switch "
             "it decides, or drop the tag from the field.",
             "       Not checked, and not checkable here: whether the field "
             "should carry a role at all. A role is what lets an emitter "
             "leave a field out of its lists, so a registered role on a field "
             "that does not vary with it reads as conditional to every check "
             "downstream and to the review of the declaration.")


def validate_field_optionality(proto: dict) -> None:
    """Abort unless every field's optionality column is one its side can carry.

    A command's two sides say whether a field may be left out in two columns,
    and the choice of a second column is the whole point here: on the request
    side the absent column means *optional* — a request field with no `required`
    is one the bindings default (`#[serde(default)]` / `?`), so `required =
    false` is what a request field says to be optional — while on the response
    side the absent column means *required*, which is how this file has always
    read: every declared response field is in every reply. One column read with
    two opposite defaults, depending on the side it sat on, is a trap in a file
    whose whole job is to be read, so the response side gets a column of its own
    (`optional = true`) and each column is refused on the wrong side.

    That refusal is the other half of the check and not a formality: a `required`
    left on a response field is a marking nothing acts on — gen_fields() would
    copy it into the manifest and every emitter would ignore it, so a field its
    author believed conditional travels as an unconditional one — which is the
    failure mode validate_field_roles() refuses for an unregistered `role`.
    Both directions are reported naming the command, the side and the field.

    The value has to be a boolean, because that is what the emitters and the
    suite read it as: `optional = "true"`, `= 1` or `= "some"` is a field whose
    marking the manifest writes as a string or a number, and a consumer that
    tests it for truth (`f.get("optional")`) would then mark the field optional
    by accident while a review of the declaration reads something else.

    What this cannot hold is whether a field *should* be optional. The
    generator has nothing to derive it from — whether the firmware leaves a key
    out is a rule of the handler that writes it — so a `optional = true` on a
    field a reply always carries is wrong in a way only a device-side check or a
    review can see, exactly like a field that should have a `role` and has none
    (validate_field_roles()).
    """
    wrong_side: List[str] = []
    not_a_bool: List[str] = []
    for cmd in proto.get("commands", []):
        command = f"{cmd['domain']}.{cmd['name']}"
        for f in cmd.get("request", []):
            if "optional" in f:
                wrong_side.append(
                    f"{command}: request field '{f['name']}' carries `optional` "
                    f"= {f['optional']!r}; a request field's absence is "
                    f"declared with `required = false`")
        for f in cmd.get("response", []):
            if "required" in f:
                wrong_side.append(
                    f"{command}: response field '{f['name']}' carries `required` "
                    f"= {f['required']!r}; a response field's absence is "
                    f"declared with `optional = true`")
            if "optional" in f and not isinstance(f["optional"], bool):
                not_a_bool.append(
                    f"{command}: response field '{f['name']}' carries `optional` "
                    f"= {f['optional']!r} ({type(f['optional']).__name__}), "
                    f"which is not a boolean")
    if wrong_side or not_a_bool:
        fail(*[f"ERROR: a field's optionality is not a boolean — {entry}"
               for entry in not_a_bool],
             *[f"ERROR: a field declares its optionality in the other "
               f"side's column — {entry}" for entry in wrong_side],
             "       A request field's absence is `required` (the absent "
             "column means optional there, so `required = false` is the "
             "marking) and a response field's is `optional = true` (the "
             "absent column means required there, as it always has).",
             "       A column on the wrong side is a marking nothing acts "
             "on — the manifest copies it and every emitter, the response "
             "table and the bindings ignore it, so the field travels as the "
             "one its column does not say it is.",
             "       `optional` takes `true` or `false`: it is read as a "
             "boolean by gen_fields() and by "
             "scripts/test/test_cdc_protocol.py.")


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
        fail(*[f"ERROR: command error codes — {problem}" for problem in problems],
             "       A command's `error_codes` array names the codes its reply "
             "can carry, so every entry has to be a key of [error_codes] at the "
             "number that key carries. Nothing emits the arrays, so an entry "
             "that drifts — a name the table does not have, or a number the "
             "table gives another name — reads as a permission the command does "
             "not have and no artifact or reader would ever show it.")

def validate_command_uniqueness(proto: dict) -> None:
    """Abort unless every command declares a domain and name no other command repeats.

    A command is identified by the pair, and the file is read as a list, so two
    entries sharing one produce no syntax error and no merge: both arrive, both
    are emitted, and the generated dispatch ends up holding whichever the
    emitters reached last. The declaration that lost is still in the file and
    still reads like the one in effect. That is the failure this refuses -- the
    same shape validate_field_roles() refuses for an unregistered `role`, reached
    from the other side: there a marking nothing acts on, here a declaration
    nothing can reach.

    The report names the pair once and then the opening words of each
    declaration, so a reader can tell which two they are without counting
    declarations in the file.
    """
    seen = {}
    for cmd in proto.get("commands", []):
        key = (cmd.get("domain", ""), cmd.get("name", ""))
        seen.setdefault(key, []).append(cmd)
    repeated = {k: v for k, v in seen.items() if len(v) > 1}
    if not repeated:
        return
    details = []
    for (domain, name), cmds in sorted(repeated.items()):
        details.append(f"       {domain}.{name} -- declared {len(cmds)} times")
        for cmd in cmds:
            opening = (cmd.get("description") or "").strip().split("\n")[0][:70]
            details.append(f'           "{opening}"')
    fail(f"ERROR: command identity -- a command is identified by its domain and its "
         f"name, and {len(repeated)} of them are declared more than once", *details)
