"""
The lines a board's declaration becomes where they are not one per field.

A key matrix is read as a whole, a bus carries a descriptor table every
driver indexes, a report rate is held to the link it travels on: each of those
names its checks and its lines here, and the schema's row points at them.
"""

import re

from .engine import flag_line, macro_line, rows
from .tables import (
    BUTTON_SUFFIXES,
    FLASH_CHIP_MAP,
    MAX_BUTTON_INDEX,
    MAX_KEY_INDEX,
    MAX_KEYPAD_BUTTONS,
    MAX_KEYPAD_KEYS,
    NO_KEY,
    USB_SPEED_CEILING_HZ,
    VALID_FLASH_CHIPS,
)

LICENSE = (
    "/*\n"
    " * This file is a part of ThetaGP.\n"
    " *\n"
    " * ThetaGP is free software: you can redistribute it and/or modify\n"
    " * it under the terms of the GNU General Public License as published by\n"
    " * the Free Software Foundation, either version 3 of the License, or\n"
    " * (at your option) any later version.\n"
    " *\n"
    " * ThetaGP is distributed in the hope that it will be useful,\n"
    " * but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
    " * GNU General Public License for more details.\n"
    " *\n"
    " * You should have received a copy of the GNU General Public License\n"
    " * along with this program.\n"
    " *\n"
    " * If not, see <https://www.gnu.org/licenses/>.\n"
    " */\n"
)


# ── Keypad ───────────────────────────────────────────────────────────────────

def check_key_map(value, path: str, entry: dict) -> list[str]:
    """The matrix against the pins it is read with, and the keys it may name.

    A pin list that is missing or empty is its own row's to report: without
    both of them there is no matrix shape to hold the map to.
    """
    drive = len(entry.get("drive_pins") or [])
    sense = len(entry.get("sense_pins") or [])
    if not drive or not sense:
        return []

    errors: list[str] = []

    if drive * sense > MAX_KEYPAD_KEYS:
        errors.append(
            f"Total keys ({drive * sense}) cannot exceed {MAX_KEYPAD_KEYS} "
            f"(drive={drive}, sense={sense})"
        )

    if not isinstance(value, list):
        errors.append(f"{path} must be an array of rows")
        return errors

    if len(value) != drive:
        errors.append(
            f"{path} has {len(value)} rows, expected {drive} (drive_pins count)"
        )

    for r, row in enumerate(value):
        if not isinstance(row, list):
            errors.append(f"{path} row {r} must be an array")
            continue
        if len(row) != sense:
            errors.append(
                f"{path} row {r} has {len(row)} columns, expected {sense}"
            )
        for c, key in enumerate(row):
            if key is None or key == NO_KEY:
                continue
            if not isinstance(key, int) or key < 0 or key > MAX_KEY_INDEX:
                errors.append(
                    f"{path}[{r}][{c}] must be 0-{MAX_KEY_INDEX} or 0xFF "
                    f"(got {key})"
                )
    return errors


def lines_key_map(out, value, macro, entry: dict) -> None:
    """The matrix as rows of key indices, and the range they need a mask for."""
    drive = len(entry["drive_pins"])
    sense = len(entry["sense_pins"])

    out.line("")
    out.line("#define BDCFG_KEYPAD_KEY_MAP \\")

    highest = 0
    for r in range(drive):
        cells = []
        for c in range(sense):
            key = value[r][c]
            if key is None:
                key = NO_KEY
            if key != NO_KEY and key > highest:
                highest = key
            cells.append(f"{key:3d}")
        row = ", ".join(cells)
        out.line(f"    {{{row}}}" + ("" if r == drive - 1 else ", \\"))

    out.line("")
    out.line("")
    out.line(macro_line("BDCFG_KEYPAD_MAX_KEY_INDEX", highest))
    out.line(macro_line("BDCFG_KEYPAD_MASK_ARRAY_SIZE", (highest + 32) // 32))


def check_button_map(value, path: str, entry: dict) -> list[str]:
    """The buttons a key matrix's indices name."""
    if not isinstance(value, list):
        return [f"{path} must be an array of [index, button_name] pairs"]
    if not value:
        return [f"{path} must have at least 1 entry"]
    if len(value) > MAX_KEYPAD_BUTTONS:
        return [f"{path} cannot exceed {MAX_KEYPAD_BUTTONS} entries"]

    errors: list[str] = []
    seen: set[int] = set()
    for i, item in enumerate(value):
        if not isinstance(item, list) or len(item) < 2:
            errors.append(f"{path}[{i}] must be [index, button_name]")
            continue
        index, name = item[0], item[1]
        if not isinstance(index, int) or index < 0 or index > MAX_BUTTON_INDEX:
            errors.append(
                f"{path}[{i}] index {index} must be a number 0-{MAX_BUTTON_INDEX}"
            )
        if index in seen:
            errors.append(f"{path}[{i}] duplicate index {index}")
        seen.add(index)
        if not isinstance(name, str):
            errors.append(
                f"{path}[{i}] button name must be a string, "
                f"got {type(name).__name__}"
            )
        elif name.upper() not in BUTTON_SUFFIXES:
            errors.append(
                f"{path}[{i}] '{name}' is not a valid button. "
                f"Valid examples: B1, L1, S1, UP"
            )
    return errors


def lines_button_map(out, value, macro, entry: dict) -> None:
    """Each key index beside the button mask it raises."""
    out.line("")
    out.line("#define BDCFG_KEYPAD_BUTTON_MAP \\")

    pairs = sorted((item[0], item[1]) for item in value)
    last = len(pairs) - 1
    for i, (index, name) in enumerate(pairs):
        mask = f"GAMEPAD_MASK_{name.upper()}"
        out.line(f"    {{{index}, {mask:<20}}}" + ("" if i == last else ", \\"))

    out.line("")


# ── USB ──────────────────────────────────────────────────────────────────────

def check_report_rate(value, path: str, entry: dict) -> list[str]:
    """The rate against the link that has to carry it.

    A board that declares no rate builds with the firmware's own default.
    """
    if isinstance(value, bool) or not isinstance(value, int):
        return [f"{path} must be a number (got {value!r})"]
    if value <= 0:
        return [f"{path} must be positive (got {value})"]

    errors: list[str] = []
    speed = entry.get("speed")
    if isinstance(speed, str):
        ceiling = USB_SPEED_CEILING_HZ.get(speed)
        if ceiling is None:
            errors.append(
                f"usb.speed '{speed}' has no report-rate ceiling in the "
                f"generator's table, so the rate cannot be checked against it"
            )
        elif value > ceiling:
            limits = ", ".join(
                f"{name} polls at most {limit} times per second"
                for name, limit in sorted(USB_SPEED_CEILING_HZ.items())
            )
            errors.append(
                f"{path} is {value} but usb.speed '{speed}' polls the "
                f"interrupt endpoint at most {ceiling} times per second, so "
                f"the key must not exceed {ceiling}: {limits}"
            )

    if 1000000 % value != 0:
        errors.append(
            f"{path} is {value}, which does not divide 1000000. The task "
            f"period is a whole number of microseconds "
            f"(1000000/{value} truncated to {1000000 // value} us), so the "
            f"tick cannot land on exactly {value} times per second."
        )
    return errors


# ── Buses ────────────────────────────────────────────────────────────────────

def render_descriptor(template: str, values: dict) -> str:
    """One descriptor row, from the template the table declares.

    `{field}` stands for the value that field renders as; every other brace is
    the row's own, so a template spells the nested braces a descriptor carries
    without escaping anything.
    """
    return re.sub(r"\{(\w+)\}", lambda at: str(values[at.group(1)]), template)


def bus_lines(kind: str, template: str, defaults: dict):
    """The macros a bus table carries.

    Its enable lines, the count of the entries that bind a name to an
    instance, the bindings themselves, and the descriptor table the drivers
    index. `template` is one descriptor row: `{field}` is the value the row of
    that field renders as, a field left out falling to `defaults`, where
    `@name` takes the value another field of the same entry carries.
    """

    def emit(out, held, table) -> None:
        entries = held or []

        for i in range(len(entries)):
            out.line(flag_line(f"BDCFG_USE_{kind}_{i + 1}", pad=True))

        # Only an entry that names both a binding and a peripheral becomes a
        # bus instance, and the descriptor table carries exactly those, in this
        # order: the instance number — and with it BUS_<kind>_<n> — is a
        # position here, not a position in the board's own array.
        bound = [
            entry for entry in entries
            if entry.get("bind") and entry.get("peripheral")
        ]
        if not bound:
            return

        out.line("")
        out.line(f"#define BDCFG_USE_{kind}_COUNT {len(bound)}")
        out.line("")

        for j, entry in enumerate(bound):
            out.line(
                macro_line(f"BDCFG_{entry['bind'].upper()}_{kind}",
                           f"BUS_{kind}_{j + 1}")
            )

        rendered: list[str] = []
        for entry in bound:
            values = {
                row.name: row.render(entry[row.name], entry)
                for row, _ in rows(table.items, entry)
                if row.name in entry
            }
            for name, default in defaults.items():
                if name not in values:
                    values[name] = (
                        values[default[1:]]
                        if isinstance(default, str) and default.startswith("@")
                        else str(default)
                    )
            rendered.append(render_descriptor(template, values))

        out.line("")
        out.line(macro_line(f"BDCFG_{kind}_DESC_DATA", "\\"))
        last = len(rendered) - 1
        for j, row in enumerate(rendered):
            out.line(f"        {row}" + (", \\" if j < last else ""))

    emit.__name__ = f"bus_lines[{kind}]"
    emit.__name__ = f"bus_lines[{kind}]"
    return emit


# ── Flash ────────────────────────────────────────────────────────────────────

def check_flash(cfg: dict, errors: list[str]) -> None:
    """A declared flash chip needs an SPI bus to sit on."""
    chip = (cfg.get("flash") or {}).get("chip", "none")

    # A chip the table does not name is the chip's own row to report.
    if chip not in VALID_FLASH_CHIPS or chip == "none":
        return

    if not any(s.get("bind") == "flash" for s in (cfg.get("bus") or {}).get("spi", [])):
        errors.append(
            f"flash.chip is '{chip}' but no bus.spi entry binds it. "
            f"Add a [[bus.spi]] entry with bind = \"flash\", or declare "
            f"chip = \"none\" on a board without a flash chip"
        )


def lines_flash(out, held, table) -> None:
    """The chip selection: whether the board carries one, and which."""
    chip = (held or {}).get("chip", "none")
    out.line(f"#define BDCFG_HAS_FLASH {0 if chip == 'none' else 1}")
    if chip != "none":
        out.line(flag_line(f"BDCFG_FLASH_CHIP_{FLASH_CHIP_MAP[chip]}"))


# ── Assembly ─────────────────────────────────────────────────────────────────

def assemble_header(em) -> str:
    """The header: its MCU include, then a group per table, blank-separated."""
    content = LICENSE + "\n#pragma once\n"
    content += f"\n{em.include}\n\n"

    first = True
    for group in em.groups:
        if not group:
            continue
        if not first:
            content += "\n"
        for line in group:
            content += line + "\n"
        first = False

    return content


def assemble_cmake(em, target: str) -> str:
    """board_config.cmake: the board's variables in the declaration's order."""
    lines = [f'set({name} "{value}")' for name, value in em.cmake.items()]
    lines.append(f'set(TARGET "{target}")')
    return "\n".join(lines)
