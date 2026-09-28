#!/usr/bin/env python3
# This file is a part of ThetaGP.
#

import argparse
import os
import os.path
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

DEFAULT_OUT_DIR = os.path.join(REPO_ROOT, "protocol")
GENERATOR = os.path.join(REPO_ROOT, "lib", "nanopb", "generator",
                         "nanopb_generator.py")

def outputs_for(proto):
    """The pair a schema generates, as names inside the output directory."""
    stem = os.path.splitext(os.path.basename(proto))[0]
    return (stem + ".pb.c", stem + ".pb.h")

def find_protobuf_runtime(explicit):
    """The directory to take python-protobuf from, or None to use the one the"""
    try:
        import google.protobuf  # noqa: F401
        return None
    except ImportError:
        pass

    for candidate in (explicit, os.environ.get("PROTOBUF_PYTHONPATH")):
        if not candidate:
            continue
        if os.path.isfile(os.path.join(candidate, "google", "protobuf",
                                       "__init__.py")):
            return candidate
    return False

def main():
    parser = argparse.ArgumentParser(
        description="Generate the C codecs of protobuf schemas.")
    parser.add_argument("--protos", nargs="+", metavar="SCHEMA", required=True,
                        help="the schemas to generate from; each produces the "
                             "pair named after it in --out-dir. Required: the "
                             "set of schemas is the build's own manifest "
                             "(PROTO_SCHEMAS in src/CMakeLists.txt)")
    parser.add_argument("--out-dir", default=None,
                        help="where the generated files go "
                             f"(default: {DEFAULT_OUT_DIR})")
    parser.add_argument("--protobuf-path", default=None,
                        help="a site-packages directory holding "
                             "google/protobuf, when the interpreter running "
                             "this script cannot import it")
    args = parser.parse_args()

    protos = [os.path.abspath(proto) for proto in args.protos]
    out_dir = os.path.abspath(args.out_dir or DEFAULT_OUT_DIR)

    for proto in protos:
        if not os.path.isfile(proto):
            print(f"ERROR: {proto} is not there", file=sys.stderr)
            return 2

    # One output directory holds every pair, so two schemas of the same name
    # would claim one pair and the second would overwrite the first.
    pairs = []
    claimed = {}
    for proto in protos:
        pair = outputs_for(proto)
        for name in pair:
            if name in claimed:
                print(f"ERROR: {proto} and {claimed[name]} would both write "
                      f"{os.path.join(out_dir, name)}", file=sys.stderr)
                return 2
            claimed[name] = proto
        pairs.append((proto, pair))

    if not os.path.isfile(GENERATOR):
        print(f"ERROR: {GENERATOR} is not there — the nanopb checkout is "
              "fetched by the firmware build (cmake -B build)", file=sys.stderr)
        return 2
    if shutil.which("protoc") is None:
        print("ERROR: protoc is not on PATH (the nanopb generator calls it "
              "for the schema it is given)", file=sys.stderr)
        return 2

    runtime = find_protobuf_runtime(args.protobuf_path)
    if runtime is False:
        print("ERROR: no python-protobuf runtime — the interpreter running "
              "this script cannot import google.protobuf, and neither "
              "$PROTOBUF_PYTHONPATH nor --protobuf-path names a directory "
              "that holds it (pip install protobuf, or point one at a "
              "site-packages that has it)", file=sys.stderr)
        return 2

    env = os.environ.copy()
    env["TEMPORARILY_DISABLE_PROTOBUF_VERSION_CHECK"] = "true"
    if runtime:
        env["PYTHONPATH"] = (runtime + os.pathsep + env["PYTHONPATH"]
                             if env.get("PYTHONPATH") else runtime)

    os.makedirs(out_dir, exist_ok=True)

    failed = []
    with tempfile.TemporaryDirectory(prefix="thetagp_gen_proto_pb_") as tmp:
        # The generator builds the python bindings of nanopb's own options file
        # beside its sources by default; the temp directory keeps that write
        env["NANOPB_PB2_TEMP_DIR"] = tmp

        # One call per schema: the generator takes a single include directory,
        # the schema's own, and one call per schema is also what names the
        for proto, pair in pairs:
            cmd = [sys.executable, GENERATOR, "-I", os.path.dirname(proto),
                   f"--output-dir={out_dir}", proto]

            prefix = f"PROTOBUF_PYTHONPATH={runtime} " if runtime else ""
            print("$ " + prefix + " ".join(cmd))
            result = subprocess.run(cmd, cwd=REPO_ROOT, env=env,
                                    capture_output=True, text=True)
            if result.stdout:
                print(result.stdout, end="")
            if result.stderr:
                print(result.stderr, end="", file=sys.stderr)
            if result.returncode != 0:
                print(f"ERROR: the nanopb generator failed on {proto} "
                      f"(exit {result.returncode})", file=sys.stderr)
                failed.append(proto)
                continue

            # An empty file is not a generated one: a write that was cut short
            # leaves zero bytes behind at a name that looks written, and a
            for name in pair:
                path = os.path.join(out_dir, name)
                if not os.path.isfile(path):
                    print(f"ERROR: the generator wrote no {path}",
                          file=sys.stderr)
                    failed.append(proto)
                    break
                if os.path.getsize(path) == 0:
                    print(f"ERROR: the generator wrote an empty {path}",
                          file=sys.stderr)
                    failed.append(proto)
                    break

    if failed:
        print("ERROR: no pair was written for " + ", ".join(failed),
              file=sys.stderr)
        return 1

    for proto, pair in pairs:
        print("wrote " + ", ".join(
            f"{os.path.join(out_dir, name)} "
            f"({os.path.getsize(os.path.join(out_dir, name))} B)"
            for name in pair))
    return 0

if __name__ == "__main__":
    sys.exit(main())
