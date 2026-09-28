#!/usr/bin/env python3
# This file is a part of ThetaGP.
#
# ThetaGP is free software: you can redistribute it
# and/or modify it under the terms of the GNU General
# Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# ThetaGP is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
#
# Generator: the config key table the firmware compiles
# Input:  configs/config_keys.toml        (the config domain's own declaration)
# Output: configs/config_keys.gen.h       (generated, ignored: .gitignore)
# Method: reads the declaration and writes it back as C++ — one row per declared
#         field, in the order the declaration lists them, with the field's
#         position in ConfigStore, its type and element count, its accepted
#         range and its flags.
# Usage:  python3 scripts/config/gen_config_keys.py [--declaration FILE]
#                                                  [--out FILE] [--dry-run]
# Expect: exit 0 = the header was written (with --dry-run: printed to stdout).
# Error:  exit 1 = the declaration is there but unusable, or the write failed;
#         exit 2 = the declaration is not there, so nothing was run.
#
# Why this is its own entry point: the config key table is the config domain's,
# not the protocol's. It is read from configs/config_keys.toml and written
# beside it, and this script reads no protocol source and imports nothing from
# the protocol generator — an edit over there cannot change what is written
# here.
#
# Who calls it: the configure-time step in src/CMakeLists.txt, and the two host
# suites scripts/test/test_profile_keys.py and scripts/test/test_profile_version.py,
# which compile the firmware's own config sources against the header. The header
# is included by src/gamepad/config/config_store.cpp and key_table.cpp as
# "configs/config_keys.gen.h".
#
# It imports the standard library only, and reads TOML through tomllib (3.11+).

"""
gen_config_keys — configs/config_keys.gen.h, the config key table as generated code.

Reads configs/config_keys.toml, the config domain's own source. A row names one
field of the store under one name, and that name is the whole identity of the
field: the protocol key a caller sends and the path its profile body carries are
this same string, so neither is written anywhere else, and the C++ side of the
field is its last segment.

The table emitted below carries every field of ConfigStore, in the order the
declaration lists them, with the field's position in the store (spelled through
offsetof, so a declared key with no such field does not build), the type and
element count of a value of it, the range it accepts and its flags. A row the
declaration marks exposed carries kKeyFlagExposed: those are the fields the
control protocol accepts, and their count is emitted beside the table so the key
list reply is sized for the list it writes.

The declaration is separate from the protocol spec, and this emitter reads only
its own TOML: a row names the consumer's field, and the spec may not know the
consumer (protocol/README.md, design principles).

Where a fact here also lives in the firmware — a factory default in
conf/ThetaGP_Config.h, say — this declaration is the home it is moving to; until
that move is done the two are held equal by hand, and a difference is a defect
in one of them, not a free choice.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import tomllib
from datetime import datetime
from pathlib import Path
from typing import Dict, List, NoReturn, Optional

# The repository root, from this file's own position: scripts/config/<this file>.
REPO_ROOT = Path(__file__).resolve().parents[2]

# The declaration, relative to the repository root.
CONFIG_KEYS_PATH = "configs/config_keys.toml"

# Where the generated header is written, relative to the repository root. It
# sits beside the declaration it comes from, and the build reaches it as
# "configs/config_keys.gen.h" — the repository root is on the include path, the
# same way "protocol/ThetaGP.pb.h" is reached.
CONFIG_KEYS_OUT = "configs/config_keys.gen.h"

# A declared type and the KeyType of the table. `bool` travels as a byte, which
# is what its range of 0..1 is checked against.
TYPE_MAP = {
    "u8": "U8",
    "u16": "U16",
    "i16": "I16",
    "bool": "U8",
    "u8_array": "U8Array",
}

# Element width in bytes, by declared type. Held equal to the widths the
# firmware's keyTypeWidth() carries; a type whose width differs is a defect in
# one of the two, and the store-bound assert in key_table.cpp is what notices.
TYPE_WIDTH = {"u8": 1, "u16": 2, "i16": 2, "bool": 1, "u8_array": 1}

# A declared flag and the constant the table carries it as.
FLAG_MAP = {
    "reboot": "kKeyFlagRequiresReboot",
    "accepts_unmapped": "kKeyFlagAcceptsUnmapped",
}

# The flag the declaration's `exposed` column becomes.
EXPOSED_FLAG = "kKeyFlagExposed"

# A default the board supplies rather than the declaration: the board's own
# configuration is where the value lives, and the firmware reads it there.
DEFAULT_FROM_BOARD = "board"


def fail(*lines: str) -> NoReturn:
    """Report a failure — one line, or several — and stop with exit 1.

    Every line reaches stderr as it stands and the run then ends non-zero, so a
    fault is reported by one call and nothing else. The text is the caller's:
    the `ERROR: ` prefix and the indentation of a detail line are part of the
    strings the site that knows the fault builds.
    """
    for line in lines:
        print(line, file=sys.stderr)
    sys.exit(1)


def file_sha256(path: Path) -> str:
    """SHA-256 of a file's bytes — the digest a generated artifact records.

    Used to fingerprint the TOML a generated artifact was derived from, so a
    consumer can tell an artifact that matches its source from one left behind
    by a later edit to that source. A path that is not absolute is taken from
    the repository root, the same as load_config_keys(), so the digest does not
    depend on the directory the generator was run from.
    """
    source = Path(path)
    if not source.is_absolute():
        source = REPO_ROOT / source
    return hashlib.sha256(source.read_bytes()).hexdigest()


def display_path(path: Path) -> str:
    """The path a generated header records, as the repository sees it.

    The header names its source relative to the repository root, whatever path
    the script was handed and whatever directory it was run from: the artifact
    then reads the same to every consumer instead of carrying the spelling of
    one invocation.
    """
    try:
        return path.resolve().relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return str(path)


def load_config_keys(path: str = CONFIG_KEYS_PATH) -> dict:
    """The declaration as a mapping, read through tomllib (3.11+).

    A path that is not absolute is taken from the repository root, so the call
    means the declaration whether it comes from the configure step or from a
    suite run out of another directory. TOML that does not parse is reported as
    a fault of this declaration rather than as a traceback: it is the one thing
    a reader of the configure log has to see.
    """
    declaration = Path(path)
    if not declaration.is_absolute():
        declaration = REPO_ROOT / declaration
    try:
        with open(declaration, "rb") as f:
            return tomllib.load(f)
    except tomllib.TOMLDecodeError as err:
        fail(f"ERROR: config keys — {display_path(declaration)}: {err}")


def validate_config_keys(keys: dict) -> None:
    """Report what makes the declaration unusable and stop.

    The checks are the ones a generator can decide on its own: that a name has
    the shape a consumer splits, that two rows do not claim the same C++ name,
    that a type is one the table carries, that a range is a range, and that a
    flag is one that exists. Whether the field a row names is really a field of
    ConfigStore is not decidable here — offsetof in the generated table is what
    decides it, at compile time.
    """
    problems: List[str] = []
    fields = keys.get("field") or []
    if not fields:
        fail("ERROR: config keys — the declaration carries no field")

    leaves: Dict[str, str] = {}
    for index, field in enumerate(fields):
        name = field.get("name")
        where = f"field {index}"
        if not isinstance(name, str) or "." not in name:
            problems.append(f"{where}: name must be <domain>.<leaf>, got {name!r}")
            continue
        where = name
        domain, leaf = name.rsplit(".", 1)
        if not domain:
            problems.append(f"{where}: name carries an empty domain")
        if not leaf.isidentifier():
            problems.append(f"{where}: leaf {leaf!r} is not a C++ identifier")
        elif leaf in leaves:
            problems.append(
                f"{where}: leaf {leaf!r} is already the name of {leaves[leaf]} — "
                f"one leaf is one field, and the table spells both")
        else:
            leaves[leaf] = name

        type_name = field.get("type")
        if type_name not in TYPE_MAP:
            problems.append(f"{where}: unknown type {type_name!r}")
            continue
        count = field.get("count", 1)
        if not isinstance(count, int) or count < 1:
            problems.append(f"{where}: count must be a positive integer")
        if type_name == "u8_array" and count == 1:
            problems.append(f"{where}: an array carries more than one element")
        if type_name != "u8_array" and "count" in field:
            problems.append(f"{where}: a scalar carries no element count")

        low, high = field.get("min"), field.get("max")
        if not isinstance(low, int) or not isinstance(high, int):
            problems.append(f"{where}: min and max are integers")
        elif low > high:
            problems.append(f"{where}: min {low} is above max {high}")

        default = field.get("default")
        if default is None:
            problems.append(f"{where}: no default")
        elif default == DEFAULT_FROM_BOARD:
            if type_name != "u8_array":
                problems.append(
                    f"{where}: only the board's button table comes from the board")
        elif not isinstance(default, int):
            problems.append(f"{where}: default must be an integer")
        elif isinstance(low, int) and isinstance(high, int) and not low <= default <= high:
            problems.append(f"{where}: default {default} is outside {low}..{high}")

        for flag in field.get("flags", []):
            if flag not in FLAG_MAP:
                problems.append(f"{where}: unknown flag {flag!r}")
        if "exposed" in field and not isinstance(field["exposed"], bool):
            problems.append(f"{where}: exposed must be true or false")
        if "doc" not in field:
            problems.append(f"{where}: no doc line — the declaration is what a "
                            f"reader is handed for this field")

    if problems:
        fail(*[f"ERROR: config keys — {problem}" for problem in problems])


def enum_name(leaf: str) -> str:
    """The enumerator a leaf is named by: every `_`-separated word capitalised."""
    return "".join(word[:1].upper() + word[1:] for word in leaf.split("_") if word)


def flag_expression(field: dict) -> str:
    """The flags a row carries, as the C++ expression the table holds."""
    names = [FLAG_MAP[flag] for flag in field.get("flags", [])]
    if field.get("exposed"):
        names.insert(0, EXPOSED_FLAG)
    return " | ".join(names) if names else "0"


def gen_config_keys(keys: dict, out: Optional[Path] = None,
                    source_path: str = CONFIG_KEYS_PATH) -> str:
    """The config key table the firmware compiles, as C++.

    Every field takes a row: a profile body is read and written through this
    table, so it spans the whole store, and kKeyFlagExposed marks the rows the
    control protocol accepts.
    """
    fields = keys.get("field", [])
    exposed_count = sum(1 for field in fields if field.get("exposed"))

    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

    w("// =============================================================================")
    w("// Auto-generated by scripts/config/gen_config_keys.py — DO NOT EDIT MANUALLY")
    w(f"// Source: {source_path} (sha256 {file_sha256(Path(source_path))})")
    w(f"// Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    w("// =============================================================================")
    w("#pragma once")
    w()
    w('#include "gamepad/config/config_store.h"')
    w('#include "gamepad/config/key_table.h"')
    w()
    w("#include <cstddef>")
    w("#include <cstdint>")
    w()
    w("namespace ThetaGP::Gamepad::Config {")
    w()
    w("// Where a field sits in the table below, in table order. An enumerator is")
    w("// the field's name without its domain, so the code names the row of a field")
    w("// by the field: position and row come from one declaration and cannot drift.")
    w("enum class ConfigKey : uint8_t {")
    for field in fields:
        w(f"  {enum_name(field['name'].rsplit('.', 1)[1])},")
    w("  Count,")
    w("};")
    w()
    w("// Every field of the store, in the order the declaration lists them. A")
    w("// field's name is the whole identity of that field: what a caller sends")
    w("// (config.get_key, config.set_key, config.list_keys) and the path a profile")
    w("// body carries for it are one string, and the leaf a body writes is its last")
    w("// segment. The code side of the field is that segment too, which is what")
    w("// offsetof below spells. A row the protocol accepts carries")
    w("// kKeyFlagExposed; the rest are carried by a profile and reached by no")
    w("// command.")
    w("inline constexpr KeyEntry kKeyTable[] = {")
    for field in fields:
        leaf = field["name"].rsplit(".", 1)[1]
        type_name = field["type"]
        count = field.get("count", 1)
        w(f"    // {field['name']} — {field['doc']}")
        w(f'    {{"{field["name"]}", offsetof(ConfigStore, {leaf}), '
          f"KeyType::{TYPE_MAP[type_name]},")
        w(f"     {count}, {field['min']}, {field['max']}, {flag_expression(field)}}},")
    w("};")
    w()
    w("// The entry of a field, by the position the enum names.")
    w("constexpr const KeyEntry &keyEntry(ConfigKey which) {")
    w("  return kKeyTable[static_cast<uint8_t>(which)];")
    w("}")
    w()
    w("inline constexpr uint8_t kKeyTableCount =")
    w("    static_cast<uint8_t>(sizeof(kKeyTable) / sizeof(kKeyTable[0]));")
    w()
    w("static_assert(kKeyTableCount == static_cast<uint8_t>(ConfigKey::Count),")
    w('              "config keys: a generated row carries no position");')
    w()
    w("// The rows the control protocol accepts: what config.list_keys answers with,")
    w("// and what its reply buffer is sized for.")
    w(f"inline constexpr uint8_t kExposedKeyCount = {exposed_count};")
    w()
    w("} // namespace ThetaGP::Gamepad::Config")
    w()

    text = "\n".join(lines)
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    return text


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate configs/config_keys.gen.h from the config key "
                    "declaration.")
    parser.add_argument("--declaration", default=None,
                        help="path to the config key declaration "
                             f"(default: {CONFIG_KEYS_PATH})")
    parser.add_argument("--out", default=None,
                        help="path of the generated header "
                             f"(default: {CONFIG_KEYS_OUT})")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the header to stdout instead of writing it")
    args = parser.parse_args()

    declaration = Path(args.declaration) if args.declaration else Path(CONFIG_KEYS_PATH)
    if not declaration.is_absolute():
        declaration = REPO_ROOT / declaration
    if not declaration.is_file():
        print(f"ERROR: {declaration} is not there — it is the declaration the "
              "config key table is generated from, and the firmware includes "
              "the header derived from it", file=sys.stderr)
        return 2

    keys = load_config_keys(str(declaration))
    validate_config_keys(keys)

    if args.dry_run:
        print(gen_config_keys(keys, source_path=display_path(declaration)))
        return 0

    out = Path(args.out) if args.out else Path(CONFIG_KEYS_OUT)
    if not out.is_absolute():
        out = REPO_ROOT / out
    gen_config_keys(keys, out, display_path(declaration))
    print(f"Generated {display_path(out)} from {display_path(declaration)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
