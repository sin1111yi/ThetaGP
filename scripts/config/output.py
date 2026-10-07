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





# ── USB ──────────────────────────────────────────────────────────────────────


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
    emit.origin = {"factory": "bus_lines", "kind": kind, "template": template,
                   "defaults": dict(defaults)}
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
