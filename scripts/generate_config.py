#!/usr/bin/env python3
"""
ThetaGP Board Configuration Generator

Reads BoardConfig.toml and produces BoardConfig.h + board_config.cmake
with byte-identical output compared to the original Lua pipeline.
"""

import argparse
import os
import sys
import tomllib

from config import (
    validate_config,
    gen_pin_lines,
    gen_keypad_lines,
    gen_usb_lines,
    gen_uart_lines,
    gen_spi_lines,
    gen_flash_lines,
    assemble_header,
    generate_cmake,
)


# =============================================================================
# Config loading & normalization
# =============================================================================

def load_config(target: str, source_dir: str) -> dict:
    """Load BoardConfig.toml for a target."""
    toml_path = os.path.join(source_dir, "configs", target, "BoardConfig.toml")
    if os.path.exists(toml_path):
        with open(toml_path, "rb") as f:
            return tomllib.load(f)

    print(
        f"[ERROR] BoardConfig.toml not found for target '{target}' "
        f"({toml_path})",
        file=sys.stderr,
    )
    sys.exit(1)


def _normalize_pin_array(pins: list) -> list[dict]:
    if not pins:
        return []
    if isinstance(pins[0], str):
        return [{"pin": p} for p in pins]
    return pins


def _normalize_key_map(km):
    if isinstance(km, list):
        if not km:
            return {"columns": 0, "data": []}
        columns = len(km[0])
        data = []
        for row in km:
            data.extend(row)
        return {"columns": columns, "data": data}
    return km


def normalize_config(cfg: dict) -> dict:
    """Convert TOML shorthand forms to canonical internal representation."""
    kp = cfg.get("keypad", {})
    for key in ("drive_pins", "sense_pins", "direct_pins"):
        if key in kp:
            kp[key] = _normalize_pin_array(kp[key])
    if "key_map" in kp:
        kp["key_map"] = _normalize_key_map(kp["key_map"])

    # button_map: 2-D array → {int: str}, or string-keyed dict → {int: str}
    bm = kp.get("button_map", {})
    if isinstance(bm, list):
        kp["button_map"] = {int(e[0]): e[1] for e in bm if isinstance(e, list) and len(e) >= 2}
    elif bm:
        kp["button_map"] = {(int(k) if isinstance(k, str) else k): v for k, v in bm.items()}

    cfg["keypad"] = kp
    return cfg


# =============================================================================
# Main
# =============================================================================

def main() -> None:
    parser = argparse.ArgumentParser(description="ThetaGP Board Config Generator")
    parser.add_argument("--target", default=os.environ.get("TARGET"),
                        help="Board target (e.g. BoringTechH743)")
    parser.add_argument("--source-dir", default=os.environ.get("THETAGP_SOURCE_DIR", "."),
                        help="Project root directory")
    args = parser.parse_args()

    if not args.target:
        print("[ERROR] TARGET is not set. Use --target or set TARGET env var.",
              file=sys.stderr)
        sys.exit(1)

    target = args.target
    source_dir = os.path.abspath(args.source_dir)

    print(f"[INFO] Loading configuration for target: {target}", file=sys.stderr)
    cfg = load_config(target, source_dir)
    cfg = normalize_config(cfg)

    print("[INFO] Validating configuration...", file=sys.stderr)
    errors = validate_config(cfg)
    if errors:
        print("[ERROR] Configuration validation failed:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("[INFO] Configuration validation passed", file=sys.stderr)

    bi = cfg.get("board_info", {})

    print("[INFO] Generating pin macros...", file=sys.stderr)
    pin_lines = gen_pin_lines(cfg)

    print("[INFO] Generating keypad macros...", file=sys.stderr)
    keypad_lines = gen_keypad_lines(cfg.get("keypad"))
    print(f"[INFO]   Keypad: {len(keypad_lines)} macros generated", file=sys.stderr)

    print("[INFO] Generating USB macros...", file=sys.stderr)
    usb_lines = gen_usb_lines(cfg.get("usb"))
    print(f"[INFO]   USB: {len(usb_lines)} macros generated", file=sys.stderr)

    bus = cfg.get("bus")

    print("[INFO] Generating UART macros...", file=sys.stderr)
    uart_lines = gen_uart_lines(bus)
    print(f"[INFO]   UART: {len(uart_lines)} macros generated", file=sys.stderr)

    print("[INFO] Generating SPI flash macros...", file=sys.stderr)
    spi_lines = gen_spi_lines(bus)
    print(f"[INFO]   SPI flash: {len(spi_lines)} macros generated", file=sys.stderr)

    print("[INFO] Generating flash chip macros...", file=sys.stderr)
    flash_lines = gen_flash_lines(cfg.get("flash"))
    print(f"[INFO]   Flash: {len(flash_lines)} macros generated", file=sys.stderr)

    header_content = assemble_header(
        bi.get("mcu_series", ""), bi,
        pin_lines, keypad_lines, usb_lines, uart_lines, spi_lines,
        flash_lines,
    )
    cmake_content = generate_cmake(bi, target)

    header_path = os.path.join(source_dir, "configs", target, "BoardConfig.h")
    cmake_path = os.path.join(source_dir, "configs", target, "board_config.cmake")

    print(f"[INFO] Writing {header_path} ...", file=sys.stderr)
    with open(header_path, "w") as f:
        f.write(header_content)
    print(f"[INFO]   Done ({len(header_content)} bytes)", file=sys.stderr)

    print(f"[INFO] Writing {cmake_path} ...", file=sys.stderr)
    with open(cmake_path, "w") as f:
        f.write(cmake_content)
    print(f"[INFO]   Done ({len(cmake_content)} bytes)", file=sys.stderr)

    print(
        f"[INFO] Configuration generated successfully for target: {target}",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
