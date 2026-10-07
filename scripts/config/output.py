"""
The lines a board's declaration becomes where they are not one per field.

A key matrix is read as a whole, a bus carries a descriptor table every
driver indexes, a report rate is held to the link it travels on: each of those
names its checks and its lines here, and the schema's row points at them.
"""

from .engine import flag_line
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
