# ThetaGP Protocol Definition Format

`protocol/protocol.toml` is the **single source of truth** for all CDC JSON protocol
types, commands, constants, and error codes used between ThetaGP device
(STM32H743 firmware) and host tools (Tauri/Rust desktop app, TypeScript frontend,
Python test scripts).

## File structure

```
protocol/
├── protocol.toml       ← protocol definition (the source of truth, tracked)
├── common.proto        ← the protobuf frontend (tracked): the declarations
├── sys.proto             every domain is written from, the four domains
├── config.proto          themselves, and the two envelopes. Each schema is
├── test.proto            generated into the codec pair named after it, listed
├── profile.proto         below
├── request.proto
├── reply.proto
├── README.md           ← this file (tracked)
└── <generated outputs> ← written at configure time, not tracked (.gitignore):
                          proto.h, proto.rs, types.ts, proto_fields.json and
                          proto_resp.h by scripts/gen_proto.py; and
                          <schema>.pb.c with <schema>.pb.h — one pair per
                          schema above, the codec of that schema — by
                          scripts/gen_proto_pb.py
```

## TOML structure

### [meta] — project metadata

```toml
[meta]
name = "ThetaGP CDC JSON Protocol"
version = "1.0"
description = "CDC ACM JSON command protocol for ThetaGP embedded gamepad"
```

### [domains] — domain prefix registry

```toml
[domains]
sys = "System commands (ping, reset, DFU)"
config = "Configuration commands"
test = "Test commands (flash/memory diagnostics)"
profile = "Profile commands (flash-backed profile management)"
```

Each key is the domain prefix used in `domain.command_name` (e.g. `test.inject_gamepad_state`).

### [error_codes] — global error codes

```toml
[error_codes]
ERR_UNKNOWN_CMD = { code = 1, description = "Unknown command name" }
ERR_QUEUE_FULL  = { code = 5, description = "Inject queue is full" }
```

### [[enums]] — enumeration types

```toml
[[enums]]
name = "TestMode"
description = "Test injection mode"
values = [
  { name = "PASS_THRU", value = 0, description = "Pass-through mode (default)" },
  { name = "INJECT",    value = 1, description = "Inject mode (consumes queue)" },
  { name = "RECORD",    value = 2, description = "Record mode (captures history)" },
]
```

### [[types]] — shared data structures

```toml
[[types]]
name = "GamepadRawInput"
namespace = "ThetaGP::Gamepad"
description = "Gamepad raw input state"
fields = [
  { name = "buttons", type = "u32", json = "buttons", description = "32-bit button mask" },
  { name = "dpad",    type = "u8",  json = "dpad",    description = "D-pad 4-bit value (0-15)" },
]
```

**Supported types**: `u8`, `u16`, `u32`, `i32`, `bool`, `string`, `any` — the set
`scripts/gen_proto.py` maps for every target, and the set an unregistered type is
rejected against

**Record types and arrays** — a `[[types]]` entry is also what a command field
names to carry a list of objects: spelling the entry's name with a `[]` suffix
(`type = "MemoryRegion[]"`) makes the field an array whose elements have that
record's fields, and the generator then emits

- a value struct per record type on the C++ side (`struct MemoryRegionValues`,
  holding the record's declared fields and nothing else) and a writer function
  per command whose response carries such a field, which takes one value per
  element and writes the whole reply — keys, punctuation, brackets and
  conversions (see the bullet on fields a table cannot carry, below: these
  commands get no response table);
- `pub regions: Option<Vec<MemoryRegion>>` / `regions?: MemoryRegion[]` in the
  Rust and TypeScript bindings;
- the record's ordered fields in `protocol/proto_fields.json` under `types`, and
  a table per record type in `protocol/protocol-fields.md`.

A record's own fields are scalars a printf conversion writes: a record nested in
a record, or an array as a field of a record, stops generation
(`validate_record_types()`). A field naming a record type *without* the suffix
is refused there too, so an embedded single record cannot reach a target as an
untyped value by accident. `MemoryRegion` (sys.get_usage) and `ConfigKeyEntry`
(config.list_keys) are the declared records today.

**Optional field flags**:
- `required` — for command request params (default: false)
- `default` — default value for request params
- `omit_in_serialize` — if true, skip this field in JSON/C++ serialization
- `ref_type` — for array fields, references the inner type name. No field uses one,
  and `scripts/gen_proto.py` does not read the flag today
- `role` — a property of the field besides its type and its place, which the
  consumers of this file act on:
  - `accounting` — part of the per-task accounting subset the CDC test suite
    requires in every build
  - `task_counters` — written to a response only in a build that compiles the
    task counters in (`USE_TASK_COUNTERS`)

  **A role is read by a person and not derived.** It says the field is
  conditional, and it is the licence an emitter takes to leave the field out of
  one of its lists (`field_coverage_errors()` in `scripts/gen_proto.py` trusts
  it), so a role on a field that does not in fact vary with its condition reads
  as conditional to every check downstream and to the response table generated
  from it. The generator holds the name — an unregistered role stops generation
  — and nothing holds the intent, because the intent is not in the field. So
  review every role against the field it marks:

  - Does the field really vary with what the role names? `task_counters` is a
    build that compiles the counters in; `accounting` claims the field is in
    every build, and a field the firmware writes only conditionally does not
    belong in that subset.
  - Does a field the firmware writes conditionally carry a role? A missing role
    reads as unconditional, and the response table then writes the field in
    builds that have no value for it.

  Two halves of the marking are checked, and are what a review has to keep
  holding: the CDC suite (`scripts/test/test_cdc_protocol.py`) refuses to run
  when the manifest stops marking the `accounting` fields its invariants are
  stated in, or the `task_counters` fields `sys.get_task_info` reports only in a
  build with the counters; and the firmware's `sys` command handler fails to
  compile when `USE_TASK_COUNTERS` and `THETAGP_RESP_HAS_TASK_COUNTERS`
  disagree.

- **A response field a table cannot carry** — the `hand_written` column a reply
  of that kind used to be declared with is gone, and both cases it covered are
  now derived from the field's own type:

  * a field of a type no printf conversion writes (the `any` entry of that set)
    gives its command no table, and the generator emits
    `THETAGP_RESP_NO_TABLE_<DOMAIN>_<NAME>` for it. The flag is derived, not
    declared: the code that assembles that reply by hand asserts it, so the
    field's type becoming one a table carries is a compile error at that code.
    `config.get_key.value` is the one field in that position today.
  * a field declared as an array of records (`type = "MemoryRegion[]"`) also
    gives its command no table — a table holds values a conversion writes, and
    an array of objects is not one — and its reply is written by the writer
    function the generator emits for the command instead of by hand.
    `sys.get_usage.regions` and `config.list_keys.keys` are the two fields in
    that position today, and no firmware source writes their JSON text.

### [[commands]] — command definitions

```toml
[[commands]]
name = "inject_gamepad_state"
domain = "test"
description = "Inject a GamepadRawInput into the inject queue (Point A)"
request = [
  { name = "buttons", type = "u32", json = "buttons", required = true, default = 0 },
]
response = [
  { name = "queued", type = "u8", json = "queued", description = "Items remaining" },
]
error_codes = [
  { name = "ERR_QUEUE_FULL", code = 5, description = "Inject queue is full" },
]
```

Commands are addressed as `domain.name` (e.g., `"test.inject_gamepad_state"`).

### [[async_messages]] — device-initiated messages

```toml
[[async_messages]]
cmd = "async.history_full"
description = "History ring buffer overflowed"
payload = [
  { name = "type", type = "string", json = "type", description = "gamepad_state or hid_report" },
]
```

## Code generation

The Python generator `scripts/gen_proto.py` reads `protocol/protocol.toml` and
outputs:

| Output file | Language | Consumer |
|---|---|---|
| `protocol/proto.h` | C++ | Device firmware (the project's own JSON layer) |
| `protocol/proto.rs` | Rust | Tauri backend (serde) |
| `protocol/types.ts` | TypeScript | Frontend (Vue/Svelte) |
| `protocol/proto_fields.json` | JSON | Consumers that read the protocol shape: the CDC test suite |
| `protocol/proto_resp.h` | C++ | Device firmware: the response field order, keys and printf conversions, plus the writers for a response holding an array of records and for a reply that failed |
| `protocol/protocol-fields.md` | Markdown | The docs: the response field tables `docs/cdc-json-protocol.md` points at. Not tracked, like the five above: every output here is a derivative of `protocol.toml`, and the repository carries the sources (`protocol.toml` and the seven schemas beside it) and this file only |

### Usage

```bash
# Generate all targets
python3 scripts/gen_proto.py

# Generate specific targets
python3 scripts/gen_proto.py --target cpp
python3 scripts/gen_proto.py --target rust
python3 scripts/gen_proto.py --target ts
python3 scripts/gen_proto.py --target fields
python3 scripts/gen_proto.py --target resp
python3 scripts/gen_proto.py --target fields-md

# Dry-run (print to stdout)
python3 scripts/gen_proto.py --dry-run

# Specify custom protocol file
python3 scripts/gen_proto.py --protocol path/to/protocol.toml
```

### Adding a new command

1. Add a `[[commands]]` block to `protocol/protocol.toml`
2. Run `python3 scripts/gen_proto.py`
3. The generated C++ code gets a new handler stub and routing entry
4. Implement the handler body in the consumer's command module

## Design principles

1. **Single source of truth**: All protocol changes go only into `protocol.toml`. The
   repository tracks that file and this README; every output derived from it is
   generated locally and ignored
2. **No manual sync**: Generated files are never hand-edited
3. **Readable output**: Generated C++/Rust/TS code has proper comments and formatting
4. **Backward compatible**: Code the consumer has already written by hand can coexist
   with generated code — migrate incrementally
5. **Low dependency**: Every generator in this tree runs at configure time, and
   every output derived from the sources in this directory is ignored, per
   principle 1. The generator of the JSON protocol (`scripts/gen_proto.py`)
   needs only the Python 3.11+ stdlib (`tomllib`). The protobuf codecs need a
   toolchain a bare interpreter does not have: the schemas named in
   `PROTO_SCHEMAS` of `src/CMakeLists.txt` — the seven set out in "File
   structure" above, `common.proto` through `reply.proto` — are read by
   `protoc` and by the nanopb generator of the `lib/nanopb` checkout
   the build fetches, through a python-protobuf runtime. So the pair each schema
   produces — `<schema>.pb.c` and `<schema>.pb.h`, one pair per schema of that
   directory — is generated by
   the configure step in `src/CMakeLists.txt`, ignored beside the
   five outputs above, and not committed: a build environment needs `protoc`,
   the nanopb generator and python-protobuf, and the configure step runs
   `python3 scripts/gen_proto_pb.py --protos <the schemas>` over those schemas
   whenever some pair is
   missing, empty or older than some schema, so a build compiles the codecs of
   the schemas it was configured against (the script's `--protobuf-path`, or
   `$PROTOBUF_PYTHONPATH`, names a
   site-packages directory holding `google/protobuf` for an interpreter that
   cannot import one itself; `--protos` is required, since the set of schemas
   is the build's manifest and a default list in the script would be a second
   copy of it). Only the `.toml` and the seven `.proto` are tracked, so
   `lib/nanopb` following upstream `master` instead of a tag leaves nothing
   stale in the repository: the codec is generated from whichever checkout the
   build fetched, and the commit that is checked out here today is
   `2c88fc8768880b03a18b493b2834c89782a36e1d`
   (`git -C lib/nanopb rev-parse HEAD`). A build against a different checkout
   generates from that one, and the generated header's own
   `PB_PROTO_HEADER_VERSION` check is what turns a runtime and a generated
   header from different commits into a compile error rather than a silent
   mismatch.
6. **Additive evolution**: the source grows, and what has already shipped keeps
   working.
   - A field added to a response is one an older host does not know. A host
     ignores keys it does not know, and a field a device may not always send is
     declared `optional = true`, so its absence reads as declared rather than as
     missing.
   - A command or a configuration key added later is one an older device does
     not have. Such a device answers `ERR_UNKNOWN_CMD` (1),
     `ERR_NOT_SUPPORTED` (6) or `ERR_INVALID_PARAM` (2) as the case fits, and the
     commands and keys it did carry keep answering exactly as before.
   - What has shipped keeps its meaning. An existing command, field, key or code
     does not change what it means, because the devices already in the field
     cannot be changed with it.
   - `scripts/test/test_cdc_protocol.py` compares a reply against the version it
     was built with, so it refuses a key the source does not declare. That is
     what lets it catch a firmware sending more than it declared, and it is not
     a statement about what a host must tolerate from a device it did not build
     against.
   - A host reads a reply by key and never by position. The keys a reply carries,
     and the order they are written in, belong to this source: two versions that
     order the same keys differently say the same thing on the wire, and a
     consumer that parsed the keys and their values sees no change at all.

7. **The protocol does not know the codebase.** The dependency runs one way: the
   codebase depends on this file and on what is generated from it, never the other way
   round. A generator that reads the consumer's source tree, a declaration saying "this
   type already exists over there", and a sentence naming the consumer's files and lines
   are one defect at three weights — each one has the protocol describing the codebase
   instead of the interface. The weak form is not a milder version of the strong one:
   the citations rot, nothing notices, and the reader is handed a location instead of a
   rule. When a rule needs the codebase to hold, the codebase asserts it at its own
   compile time, beside the code that would break it (`#ifndef ... #error` at the write
   point); that is the direction that works, because the consumer knows what it must do
   and the protocol does not have to be told. An entry that breaks this is either
   removed or said in terms that stand on their own: quote what happens, not where it
   is written.
