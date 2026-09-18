#!/usr/bin/env python3
"""
ThetaGP Protocol Code Generator

Reads protocol/protocol.toml and generates type-safe serialization code:
  - C++   header (protocol/proto.h)  — device side, ArduinoJson v7
  - Rust  module (protocol/proto.rs) — Tauri backend, serde
  - TS    types   (protocol/types.ts) — frontend
  - JSON  manifest (protocol/proto_fields.json) — ordered request/response
          field lists per command, for consumers that must not hand-copy the
          protocol shape: the CDC test suite reads it, the docs table can too
  - MD    table    (protocol/protocol-fields.md) — the response fields of
          every command as the Markdown table the docs point at, so the table a
          reader is handed is derived from the same TOML as the firmware's; it
          sits beside protocol.toml and is tracked, because it is the one
          artifact read by a person rather than compiled by a build
  - C++   header (protocol/proto_resp.h) — response payload field tables in
          declaration order, as X-macros, for the firmware that writes a
          response: the order, the JSON keys and the printf conversions come
          from here instead of from a format string written by hand

Usage:
  python3 scripts/gen_proto.py                       # all targets
  python3 scripts/gen_proto.py --target cpp           # C++ only
  python3 scripts/gen_proto.py --target rust           # Rust only
  python3 scripts/gen_proto.py --target ts             # TS only
  python3 scripts/gen_proto.py --target fields         # field manifest only
  python3 scripts/gen_proto.py --target resp           # response tables only
  python3 scripts/gen_proto.py --target fields-md      # Markdown tables only
  python3 scripts/gen_proto.py --dry-run               # print to stdout
  python3 scripts/gen_proto.py --protocol custom.toml  # custom path

Requires: Python 3.11+ (uses stdlib tomllib)
"""

import argparse
import sys
from pathlib import Path

from proto_gen.emit_cpp import gen_cpp
from proto_gen.emit_fields import gen_fields, gen_fields_md
from proto_gen.emit_resp import gen_resp
from proto_gen.emit_rust import gen_rust
from proto_gen.emit_ts import gen_ts
from proto_gen.model import load_protocol
from proto_gen.validate import (
    validate_command_error_codes,
    validate_domains,
    validate_envelope,
    validate_error_reply,
    validate_field_coverage,
    validate_field_optionality,
    validate_field_roles,
    validate_optional_flags_reached,
    validate_types,
)

# ═════════════════════════════════════════════════════════════════════════════
# CLI
# ═════════════════════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="ThetaGP Protocol Code Generator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 scripts/gen_proto.py
  python3 scripts/gen_proto.py --target cpp --dry-run
  python3 scripts/gen_proto.py --target rust,ts
        """,
    )
    parser.add_argument("--protocol", default="protocol/protocol.toml",
                        help="Path to protocol.toml (default: protocol/protocol.toml)")
    parser.add_argument("--target", default="cpp,rust,ts,fields,resp,fields-md",
                        help="Comma-separated targets: cpp,rust,ts,fields,resp,fields-md (default: all)")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print generated code to stdout instead of writing files")
    parser.add_argument("--outdir-cpp", default="protocol",
                        help="Output dir for C++ generated header")
    parser.add_argument("--outdir-rust", default="protocol",
                        help="Output dir for Rust generated module")
    parser.add_argument("--outdir-ts", default="protocol",
                        help="Output dir for TS generated types")
    parser.add_argument("--outdir-fields", default="protocol",
                        help="Output dir for the generated JSON field manifest")
    parser.add_argument("--outdir-resp", default="protocol",
                        help="Output dir for the generated C++ response field tables")
    parser.add_argument("--outdir-fields-md", default="protocol",
                        help="Output dir for the generated Markdown response field tables")

    args = parser.parse_args()
    targets = [t.strip() for t in args.target.split(",")]
    proto_path = Path(args.protocol)

    if not proto_path.exists():
        print(f"ERROR: Protocol file not found: {proto_path}", file=sys.stderr)
        sys.exit(1)

    proto = load_protocol(str(proto_path))
    validate_domains(proto)
    validate_envelope(proto)
    validate_error_reply(proto)
    validate_types(proto)
    validate_field_roles(proto)
    validate_field_optionality(proto)
    validate_optional_flags_reached(proto)
    validate_field_coverage(proto)
    validate_command_error_codes(proto)

    # Print summary
    types_list = proto.get("types", [])
    cmds_list = proto.get("commands", [])
    enums_list = proto.get("enums", [])
    print(f"ThetaGP Protocol Code Generator", file=sys.stderr)
    print(f"  Source:     {proto_path}", file=sys.stderr)
    print(f"  Types:      {len(types_list)}", file=sys.stderr)
    print(f"  Commands:   {len(cmds_list)}", file=sys.stderr)
    print(f"  Enums:      {len(enums_list)}", file=sys.stderr)
    print(f"  Targets:    {', '.join(targets)}", file=sys.stderr)
    print(file=sys.stderr)

    if args.dry_run:
        out_target = None
    else:
        out_target = object()  # truthy, but we use Path below

    for tgt in targets:
        if tgt == "cpp":
            out_file = None if args.dry_run else Path(args.outdir_cpp) / "proto.h"
            gen_cpp(proto, out_file)
            if args.dry_run:
                print(gen_cpp(proto))
        elif tgt == "rust":
            out_file = None if args.dry_run else Path(args.outdir_rust) / "proto.rs"
            gen_rust(proto, out_file)
            if args.dry_run:
                print(gen_rust(proto))
        elif tgt == "ts":
            out_file = None if args.dry_run else Path(args.outdir_ts) / "types.ts"
            gen_ts(proto, out_file)
            if args.dry_run:
                print(gen_ts(proto))
        elif tgt == "fields":
            out_file = None if args.dry_run else Path(args.outdir_fields) / "proto_fields.json"
            gen_fields(proto, out_file, proto_path)
            if args.dry_run:
                print(gen_fields(proto, source_path=proto_path))
        elif tgt == "resp":
            out_file = None if args.dry_run else Path(args.outdir_resp) / "proto_resp.h"
            gen_resp(proto, out_file)
            if args.dry_run:
                print(gen_resp(proto))
        elif tgt == "fields-md":
            # The Markdown tables are read, not compiled, so they sit beside the
            # TOML they are derived from and in the repository, not with the
            # generated code: docs/ is not tracked, and the one artifact meant
            # for a reader has to be readable in the repository without running
            # this script first.
            out_file = (None if args.dry_run
                        else Path(args.outdir_fields_md) / "protocol-fields.md")
            gen_fields_md(proto, out_file, proto_path)
            if args.dry_run:
                print(gen_fields_md(proto, source_path=proto_path))
        else:
            print(f"WARNING: Unknown target '{tgt}' (supported: cpp, rust, ts, fields, resp, fields-md)",
                  file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
