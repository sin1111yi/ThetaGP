# ThetaGP Protocol Definition Format

`protocol/protocol.toml` is the **single source of truth** for all CDC JSON protocol
types, commands, constants, and error codes used between ThetaGP device
(STM32H743 firmware) and host tools (Tauri/Rust desktop app, TypeScript frontend,
Python test scripts).

## File structure

```
protocol/
├── protocol.toml       ← protocol definition (the source of truth, tracked)
├── README.md           ← this file (tracked)
└── <generated outputs> ← written by scripts/gen_proto.py, not tracked (.gitignore)
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
  build with the counters; and `src/test/testsys.cpp` fails to compile when
  `USE_TASK_COUNTERS` and `THETAGP_RESP_HAS_TASK_COUNTERS` disagree.

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
| `protocol/proto_resp.h` | C++ | Device firmware: the response field order, keys and printf conversions |
| `protocol/protocol-fields.md` | Markdown | The docs: the response field tables `docs/cdc-json-protocol.md` points at. Not tracked, like the five above: every output here is a derivative of `protocol.toml`, and the repository carries the source and this file only |

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
4. Implement the handler body in `testcmds.cpp`

## Design principles

1. **Single source of truth**: All protocol changes go only into `protocol.toml`. The
   repository tracks that file and this README; every output derived from it is
   generated locally and ignored
2. **No manual sync**: Generated files are never hand-edited
3. **Readable output**: Generated C++/Rust/TS code has proper comments and formatting
4. **Backward compatible**: Existing hand-written code (`testcmds.cpp`) can coexist
   with generated code — migrate incrementally
5. **Low dependency**: Generator uses only Python 3.11+ stdlib (`tomllib`)
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
