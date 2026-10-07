#!/usr/bin/env python3
"""
ThetaGP Board Configuration Generator

Reads BoardConfig.toml and produces BoardConfig.h + board_config.cmake.
"""

import argparse
import os
import sys
import tomllib

from config import BOARD_SCHEMA, assemble_cmake, assemble_header, emit, validate


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

    print("[INFO] Validating configuration...", file=sys.stderr)
    errors = validate(BOARD_SCHEMA, cfg)
    if errors:
        print("[ERROR] Configuration validation failed:", file=sys.stderr)
        for e in errors:
            print(f"  - {e}", file=sys.stderr)
        sys.exit(1)
    print("[INFO] Configuration validation passed", file=sys.stderr)

    print("[INFO] Generating macros...", file=sys.stderr)
    emitted = emit(BOARD_SCHEMA, cfg)
    for table, group in zip(BOARD_SCHEMA, emitted.groups):
        if group:
            print(f"[INFO]   {table.where}: {len(group)} lines", file=sys.stderr)

    header_content = assemble_header(emitted)
    cmake_content = assemble_cmake(emitted, target)

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
