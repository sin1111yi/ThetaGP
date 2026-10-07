"""
The field machinery a board's declaration is read with.

A schema is a list of tables, a table is a list of rows, and a row is one
declared value. A field kind holds both halves of a field: what a declared
value must look like, and the line it becomes. They sit in one class so the
two cannot drift apart.

Shape a board's own configuration calls for — a key matrix, a bus descriptor
table, a report rate bound to the link speed — declares its checks and its
lines in output.py. A row still names them, so a field is spelled once.
"""

from .pin_utils import pin_array_lines, pin_struct, validate_pin_format

# A row's presence, for a group that applies when a field is there or not.
PRESENT = object()
ABSENT = object()

# The shapes a table can take.
SINGLE = "single"    # one table of scalar values
MAP = "map"          # named sub-tables, each with the same rows
ARRAY = "array"      # a list of tables, each with the same rows

MAX_PINS = 8


def macro_line(name: str, value) -> str:
    """One `#define`, the name padded so a table's values line up."""
    return f"#define {name:<28} {value}"


def flag_line(name: str, pad: bool = False) -> str:
    """One `#define` that names what is there and carries no value.

    Padded where it stands among value lines, so the values beside it keep
    one column.
    """
    return f"#define {name:<28}" if pad else f"#define {name}"


# ── field kinds ──────────────────────────────────────────────────────────────

class Field:
    """One declared value of a table.

    A field with `macro` becomes a line of the header, a field with `cmake`
    becomes a variable of board_config.cmake, and a field with neither is
    read for its rules alone (board_info's series names the MCU header, which
    the assembly writes, not a value).
    """

    def __init__(self, name: str, *, required: bool = False,
                 macro: str | None = None, cmake: str | None = None,
                 default=None):
        self.name = name
        self.required = required
        self.macro = macro
        self.cmake = cmake
        # A field a board may leave out while the firmware still needs a value:
        # `default` is emitted in its place, and no rule is read of it.
        self.default = default

    def macro_for(self, namespace: dict) -> str | None:
        """The macro name this field emits, with the table's name filled in."""
        return self.macro.format(**namespace) if self.macro else None

    def check(self, value, path: str, entry: dict) -> list[str]:
        """What a declared value gets wrong; [] when it is accepted."""
        return []

    def render(self, value, entry: dict) -> str:
        """How a declared value is spelled inside a descriptor row."""
        return str(value)

    def emit(self, out: "Emission", value, macro: str | None, entry: dict) -> None:
        """The line or the cmake variable a declared value becomes."""
        if self.cmake is not None:
            out.cmake[self.cmake] = value
        if macro is not None:
            out.line(macro_line(macro, value))


class Text(Field):
    """A string with no further rule."""


class Word(Text):
    """A string of letters, digits and underscores."""

    def check(self, value, path, entry):
        if (not isinstance(value, str) or not value
                or not all(c.isalnum() or c == "_" for c in value)):
            return [
                f"{path} must contain only alphanumeric characters "
                f"and underscores"
            ]
        return []


class Int(Field):
    """An integer within a range, or above a floor where a board's own value
    is the ceiling it has to hold to."""

    def __init__(self, name, low, high=None, **kwargs):
        super().__init__(name, **kwargs)
        self.low = low
        self.high = high

    def check(self, value, path, entry):
        if isinstance(value, bool) or not isinstance(value, int):
            return [f"{path} must be an integer"]
        if self.high is None:
            if value < self.low:
                return [f"{path} must be at least {self.low}"]
        elif not self.low <= value <= self.high:
            return [f"{path} must be between {self.low} and {self.high}"]
        return []


class Bool(Field):
    """A boolean, emitted as true or false."""

    def check(self, value, path, entry):
        if not isinstance(value, bool):
            return [f"{path} must be a boolean"]
        return []

    def emit(self, out, value, macro, entry):
        if macro is not None:
            out.line(macro_line(macro, "true" if value else "false"))


class Presence(Field):
    """Whether a table holds a named entry, as the 0/1 switch a guard reads."""

    def __init__(self, macro: str, sub: str):
        super().__init__("", macro=macro)
        self.sub = sub

    def emit(self, out, value, macro, entry):
        out.line(macro_line(self.macro, 1 if self.sub in (value or {}) else 0))


class Named(Field):
    """A value the table above names. `lower` accepts a value in any case."""

    def __init__(self, name, table, *, lower=False, prefix="", **kwargs):
        super().__init__(name, **kwargs)
        self.table = table
        self.lower = lower
        self.prefix = prefix

    def named(self, value):
        """A declared value as the table's key."""
        return value.lower() if self.lower and isinstance(value, str) else value

    def named_value(self, value) -> str:
        return self.prefix + self.table[self.named(value)]

    def check(self, value, path, entry):
        named = self.named(value)
        if not isinstance(named, str) or named not in self.table:
            return [
                f"Invalid {path} '{value}'. Valid values: "
                f"{', '.join(sorted(self.table))}"
            ]
        return []

    def render(self, value, entry):
        return self.named_value(value)


class Enum(Named):
    """A named value, emitted as the constant the table names."""

    def __init__(self, name, table, *, include=False, **kwargs):
        super().__init__(name, table, **kwargs)
        # A series names the header the whole file opens with, not a value.
        self.include = include

    def emit(self, out, value, macro, entry):
        if self.cmake is not None:
            out.cmake[self.cmake] = value
        if self.include:
            out.include = f'#include "{self.named_value(value)}"'
        elif macro is not None:
            out.line(macro_line(macro, self.named_value(value)))


class Flag(Named):
    """A named value, emitted as a macro name for the capability it is."""

    def emit(self, out, value, macro, entry):
        out.line(flag_line(self.named_value(value)))


class Pin(Field):
    """One pin, emitted as the port and pin constants a driver takes."""

    def check(self, value, path, entry):
        err = validate_pin_format(value)
        return [f"{path}: {err}"] if err else []

    def render(self, value, entry):
        return pin_struct(value)

    def emit(self, out, value, macro, entry):
        if macro is not None:
            out.line(macro_line(macro, pin_struct(value)))


class PinArray(Field):
    """A list of pins: its count, and the pins themselves.

    A pin may be written as a string or as a table naming one, as a keypad
    declaration has always allowed.
    """

    def __init__(self, name, count_macro, list_macro, **kwargs):
        super().__init__(name, **kwargs)
        self.count_macro = count_macro
        self.list_macro = list_macro

    def check(self, value, path, entry):
        if not isinstance(value, list):
            return [f"{path} must be an array"]
        if not value:
            return [f"{path} must have at least 1 pin"]
        if len(value) > MAX_PINS:
            return [f"{path} cannot have more than {MAX_PINS} pins"]

        errors = []
        for i, item in enumerate(value):
            if isinstance(item, str):
                err = validate_pin_format(item)
            elif isinstance(item, dict) and "pin" in item:
                err = validate_pin_format(item["pin"])
            else:
                errors.append(
                    f"{path}[{i}] must be a pin string or table (e.g. 'PA0')"
                )
                continue
            if err:
                errors.append(f"{path}[{i}]: {err}")
        return errors

    def emit(self, out, value, macro, entry):
        out.line(macro_line(self.count_macro, len(value)))
        out.line(pin_array_lines(self.list_macro, value))


class Custom(Field):
    """A field whose checks and lines are its own, named here in one row."""

    def __init__(self, name, *, checks=None, lines=None, **kwargs):
        super().__init__(name, **kwargs)
        self.checks = checks
        self.lines = lines

    def check(self, value, path, entry):
        return self.checks(value, path, entry) if self.checks else []

    def emit(self, out, value, macro, entry):
        if self.lines is None:
            super().emit(out, value, macro, entry)
        else:
            self.lines(out, value, macro, entry)


class Group:
    """Rows a table carries only in one state.

    `when` names the field and what it must hold — PRESENT or ABSENT for the
    field's presence, a string for one of its values. `note` is what a missing
    field's message adds, so the message says which state asked for it.
    """

    def __init__(self, when, items, note=""):
        self.field, self.value = when
        self.items = list(items)
        self.note = note

    def applies(self, entry: dict) -> bool:
        held = entry.get(self.field, ABSENT)
        if self.value is PRESENT:
            return held is not ABSENT
        if self.value is ABSENT:
            return held is ABSENT
        return held is not ABSENT and str(held).lower() == str(self.value).lower()


def rows(items, entry: dict, note: str = ""):
    """The rows that apply to a table's entry, each with its group's note."""
    for item in items:
        if isinstance(item, Group):
            if item.applies(entry):
                yield from rows(item.items, entry, item.note)
        else:
            yield item, note


class Table:
    """One table of a board's declaration.

    `preamble` holds rows read against the table as a whole, before its
    entries. `lines` takes over the table's lines where they are not one per
    field — a bus descriptor table, a chip selection — while the rows below
    still hold its rules.
    """

    def __init__(self, path, *, kind=SINGLE, items=(), preamble=(),
                 required=False, what="", reports=None, lines=None):
        self.path = tuple(path)
        self.kind = kind
        self.items = list(items)
        self.preamble = list(preamble)
        self.required = required
        self.what = what
        self.reports = reports
        self.lines = lines

    @property
    def where(self) -> str:
        return ".".join(self.path)


class Emission:
    """What a schema's walk produces: the header's groups, its MCU header
    line, and the variables board_config.cmake carries."""

    def __init__(self):
        self.include: str | None = None
        self.cmake: dict[str, str] = {}
        self.groups: list[list[str]] = []

    def open(self) -> None:
        """Start the group the lines below belong to."""
        self.groups.append([])

    def line(self, text) -> None:
        """Add one line to the group open now. An empty line is a separator."""
        self.groups[-1].append(str(text))


# ── the walks ────────────────────────────────────────────────────────────────

def held_at(cfg: dict, path: tuple):
    """The value a path names, or None where the declaration is silent."""
    held = cfg
    for key in path:
        if not isinstance(held, dict) or key not in held:
            return None
        held = held[key]
    return held


def _entries(table: Table, held, errors: list[str], declared_order: bool = False):
    """The (entry, path, name) triples the table's rows are read against.

    A map's entries are read in the order the board declares them when its
    rules are the question, and in name order when its lines are, that being
    the order the header lists them.
    """
    if table.kind is MAP:
        if held is None:
            return []
        if not isinstance(held, dict) or not held:
            errors.append(
                f"{table.where} must hold at least one {table.what} table"
            )
            return []
        names = list(held) if declared_order else sorted(held)
        found = []
        for name in names:
            if not isinstance(held[name], dict):
                errors.append(f"{table.where}.{name} must be a table")
            else:
                found.append((held[name], f"{table.where}.{name}", name))
        return found

    if table.kind is ARRAY:
        if held is None:
            return []
        if not isinstance(held, list):
            errors.append(f"{table.where} must be an array of tables")
            return []
        found = []
        for i, entry in enumerate(held):
            if not isinstance(entry, dict):
                errors.append(f"{table.where}[{i}] must be a table")
            else:
                found.append((entry, f"{table.where}[{i}]", ""))
        return found

    return [(held or {}, table.where, "")]


def validate(schema: list[Table], cfg: dict) -> list[str]:
    """Every rule a board's declaration breaks, in the schema's order."""
    errors: list[str] = []

    for table in schema:
        held = held_at(cfg, table.path)

        if table.required and not held:
            errors.append(f"{table.where} is required")
            continue

        if table.reports is not None:
            table.reports(cfg, errors)

        for row in table.preamble:
            errors += row.check(held, table.where, held or {})

        for entry, path, _ in _entries(table, held, errors, declared_order=True):
            for row, note in rows(table.items, entry):
                where = f"{path}.{row.name}"
                if row.name not in entry:
                    if row.required:
                        suffix = f" {note}" if note else ""
                        errors.append(f"{where} is required{suffix}")
                    continue
                errors += row.check(entry[row.name], where, entry)

    return errors


def emit(schema: list[Table], cfg: dict) -> Emission:
    """The lines a board's declaration becomes."""
    out = Emission()

    for table in schema:
        out.open()
        held = held_at(cfg, table.path)

        if table.lines is not None:
            table.lines(out, held, table)
            continue

        for row in table.preamble:
            row.emit(out, held or {}, row.macro_for({}), held or {})

        for entry, _, name in _entries(table, held, []):
            namespace = {"NAME": name.upper(), "name": name} if name else {}
            for row, _ in rows(table.items, entry):
                if row.name in entry:
                    row.emit(out, entry[row.name], row.macro_for(namespace), entry)
                elif row.default is not None:
                    row.emit(out, row.default, row.macro_for(namespace), entry)

    return out
