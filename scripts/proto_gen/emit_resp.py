"""
gen_resp — protocol/proto_resp.h, the response writers as generated code.

Two forms, one rule: the JSON a reply carries is written by code derived from
protocol.toml and not by a format string written in the firmware.

  * an X-macro table per command whose response fields are all values a printf
    conversion writes — the order, the JSON keys and the conversions are the
    table, and the firmware names the value of each field from its own macros;
  * a writer function per command whose response carries an array of records
    (`type = "MemoryRegion[]"`): the caller fills a value struct per element and
    passes a pointer and a length, and the keys, the punctuation, the brackets
    and the conversions are here.

An array has no printf conversion, so it has no table form: the writer is what a
response carrying one is written through.

"""
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from proto_gen.model import (
    CPP_TYPE_MAP,
    PRINTF_TYPE_MAP,
    ROLE_PRESENCE,
    envelope_on_side,
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
    """
    commands = proto.get("commands", [])
    records = record_types(proto)
    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

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
    if writable:
        # The writer functions take the buffer the reply is written into.
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
        w("namespace ThetaGP::Resp {")
        w()

        emitted: List[str] = []
        for cmd in writable:
            for f in cmd.get("response", []):
                element = record_element_of(f["type"], records)
                if element and element not in emitted:
                    emitted.append(element)
                    _write_values_struct(w, element, records[element])

        for cmd in writable:
            _write_writer(w, proto, cmd, records)

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
