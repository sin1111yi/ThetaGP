"""
gen_config_keys — configs/config_keys.gen.h, the config key table as generated code.

Reads configs/config_keys.toml, the config domain's own source. A row names one
field of the store under one name, and that name is the whole identity of the
field: the protocol key a caller sends and the path its profile body carries are
this same string, so neither is written anywhere else, and the C++ side of the
field is its last segment.

The table emitted below carries the keys the control protocol accepts, in the
order the protocol lists them, with the field's position in ConfigStore (spelled
through offsetof, so a declared key with no such field does not build), the type
and element count of a value of it, the range it accepts and its flags.

The declaration is separate from the protocol spec, and this emitter reads only
its own TOML: a row names the consumer's field, and the spec may not know the
consumer (protocol/README.md, design principles).

Where a fact here also lives in the firmware — a factory default in
conf/ThetaGP_Config.h, say — this declaration is the home it is moving to; until
that move is done the two are held equal by hand, and a difference is a defect
in one of them, not a free choice.
"""

from __future__ import annotations

import tomllib
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

from .model import fail, file_sha256

# The declaration, relative to the repository root.
CONFIG_KEYS_PATH = "configs/config_keys.toml"

# Where the generated header is written, relative to the repository root. It
# sits beside the declaration it comes from, and the build reaches it as
# "configs/config_keys.gen.h" — the repository root is on the include path, the
# same way "protocol/proto_resp.h" is reached.
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

# A default the board supplies rather than the declaration: the board's own
# configuration is where the value lives, and the firmware reads it there.
DEFAULT_FROM_BOARD = "board"


def load_config_keys(path: str = CONFIG_KEYS_PATH) -> dict:
    with open(path, "rb") as f:
        return tomllib.load(f)


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
        if "doc" not in field:
            problems.append(f"{where}: no doc line — the declaration is what a "
                            f"reader is handed for this field")

    if problems:
        fail(*[f"ERROR: config keys — {problem}" for problem in problems])


def enum_name(leaf: str) -> str:
    """The enumerator a leaf is named by: every `_`-separated word capitalised."""
    return "".join(word[:1].upper() + word[1:] for word in leaf.split("_") if word)


def gen_config_keys(keys: dict, out: Optional[Path] = None,
                    source_path: str = CONFIG_KEYS_PATH) -> str:
    """The config key table the firmware compiles, as C++.

    Only the fields the declaration marks exposed take a row: the table is what
    `config.*` accepts, and a field the firmware carries but no command reaches
    is not a key yet.
    """
    fields = keys.get("field", [])
    exposed = [f for f in fields if f.get("exposed")]

    lines: List[str] = []

    def w(line: str = "") -> None:
        lines.append(line)

    w("// =============================================================================")
    w("// Auto-generated by scripts/gen_proto.py — DO NOT EDIT MANUALLY")
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
    w("// Where a key sits in the table below, in table order. An enumerator is the")
    w("// key's name without its domain, so the code names the row of a field by the")
    w("// field: position and row come from one declaration and cannot drift.")
    w("enum class ConfigKey : uint8_t {")
    for field in exposed:
        w(f"  {enum_name(field['name'].rsplit('.', 1)[1])},")
    w("  Count,")
    w("};")
    w()
    w("// The keys this build accepts, in the order the control protocol lists them.")
    w("// A key's name is the field's name: what a caller sends (config.get_key,")
    w("// config.set_key, config.list_keys) and the path a profile body carries for")
    w("// that field are one string, and the leaf a body writes is its last segment.")
    w("// The code side of the field is that segment too, which is what offsetof")
    w("// below spells.")
    w("inline constexpr KeyEntry kKeyTable[] = {")
    for field in exposed:
        leaf = field["name"].rsplit(".", 1)[1]
        type_name = field["type"]
        count = field.get("count", 1)
        flags = field.get("flags", [])
        flag_expr = " | ".join(FLAG_MAP[f] for f in flags) if flags else "0"
        w(f"    // {field['name']} — {field['doc']}")
        w(f'    {{"{field["name"]}", offsetof(ConfigStore, {leaf}), '
          f"KeyType::{TYPE_MAP[type_name]},")
        w(f"     {count}, {field['min']}, {field['max']}, {flag_expr}}},")
    w("};")
    w()
    w("// The entry of a key, by the position the enum names.")
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
    w("} // namespace ThetaGP::Gamepad::Config")
    w()

    text = "\n".join(lines)
    if out is not None:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    return text
