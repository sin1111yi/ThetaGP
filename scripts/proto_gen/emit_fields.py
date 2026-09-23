"""
gen_fields / gen_fields_md — the two artifacts a consumer reads rather than compiles.

proto_fields.json is the ordered request/response field lists the CDC suite and
other consumers read instead of hand-copying the protocol shape;
protocol-fields.md is the same response fields as the Markdown tables the docs
point at. Both are derived from the TOML in declaration order.

"""
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from proto_gen.model import (
    GENERATOR_SOURCES,
    command_fields,
    envelope_fields,
    fail_uncovered_fields,
    field_coverage_errors,
    file_sha256,
    generator_sha256,
    record_types,
)

# ═════════════════════════════════════════════════════════════════════════════
# Field manifest generator (JSON)
# ═════════════════════════════════════════════════════════════════════════════


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

    # The declared record types, by name, each with its ordered fields — the
    # shape a field written as `<Name>[]` carries one object per element of. A
    # consumer reading a response field of that type finds the element's keys,
    # types and order here rather than in the description prose, and the
    # [[types]] order is the TOML's.
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
        # Keyed "<Name>", the name a field's `type` spells before the array
        # suffix. Insertion order = the TOML's [[types]] order.
        "types": {
            name: {
                "namespace": t.get("namespace", ""),
                "description": t.get("description", ""),
                "fields": [
                    {k: v for k, v in (("name", f.get("name")),
                                       ("type", f.get("type")),
                                       ("json", f.get("json")),
                                       ("description", f.get("description")))
                     if v}
                    for f in t.get("fields", [])
                ],
            }
            for name, t in record_types(proto).items()
        },
    }

    result = json.dumps(manifest, indent=2, ensure_ascii=False) + "\n"

    if out:
        out.write_text(result)
        print(f"  [fields] wrote {out}", file=sys.stderr)

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
      "is spelled as the TOML declares it (`u32` / `string` / `MemoryRegion[]`) rather\n"
      "than translated, so the two can be compared word for word; the order is the\n"
      "order the fields are written in. A type ending in `[]` is an array: the field\n"
      "carries one object per element, under the keys of the record type the name\n"
      "declares, and those are the tables at the end of this file.")
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

    # The element shapes of the array fields above, as tables of their own: a
    # reader of a `MemoryRegion[]` row needs the record's keys, types and order,
    # and they are declared once here rather than repeated in each row's note.
    records = record_types(proto)
    if records:
        w("## Declared record types")
        w()
        w("One table per `[[types]]` entry of `protocol.toml`, in declaration order.\n"
          "A field whose type is `<Name>[]` carries a list of these, one object per\n"
          "element, under the record's own key names.")
        w()
        for name, record in records.items():
            w(f"### `{name}`")
            w()
            desc = md_cell(record.get("description", ""))
            if desc:
                w(desc)
                w()
            w("| Field | Type | Source or note |")
            w("|------|------|--------------|")
            for f in record.get("fields", []):
                note = md_cell(f.get("description", "")) or "—"
                w(f"| `{md_cell(f['json'])}` | {md_cell(f['type'])} | {note} |")
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
