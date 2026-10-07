#!/usr/bin/env python3
"""
Writes the board's declaration out as data.

Input:  scripts/config/schema.py's BOARD_SCHEMA — the table the generator walks.
Output: configs/board_schema.toml — the same table as data, which schema.py
        reads back. The round trip is what the golden headers under
        scripts/test/board_config/ hold: the generator's output is unchanged
        only when every attribute of every row came back.

Usage: python3 scripts/config/schema_dump.py [--out FILE]
"""

import argparse
import os
import pathlib
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from config import BOARD_SCHEMA, engine  # noqa: E402
from config import output, tables  # noqa: E402

# The keys a row carries per kind, and the order they are written in: the
# loader hands exactly these to the kind's constructor.
ROW_KEYS = ("required", "macro", "cmake", "default", "prefix", "lower",
            "include", "sub", "count_macro", "list_macro", "low", "high")


def map_name(mapping: dict) -> str:
    for name in sorted(dir(tables)):
        if name.isupper() and getattr(tables, name) is mapping:
            return name
    raise SystemExit(f"schema_dump: a row names a map with no name: {mapping}")


def func_name(func) -> str:
    for name in dir(output):
        if not name.startswith("_") and getattr(output, name) is func:
            return name
    return getattr(func, "__name__", "?")


def value(row) -> str:
    if isinstance(row, bool):
        return "true" if row else "false"
    if isinstance(row, str):
        return '"' + row.replace('"', '\\"') + '"'
    return str(row)


def field_lines(field) -> list[str]:
    kind = type(field).__name__
    lines = [f"kind = {value(kind)}"]
    if field.name:
        lines.insert(0, f"name = {value(field.name)}")
    if isinstance(field, engine.Named):
        lines.append(f"table = {value(map_name(field.table))}")
    if isinstance(field, engine.Custom):
        if field.checks is not None:
            lines.append(f"checks = {value(func_name(field.checks))}")
        if field.lines is not None:
            lines.append(f"lines = {value(func_name(field.lines))}")
    for key in ROW_KEYS:
        held = getattr(field, key, None)
        if held is None or held is False or held == "":
            continue
        lines.append(f"{key} = {value(held)}")
    return lines


def emit_fields(lines: list[str], head: str, rows: list, indent: str) -> None:
    for row in rows:
        lines.append("")
        lines.append(f"{indent}[[{head}]]")
        for line in field_lines(row):
            lines.append(f"{indent}{line}")


def emit_group(lines: list[str], group) -> None:
    where = ("present" if group.value is engine.PRESENT
             else "absent" if group.value is engine.ABSENT else group.value)
    lines.append("")
    lines.append("[[table.item]]")
    lines.append('item = "group"')
    lines.append(f"when = [{value(group.field)}, {value(where)}]")
    if group.note:
        lines.append(f"note = {value(group.note)}")
    emit_fields(lines, "table.item.field", group.items, "  ")


def emit_emitter(lines: list[str], key: str, callable_) -> None:
    origin = getattr(callable_, "origin", None)
    if origin is None:
        lines.append(f"{key} = {value(func_name(callable_))}")
        return
    lines.append("")
    lines.append(f"[table.{key}]")
    for name, held in origin.items():
        if isinstance(held, dict):
            inside = ", ".join(f"{k} = {value(v)}" for k, v in held.items())
            lines.append(f"{name} = {{ {inside} }}")
        else:
            lines.append(f"{name} = {value(held)}")


def dump() -> str:
    out = [
        "# The board's own declaration, as data: every field a BoardConfig.toml",
        "# may carry, the values it is accepted under, and what it becomes.",
        "#",
        "# Read by scripts/config/schema.py, which is the only reader, and written",
        "# by schema_dump.py, which is the only writer: the two are each other's",
        "# inverse, and the golden headers under scripts/test/board_config/ are",
        "# what says so — the generator's output is unchanged only when every row",
        "# of this file came back whole.",
        "#",
        "# `path` is where a table sits in the declaration: one segment is a table,",
        "# several are tables inside tables. A row of `items` is a field, or a",
        "# group of them that applies to the table only in one state.",
        "",
    ]
    for table in BOARD_SCHEMA:
        out.append("[[table]]")
        out.append(f"path = [{', '.join(value(p) for p in table.path)}]")
        if table.kind != engine.SINGLE:
            out.append(f"kind = {value(table.kind)}")
        if table.what:
            out.append(f"what = {value(table.what)}")
        if table.required:
            out.append("required = true")
        # The table's own keys come before its rows: a bare key after an
        # [[table.item]] would belong to that row, and a [table.lines] header
        # names a sub-table of the element the [[table]] opened.
        if table.reports is not None:
            emit_emitter(out, "reports", table.reports)
        if table.lines is not None:
            emit_emitter(out, "lines", table.lines)
        emit_fields(out, "table.preamble", table.preamble, "")
        for item in table.items:
            if isinstance(item, engine.Group):
                emit_group(out, item)
            else:
                out.append("")
                out.append("[[table.item]]")
                out.append('item = "field"')
                for line in field_lines(item):
                    out.append(line)
        out.append("")
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Dump the board declaration")
    parser.add_argument("--out", default="configs/board_schema.toml")
    args = parser.parse_args()
    text = dump()
    pathlib.Path(args.out).write_text(text)
    print(f"wrote {args.out} ({len(text.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
