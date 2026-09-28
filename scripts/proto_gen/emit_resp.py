"""
gen_resp — protocol/proto_resp.h, the response writers as generated code.

Three forms, one rule: the JSON a reply carries is written by code derived from
protocol.toml and not by a format string written in the firmware.

  * an X-macro table per command whose response fields are all values a printf
    conversion writes — the order, the JSON keys and the conversions are the
    table, and the firmware names the value of each field from its own macros;
  * a writer function per command whose response carries an array of records
    (`type = "MemoryRegion[]"`): the caller fills a value struct per element and
    passes a pointer and a length, and the keys, the punctuation, the brackets
    and the conversions are here;
  * a writer function per error reply shape ([envelope] plus [error_reply]):
    the keys, their order, the conversions and the status word are those two
    sections, and the code arrives as one of the error codes the generated
    protocol class names rather than as a digit at the call site.

An array has no printf conversion, so it has no table form: the writer is what a
response carrying one is written through. An error reply is written in more than
one shape, so the three shapes are three functions instead of one with a flag.

"""
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from proto_gen.model import (
    CPP_TYPE_MAP,
    PRINTF_TYPE_MAP,
    ROLE_PRESENCE,
    envelope_on_side,
    error_reply_on_side,
    fail,
    no_table_flag,
    optional_flag,
    record_element_of,
    record_types,
    sanitize_cpp_comment,
    to_pascal,
    values_struct_name,
)

# ═════════════════════════════════════════════════════════════════════════════
# Response field table generator (C++ header, X-macro)
# ═════════════════════════════════════════════════════════════════════════════

def resp_macro(domain: str, name: str) -> str:
    """The name of the X-macro table of command <domain>.<name>."""
    return f"THETAGP_RESP_{domain.upper()}_{name.replace('-', '_').upper()}"


def resp_writer(domain: str, name: str) -> str:
    """The name of the writer function of command <domain>.<name>."""
    return f"{domain}{to_pascal(name)}"


def _envelope_head(proto: dict, cmd: dict) -> Tuple[str, List[Tuple[str, str, str]]]:
    """The reply's leading keys → (the format text, the arguments to it).

    The keys and the order come from [envelope]'s reply side, so the head of a
    written reply is the declared envelope and not three names spelled here.
    Each returned argument is (the expression passed, the conversion, the
    parameter it takes) and the parameter's C++ type comes with it: `status` is
    the outcome of a reply that succeeded and `cmd` is the command's own full
    name, both known at generation time, while any other key of the envelope is
    a value the caller passes.
    """
    parts: List[str] = []
    args: List[Tuple[str, str, str, str]] = []
    for field in envelope_on_side(proto, "reply"):
        ctype, spec = PRINTF_TYPE_MAP[field["type"]]
        parts.append(f'{field["json"]}:{spec}')
        if field["json"] == "status":
            args.append(('"ok"', spec, "", ctype))
        elif field["json"] == "cmd":
            args.append((f'"{cmd["domain"]}.{cmd["name"]}"', spec, "", ctype))
        else:
            args.append((field["json"], spec, field["json"], ctype))
    return "{" + ",".join(parts), args


# ═════════════════════════════════════════════════════════════════════════════
# Error reply writers (C++)
# ═════════════════════════════════════════════════════════════════════════════

# The C++ type the code of an error reply arrives as: the generated protocol
# class names every code [error_codes] declares, and a writer takes one of them
# rather than an integer, so a code reaches the wire by name.
ERROR_CODE_TYPE = "Proto::ErrorCode"

# The status word an error reply carries, and the one the envelope's description
# names beside `ok` ([envelope].status). Known at generation time, the way the
# successful reply's `ok` is in _envelope_head().
ERROR_STATUS = "error"


def error_reply_keys(proto: dict) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    """[error_reply]'s code key and sentence key, as the section declares them.

    Which declared key carries which is read from its type and not from its name:
    a value of the code key is the `code` of one of [error_codes], so that key is
    of an integer type, while the sentence is the key of type string. A section
    declaring neither, or more than one of either, is not a shape a writer can be
    generated from, so the run stops here rather than writing one of the two
    under the other's conversion.
    """
    keys = error_reply_on_side(proto, "reply")
    if not keys:
        fail("ERROR: error reply — [error_reply] declares no key that appears in "
             "a reply, and the generated error reply writer is the whole shape "
             "of a reply that failed.")
    unwritable = sorted({k["type"] for k in keys if k["type"] not in PRINTF_TYPE_MAP})
    if unwritable:
        fail(f"ERROR: error reply — [error_reply] declares type(s) "
             f"{unwritable}, which no printf conversion writes, so no error "
             f"reply writer can carry them (types a conversion writes: "
             f"{sorted(PRINTF_TYPE_MAP)}).")

    codes = [k for k in keys if PRINTF_TYPE_MAP[k["type"]][1] != "%Q"]
    reasons = [k for k in keys if PRINTF_TYPE_MAP[k["type"]][1] == "%Q"]
    if len(codes) != 1 or len(reasons) != 1:
        fail("ERROR: error reply — the writers are generated from a section "
             "declaring exactly one key of an integer type (the code) and "
             "exactly one of type string (the sentence): [error_reply] declares "
             f"{[k['json'] for k in codes]} and {[k['json'] for k in reasons]}.")
    return codes[0], reasons[0]


def _error_reply_keys(proto: dict, echo_request: bool,
                      with_code: bool) -> List[Dict[str, Any]]:
    """The keys one error reply shape carries, in the order it writes them.

    The envelope's keys come first, in [envelope]'s declaration order, and the
    error reply's after them, in [error_reply]'s: a reply that answered a request
    it could read echoes it, so it carries the envelope's `some` keys (`cmd`,
    `queued`) beside the key every reply carries (`status`), while one that could
    not read the request carries the `always` keys alone. `with_code` drops the
    code for the shape a failed write is reported in, which carries the sentence
    and no code ([error_reply] documents that shape).
    """
    code_key, reason_key = error_reply_keys(proto)
    envelope = envelope_on_side(proto, "reply")
    if echo_request:
        head = list(envelope)
    else:
        head = [f for f in envelope if f.get("required")]
    tail = [code_key, reason_key] if with_code else [reason_key]
    return head + tail


def _write_error_writer(w, proto: dict, name: str,
                        keys: List[Dict[str, Any]], code_key: Optional[dict]) -> None:
    """One error reply writer: its comment, its signature, its writes.

    The parameters are the reply's keys in the order it writes them — a value for
    each, taken as the type its declaration gives it, and the code as one of
    [error_codes] — so the signature and the format text are read from the same
    list and cannot disagree with it or with each other.
    """
    parts: List[str] = []
    params: List[str] = ["Json &out"]
    args: List[str] = []
    for key in keys:
        ctype, spec = PRINTF_TYPE_MAP[key["type"]]
        parts.append(f'{key["json"]}:{spec}')
        if key["json"] == "status":
            args.append(f'"{ERROR_STATUS}"')
        elif code_key is not None and key["json"] == code_key["json"]:
            args.append(f"static_cast<{ctype}>(code)")
            params.append(f"{ERROR_CODE_TYPE} code")
        else:
            args.append(key["json"])
            params.append(f"{ctype} {key['json']}")

    w("// Writes the reply of a request that failed — the keys")
    w(f"// {', '.join(key['json'] for key in keys)}, in that order.")
    w(f"inline void {name}(")
    for i, param in enumerate(params):
        comma = "," if i + 1 < len(params) else ") {"
        w(f"    {param}{comma}")
    w('    out.printf("{' + ",".join(parts) + '}",')
    w("               " + ", ".join(args) + ");")
    w("}")
    w()


def gen_resp(proto: dict, out: Optional[Path] = None) -> str:
    """Response payload writers, one per command that carries one, as C++.

    A command whose response fields are all values a printf conversion writes
    gets an X-macro table listing them in the order protocol.toml declares them
    — the order the response writes them in — with the JSON key, the printf
    argument type, the conversion for that type and the presence of each field.
    A consumer expands the table with a macro of its own, so the same list
    drives the bytes on the wire and anything that has to agree with them; the
    firmware's copy of the order, the keys and the specifiers is this file, not
    a format string written by hand.

    A command whose response carries an array of records (`MemoryRegion[]`) gets
    a writer function instead of a table: an array of objects is not a value any
    conversion writes, so it has no entry form, and the function takes the
    elements as values — one C++ struct per record type, holding the record's
    declared fields and nothing else — beside the command's scalar fields. The
    keys, the punctuation and the brackets of the array are in the function, so
    the caller fills values and passes a pointer and a length.

    A command whose response carries a field of a type no conversion writes and
    that is not an array of records (`any`) gets neither: its reply stays
    assembled by hand, and the command is named in the header beside the flag
    emitted for it, which the site that assembles that reply asserts.

    An error reply is written by a function of this header as well, and there is
    one per shape the firmware writes rather than one per command: every command
    that fails writes the same keys behind `status`, so the shape is derived once
    from [envelope] and [error_reply] — the keys, their order in the reply, the
    conversions and the status word — and the entry points differ in whether they
    echo the request they answer and whether the reply carries a code. The code
    is a value of the generated protocol's error codes, so a caller names the
    code it answers with and the number it travels as is the [error_codes]
    entry's.
    """
    commands = proto.get("commands", [])
    records = record_types(proto)
    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

    # The three error reply shapes, as the two sections declare them: the keys
    # are the whole of what the writers need, so they are read once here and each
    # shape carries its own list. The code key is the one [error_reply] declares
    # of an integer type — a writer takes it as an error code — and the
    # `errorReplyNoCode` shape is the one that section documents without a code.
    err_code_key, _ = error_reply_keys(proto)
    error_shapes: List[Tuple[str, List[Dict[str, Any]], Optional[dict]]] = [
        ("errorReply",
         _error_reply_keys(proto, echo_request=False, with_code=True), err_code_key),
        ("errorReply",
         _error_reply_keys(proto, echo_request=True, with_code=True), err_code_key),
        ("errorReplyNoCode",
         _error_reply_keys(proto, echo_request=True, with_code=False), None),
    ]

    # The three partitions of the commands that carry a response, decided here so
    # the body below writes one section per form and not one condition per
    # field: a table for the responses a table can carry, a writer for the
    # responses that hold an array of records, and nothing but the header's
    # naming for the rest.
    tabbable: List[dict] = []
    writable: List[dict] = []
    unwritable: List[dict] = []
    for cmd in commands:
        resp = cmd.get("response", [])
        if not resp:
            continue
        if any(record_element_of(f["type"], records) for f in resp):
            writable.append(cmd)
        elif all(f["type"] in PRINTF_TYPE_MAP for f in resp):
            tabbable.append(cmd)
        else:
            unwritable.append(cmd)

    # ── Header ──────────────────────────────────────────────────────────────
    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
    w("// Source: protocol/protocol.toml")
    w(f"// Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w("#pragma once")
    w("#include <cstdint>")
    # Every writer takes the buffer the reply is written into, and an error reply
    # writer takes the code as one of the error codes the generated protocol
    # class names, so the header carries both.
    w('#include "protocol/proto.h"')
    w('#include "utils/json/json.h"')
    w()
    w("// Response payload fields of a command, in the order the response writes")
    w("// them, one table per command. A table is expanded with a macro of the")
    w("// consumer's own, called once per field as")
    w("//     X(<json name>, <printf argument type>, <conversion>, <presence>)")
    w("// where the name selects the value to write, the type is what that value")
    w("// has to be, the conversion is the printf specifier for it, and the")
    w("// presence is 1 or a flag that is 0 in a build without the field.")
    w("//")
    w("// A second flag, THETAGP_RESP_OPTIONAL_<command>_<field>, answers a")
    w("// different question about a field: whether a *reply* has to carry the")
    w("// key, which the presence above does not say. The field is in every")
    w("// build that writes it; what the flag carries is that the command's")
    w("// replies may leave the key out, and the write site names it, so a")
    w("// declaration that comes off the field stops that site from compiling.")
    w("//")
    w("// A command whose response carries an array of records gets no table: the")
    w("// array has no conversion, so a table would be short of it. It gets a")
    w("// writer function instead (below), which takes the elements as values and")
    w("// writes the whole reply.")
    w("//")
    w("// A third flag, THETAGP_RESP_NO_TABLE_<command>, is derived and not")
    w("// declared: it is emitted for every command whose response carries a")
    w("// field of a type no conversion writes and that is not an array of")
    w("// records. Such a command gets no table and no writer, so its reply is")
    w("// assembled by hand, and the site that assembles it names the flag — a")
    w("// field whose type becomes one a table carries takes the flag with it and")
    w("// stops that site from compiling.")
    w("//")
    w("// An error reply is written by the functions below too, derived from")
    w("// [envelope] and [error_reply] rather than from a format string written")
    w("// by hand: those two sections carry the keys, the order they are written")
    w("// in, the conversions and the status word, and the code is passed as one")
    w("// of the error codes the generated protocol class names — so a reply")
    w("// names the code it answers with instead of spelling its number. The")
    w("// three entry points are the three shapes the firmware writes: a request")
    w("// the dispatcher could not read (the envelope's `always` keys, then the")
    w("// code and the sentence), the same reply to a request it did read (the")
    w("// envelope's keys, then the code and the sentence), and the one a failed")
    w("// write is reported in ([error_reply] documents it, and it carries the")
    w("// sentence and no code).")
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

    # Presence flags, once per response field the TOML declares `optional`:
    # a reply of the command may leave the key out. Such a field still gets a
    # table entry below, with the constant presence 1 — whether a *build* writes
    # the field at all and whether a *reply* has to carry it are two questions,
    # and a presence column holds one answer, so the second question gets a flag
    # of its own instead of a second meaning for that column.
    #
    # The flag is the whole of what this emitter derives from the column, and it
    # is emitted so that the declaration travels to the code that has to keep it
    # true: the write site names the flag (src/test/config_cmd_handler.cpp for
    # config.save.dropped_keys), so a field whose `optional = true` comes off
    # stops that site from compiling rather than leaving a declaration no code
    # answers to. What the flag cannot carry is *when* the key is left out:
    # that rule has no column to be derived from and is written where the write
    # is (protocol.toml, [commands] config.save).
    optional_fields = [(cmd, f) for cmd in commands
                       for f in cmd.get("response", []) if f.get("optional")]
    for cmd, f in optional_fields:
        w(f"// {cmd['domain']}.{cmd['name']} declares {f['json']} "
          f"`optional = true` (protocol.toml):")
        w("// a reply of that command may leave the key out.")
        w(f"#define {optional_flag(cmd['domain'], cmd['name'], f['name'])} 1")
    if optional_fields:
        w()

    # Flags, once per command whose response no table and no writer carries.
    # Derived from the command's own fields and not declared by a column, so the
    # flag exists exactly where such a reply does.
    for cmd in unwritable:
        carried = [f for f in cmd.get("response", [])
                   if f["type"] not in PRINTF_TYPE_MAP]
        w(f"// {cmd['domain']}.{cmd['name']} carries "
          f"{', '.join(f['json'] for f in carried)} of a type no conversion")
        w("// writes, so it gets no table and no writer: its reply is assembled")
        w("// by hand and the site that writes it names this flag.")
        w(f"#define {no_table_flag(cmd['domain'], cmd['name'])} 1")
    if unwritable:
        w()

    # ── Tables ──────────────────────────────────────────────────────────────
    for cmd in tabbable:
        resp = cmd.get("response", [])
        full_name = f"{cmd['domain']}.{cmd['name']}"
        w(f"// {full_name} — {sanitize_cpp_comment(cmd.get('description', ''))}")
        w(f"#define {resp_macro(cmd['domain'], cmd['name'])}(X) \\")
        for i, f in enumerate(resp):
            ctype, spec = PRINTF_TYPE_MAP[f["type"]]
            flag = ROLE_PRESENCE.get(f.get("role"), ("1",))[0]
            continuation = " \\" if i + 1 < len(resp) else ""
            w(f'    X({f["name"]}, {ctype}, "{spec}", {flag}){continuation}')
        w()

    # ── Writers ─────────────────────────────────────────────────────────────
    # Both kinds of writer live in one namespace, so the header describes them
    # here and opens the namespace once.
    if writable:
        w("// ── Response writers ──")
        w("// One function per command whose response carries an array of records,")
        w("// with a value struct per record type they take. A writer takes the")
        w("// buffer the reply is written into, then one argument per declared")
        w("// response field in declaration order — a value for a scalar field and")
        w("// a pointer with a length for an array field — and writes the whole")
        w("// reply: the envelope, the keys, the punctuation and the conversion of")
        w("// every value. It returns false when the reply did not fit the buffer,")
        w("// in which case the text in the buffer is an incomplete prefix and the")
        w("// caller sends its own error reply rather than it.")
        w("//")
        w("// A value struct holds the declared fields of one record and nothing")
        w("// else: no key names, no punctuation, no conversions — the writer")
        w("// carries those — so the caller fills values. The namespace is")
        w("// ThetaGP::Resp and not ThetaGP::Proto::Resp: ThetaGP::Proto is the")
        w("// generated class of protocol/proto.h, and a namespace cannot be")
        w("// declared inside a class.")
        w()

    w("// ── Error reply writers ──")
    w("// One function per shape an error reply is written in, taking the keys,")
    w("// the order and the conversions of the two sections that declare the")
    w("// shape. The code is a value of the generated protocol class's error")
    w("// codes, so a call site names the code it answers with and the number it")
    w("// travels as is that [error_codes] entry's.")
    w()
    w("namespace ThetaGP::Resp {")
    w()

    if writable:
        emitted: List[str] = []
        for cmd in writable:
            for f in cmd.get("response", []):
                element = record_element_of(f["type"], records)
                if element and element not in emitted:
                    emitted.append(element)
                    _write_values_struct(w, element, records[element])

        for cmd in writable:
            _write_writer(w, proto, cmd, records)

    for name, keys, code_key in error_shapes:
        _write_error_writer(w, proto, name, keys, code_key)

    w("} // namespace ThetaGP::Resp")
    w()

    # ── What no table carries ───────────────────────────────────────────────
    writer_names = [f"{c['domain']}.{c['name']}" for c in writable]
    if writer_names:
        w("// Written by the functions above and not through a table, because the")
        w("// response carries an array of records and an array has no conversion:")
        for name in writer_names:
            w(f"//   {name} — {resp_writer(name.split('.')[0], name.split('.')[1])}")
        w()
    w("// The error replies are written by the functions above as well: errorReply")
    w("// for the shapes that carry the code and errorReplyNoCode for the one that")
    w("// carries the sentence alone. They belong to no command, so no table lists")
    w("// them.")
    w()
    if unwritable:
        w("// No table and no writer, because a response field of no printf form")
        w("// would be missing from every response written through one. Each of")
        w("// these names the flag the code that assembles its reply asserts")
        w("// (THETAGP_RESP_NO_TABLE_*, above):")
        for cmd in unwritable:
            carried = [f["name"] for f in cmd.get("response", [])
                       if f["type"] not in PRINTF_TYPE_MAP]
            w(f"//   {cmd['domain']}.{cmd['name']} ({', '.join(carried)})")
        w()

    result = "\n".join(lines) + "\n"

    if out:
        out.write_text(result)
        print(f"  [resp] wrote {out}", file=sys.stderr)

    return result


def _write_values_struct(w, record_name: str, record: dict) -> None:
    """One record type's C++ value struct, as the fields protocol.toml declares."""
    w(f"// The element of a field declared as `{record_name}[]`.")
    w(f"// {sanitize_cpp_comment(record.get('description', ''))}")
    w(f"struct {values_struct_name(record_name)} {{")
    for f in record["fields"]:
        ctype = CPP_TYPE_MAP.get(f["type"])
        if ctype is None:
            fail(f"ERROR: record types — {record_name}.{f['name']} declares type "
                 f"'{f['type']}', which the C++ type map does not carry")
        w(f"    {ctype} {f['name']}; // {sanitize_cpp_comment(f.get('description', ''))}")
    w("};")
    w()


def _write_writer(w, proto: dict, cmd: dict, records: Dict[str, dict]) -> None:
    """The function that writes one command's whole reply, as C++."""
    full_name = f"{cmd['domain']}.{cmd['name']}"
    resp = cmd.get("response", [])
    head_fmt, head_args = _envelope_head(proto, cmd)

    # The parameters: the envelope keys a reply takes a value for, ahead of the
    # declared response fields in declaration order, each scalar field one value
    # and each array field a pointer with its length after it.
    signature: List[str] = ["Json &resp"]
    for _expr, _spec, param, ctype in head_args:
        if param:
            signature.append(f"{ctype} {param}")
    for f in resp:
        element = record_element_of(f["type"], records)
        if element:
            signature.append(f"const {values_struct_name(element)} *{f['name']}")
            signature.append(f"uint32_t {f['name']}_len")
        else:
            signature.append(f"{PRINTF_TYPE_MAP[f['type']][0]} {f['name']}")

    w(f"// Writes the reply of {full_name} — "
      f"{sanitize_cpp_comment(cmd.get('description', ''))}")
    w(f"inline bool {resp_writer(cmd['domain'], cmd['name'])}(")
    for i, param in enumerate(signature):
        comma = "," if i + 1 < len(signature) else ") {"
        w(f"    {param}{comma}")
    w(f'    resp.printf("{head_fmt}",')
    w("                " + ", ".join(arg[0] for arg in head_args) + ");")
    for f in resp:
        element = record_element_of(f["type"], records)
        if element:
            _write_array(w, f, records[element])
            continue
        _ctype, spec = PRINTF_TYPE_MAP[f["type"]]
        presence = ROLE_PRESENCE.get(f.get("role"), ("1",))[0]
        if presence == "1":
            w(f'    resp.printf(",{f["json"]}:{spec}", {f["name"]});')
        else:
            w(f"    if ({presence}) {{")
            w(f'        resp.printf(",{f["json"]}:{spec}", {f["name"]});')
            w("    }")
    w('    resp.printf("}");')
    w("    return !resp.overflowed();")
    w("}")
    w()


def _write_array(w, field: dict, record: dict) -> None:
    """The writes that carry one array field of a response, punctuation included."""
    entry = "{"
    entry += ",".join(f'{f["json"]}:{PRINTF_TYPE_MAP[f["type"]][1]}'
                      for f in record["fields"])
    entry += "}"
    args = [f'{field["name"]}[i].{f["name"]}' for f in record["fields"]]
    w(f'    resp.printf(",{field["json"]}:[");')
    w(f'    for (uint32_t i = 0; i < {field["name"]}_len; ++i) {{')
    w(f'        resp.printf((i == 0) ? "{entry}"')
    # The entry under a comma, then the values of one element, four to a line.
    w(f'                             : ",{entry}",')
    for i in range(0, len(args), 4):
        tail = ", ".join(args[i:i + 4])
        w("                    " + tail + ("," if i + 4 < len(args) else ");"))
    w("    }")
    w('    resp.printf("]");')
