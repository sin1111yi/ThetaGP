"""
The board's own declaration, read from the data that carries it.

configs/board_schema.toml holds one row per field: its name, the kind that
holds its rules, the values it is accepted under and the macro or cmake
variable it becomes. This module reads that file and hands the generator the
same objects engine.py walks — the rows are data, the kinds are code, and the
golden headers under scripts/test/board_config/ are what holds the two to each
other.

The file is written by schema_dump.py, which is the inverse of this reader.
"""

import pathlib
import tomllib

from . import engine, output, tables
from .engine import ABSENT, Group, PRESENT, Table

DECLARATION = pathlib.Path(__file__).resolve().parents[2] / "configs" / "board_schema.toml"

# What a row's `table`, `checks` and `lines` name: a value map the field is held
# to, a check the field is held by, or the lines it becomes.
def _map(name: str):
    return getattr(tables, name)


def _callable(name: str):
    return getattr(output, name)


def _field(row: dict):
    held = dict(row)
    held.pop("item", None)
    kind = held.pop("kind")
    name = held.pop("name", "")
    if "table" in held:
        held["table"] = _map(held["table"])
    for key in ("checks", "lines"):
        if key in held:
            held[key] = _callable(held[key])
    for key in ("when",):
        held.pop(key, None)
    cls = getattr(engine, kind)
    # A row with no name of its own is not a field of the table: the
    # switch a strip's presence is, and a group's own rules.
    return cls(name, **held) if name else cls(**held)


def _group(row: dict) -> Group:
    field, where = row["when"]
    if where == "present":
        value = PRESENT
    elif where == "absent":
        value = ABSENT
    else:
        value = where
    items = [_field(item) for item in row.get("field", [])]
    return Group((field, value), items, row.get("note", ""))


def _emitter(row: dict):
    if "factory" in row:
        args = {k: v for k, v in row.items() if k != "factory"}
        return _callable(row["factory"])(**args)
    return _callable(row["name"]) if isinstance(row, dict) else _callable(row)


def _table(row: dict) -> Table:
    items = []
    for item in row.get("item", []):
        items.append(_group(item) if item.get("item") == "group" else _field(item))
    kwargs = {}
    if "kind" in row:
        kwargs["kind"] = row["kind"]
    if "what" in row:
        kwargs["what"] = row["what"]
    if row.get("required"):
        kwargs["required"] = True
    if "reports" in row:
        kwargs["reports"] = _emitter(row["reports"])
    if "lines" in row:
        kwargs["lines"] = _emitter(row["lines"])
    return Table(
        tuple(row["path"]),
        items=items,
        preamble=[_field(p) for p in row.get("preamble", [])],
        **kwargs,
    )


def _read() -> list[Table]:
    with open(DECLARATION, "rb") as f:
        return [_table(row) for row in tomllib.load(f)["table"]]


BOARD_SCHEMA = _read()
