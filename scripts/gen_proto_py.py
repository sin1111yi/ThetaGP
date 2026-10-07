#!/usr/bin/env python3

import argparse
import glob
import hashlib
import json
import os
import os.path
import re
import shutil
import subprocess
import sys
from typing import NoReturn

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CMAKE_LISTS = os.path.join(REPO_ROOT, "src", "CMakeLists.txt")
DEFAULT_OUT_DIR = os.path.join(REPO_ROOT, "build", "proto_py")

PROTOBUF_PYTHONPATH_ENV = "PROTOBUF_PYTHONPATH"
SUGGESTED_VENV = os.path.join(os.path.expanduser("~"), ".venvs", "thetagp-tools")

# The digest of the schema the bindings beside it were generated from. The tool
# that reads them holds the schema to it, so this file is what tells a run that
# the reply it is about to read belongs to the schema it was read by.
MANIFEST_NAME = "proto_schema.sha256.json"

def eprint(*parts):
    print(*parts, file=sys.stderr)

def fail(message, code) -> NoReturn:
    eprint("gen_proto_py: " + message)
    sys.exit(code)

def scheme_from_cmake():
    """The schemas src/CMakeLists.txt names in PROTO_SCHEMAS, as paths under the
    repository root.

    A schema is named there as "<variable>/<path>", and the variables are set
    earlier in the same file relative to the source tree: the schema is fetched
    into a directory of its own, so where it sits is one of that file's
    variables and not a fixed path.
    """
    try:
        with open(CMAKE_LISTS, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as error:
        fail("cannot read %s: %s" % (CMAKE_LISTS, error), 2)

    match = re.search(r"\bset\(\s*PROTO_SCHEMAS\b(.*?)\n\s*\)", text, re.S)
    if match is None:
        fail("no set(PROTO_SCHEMAS ...) block in %s: name the schemas with "
             "--protos" % CMAKE_LISTS, 2)

    body = re.sub(r"#[^\n]*", "", match.group(1))

    # The directories the block can lean on, each one a path under the tree.
    directories = {"CMAKE_SOURCE_DIR": ""}
    for name, tail in re.findall(
            r'\bset\(\s*([A-Za-z_]\w*)\s+"\$\{CMAKE_SOURCE_DIR\}/([^"]*)"\s*\)',
            text):
        directories[name] = tail

    paths = []
    for name, tail in re.findall(r'\$\{([A-Za-z_]\w*)\}/([^"\s)]+\.proto)',
                                 body):
        if name not in directories:
            fail("PROTO_SCHEMAS in %s names a schema under ${%s}, and no "
                 "set(... \"${CMAKE_SOURCE_DIR}/...\") in that file says where "
                 "that is: name the schemas with --protos"
                 % (CMAKE_LISTS, name), 2)
        paths.append(os.path.join(directories[name], tail))
    if not paths:
        fail("PROTO_SCHEMAS in %s names no .proto file: name the schemas with "
             "--protos" % CMAKE_LISTS, 2)
    return paths

def inside_work_tree(path):
    """True when the path is the work tree itself or something under it."""
    root = os.path.abspath(REPO_ROOT)
    return os.path.commonpath([root, os.path.abspath(path)]) == root

def git_ignores(path):
    """True, False, or None when git cannot answer (no git, not a work tree, or
    a path outside it).

    A path outside the work tree is one no commit of this repository holds, so
    git is not asked whether this repository ignores it: None says that, and
    False keeps saying "inside the tree, and this repository would commit it" --
    the caller reads the two apart, which is what lets it name a directory
    outside the tree as somewhere the bindings may go.
    """
    if shutil.which("git") is None:
        return None
    probe = subprocess.run(["git", "-C", REPO_ROOT, "rev-parse",
                            "--is-inside-work-tree"],
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if probe.returncode != 0:
        return None
    if not inside_work_tree(path):
        return None
    check = subprocess.run(["git", "-C", REPO_ROOT, "check-ignore", "-q", "--",
                            os.path.relpath(path, REPO_ROOT)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return check.returncode == 0

def write_manifest(schemas, out_dir):
    """Write the digest of each schema beside the bindings generated from it.

    A schema is one revision of a package the board also carries, and the
    bindings are generated from one revision while a host may hold another:
    reading a reply is then a claim about which revision read it. The manifest
    is what a run holds that claim to, and the tool refuses a schema that does
    not hash to it. Names are basenames, because a set of schemas is one
    directory read against one include path, and the entries are sorted so two
    runs over the same bytes write the same file.
    """
    entries = []
    combined = hashlib.sha256()
    for path in sorted(schemas, key=os.path.basename):
        with open(path, "rb") as handle:
            data = handle.read()
        combined.update(data)
        entries.append({"name": os.path.basename(path),
                        "sha256": hashlib.sha256(data).hexdigest()})

    manifest = {"schemas": entries, "combined_sha256": combined.hexdigest()}
    with open(os.path.join(out_dir, MANIFEST_NAME), "w",
              encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2, sort_keys=True)
        handle.write("\n")
    print("digest  %s: %d schema(s), combined %s"
          % (MANIFEST_NAME, len(entries), manifest["combined_sha256"][:16]))

def pythonpath_entries():
    raw = os.environ.get(PROTOBUF_PYTHONPATH_ENV, "")
    return [part for part in raw.split(os.pathsep) if part]

def protobuf_report():
    """Where python-protobuf comes from, or None when there is none."""
    for entry in reversed(pythonpath_entries()):
        if entry not in sys.path:
            sys.path.insert(0, entry)
    try:
        import google.protobuf as runtime  # noqa: F401  (the probe is the point)
    except ImportError:
        return None
    location = getattr(runtime, "__file__", None) or "?"
    version = getattr(runtime, "__version__", "?")
    return version, location

def runtime_missing_message():
    venv_python = os.path.join(SUGGESTED_VENV, "bin", "python")
    site_packages = sorted(glob.glob(os.path.join(SUGGESTED_VENV, "lib",
                                                  "python*", "site-packages")))
    lines = [
        "the bindings are written, but this interpreter "
        "cannot import google.protobuf:",
        "  interpreter: %s" % sys.executable,
        "  %s: %s" % (PROTOBUF_PYTHONPATH_ENV,
                     os.environ.get(PROTOBUF_PYTHONPATH_ENV) or "(not set)"),
        "a binding no interpreter imports is not a binding. Install the "
        "runtime outside the repository and run the consumer with it:",
        "  python3 -m venv %s" % SUGGESTED_VENV,
        "  %s -m pip install protobuf" % venv_python,
        "  %s scripts/tools/thetagp.py ..." % venv_python,
        "or point this convention at an existing site-packages directory "
        "holding google/protobuf:",
        "  %s=%s scripts/tools/thetagp.py ..."
        % (PROTOBUF_PYTHONPATH_ENV,
           site_packages[-1] if site_packages
           else os.path.join(SUGGESTED_VENV, "lib", "pythonX.Y", "site-packages")),
    ]
    return "\n".join(lines)

def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Generate the Python bindings of protocol/*.proto.")
    parser.add_argument("--protos", nargs="+", metavar="SCHEMA",
                        help="the schemas to generate from; when it is not "
                             "given, the set comes from PROTO_SCHEMAS in "
                             "src/CMakeLists.txt")
    parser.add_argument("--out-dir", default=DEFAULT_OUT_DIR, metavar="DIR",
                        help="where the bindings go "
                             "(default: %(default)s); it must be a directory "
                             "git ignores")
    parser.add_argument("--protoc", default=None, metavar="PATH",
                        help="the protoc to run (default: the one on PATH)")
    parser.add_argument("--no-runtime-check", action="store_true",
                        help="write the bindings even when "
                             "no python-protobuf runtime is importable")
    return parser.parse_args(argv)

def main(argv):
    args = parse_args(argv)

    if args.protos:
        schemas = [path if os.path.isabs(path) else os.path.join(REPO_ROOT, path)
                   for path in args.protos]
    else:
        schemas = [os.path.join(REPO_ROOT, path)
                   for path in scheme_from_cmake()]

    missing = [path for path in schemas if not os.path.isfile(path)]
    if missing:
        fail("not there: %s" % ", ".join(missing), 2)
    directories = sorted({os.path.dirname(os.path.abspath(path))
                          for path in schemas})
    if len(directories) != 1:
        fail("the schemas must sit in one directory, since protoc resolves "
             "their imports against one include path; these sit in %s"
             % ", ".join(directories), 2)
    schema_dir = directories[0]

    protoc = args.protoc or shutil.which("protoc")
    if not protoc:
        fail("protoc is not on PATH and --protoc was not given: the python "
             "plugin of protoc is what writes the bindings (Arch: pacman -S "
             "protobuf)", 2)
    version = subprocess.run([protoc, "--version"], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True)
    protoc_version = version.stdout.strip() or "unknown"

    out_dir = os.path.abspath(args.out_dir)
    ignored = git_ignores(out_dir)
    if ignored is False:
        fail("--out-dir %s is inside the work tree and git does not ignore it, "
             "so the generated bindings would be committable. Write somewhere "
             "ignored (build/proto_py/ is) or outside the repository."
             % out_dir, 2)
    os.makedirs(out_dir, exist_ok=True)

    # One protoc call for the whole set: the schemas import each other, so they
    # are read in one pass against one include path. protoc resolves each
    command = [protoc, "--python_out=" + out_dir, "-I", schema_dir] + \
              [os.path.basename(path) for path in schemas]
    run = subprocess.run(command, cwd=schema_dir, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    if run.stdout.strip():
        print(run.stdout.rstrip())

    bindings = []
    refused = []
    for path in schemas:
        stem = os.path.splitext(os.path.basename(path))[0]
        binding = os.path.join(out_dir, stem + "_pb2.py")
        if os.path.isfile(binding) and os.path.getsize(binding) > 0:
            bindings.append((stem, binding))
        else:
            refused.append(stem)
    if run.returncode != 0 or refused:
        fail("protoc wrote no binding for: %s (exit %d)"
             % (", ".join(refused) or "nothing named", run.returncode), 1)

    print("bindings in %s (git ignores this directory: %s)"
          % (out_dir, "yes" if ignored else "not checked"))

    # Beside the bindings they were generated from: a host that reads them
    # holds the schema to this, and a schema it does not describe is refused
    # rather than read into the wrong fields.
    write_manifest(schemas, out_dir)

    report = protobuf_report()
    if report is None:
        eprint("gen_proto_py: " + runtime_missing_message())
        if not args.no_runtime_check:
            sys.exit(3)
        print("python-protobuf runtime: MISSING (not checked, "
              "--no-runtime-check)")
    else:
        print("python-protobuf runtime: %s (%s)" % (report[0], report[1]))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
