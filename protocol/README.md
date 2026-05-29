# ThetaGP Protocol Definition Format

`protocol/protocol.toml` is the **single source of truth** for all CDC JSON protocol
types, commands, constants, and error codes used between ThetaGP device
(STM32H743 firmware) and host tools (Tauri/Rust desktop app, TypeScript frontend,
Python test scripts).

## File structure

```
protocol/
├── protocol.toml       ← protocol definition
└── README.md           ← this file
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
test = "Test and injection commands"
config = "Configuration commands (reserved)"
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

**Supported types**: `u8`, `u16`, `u32`, `bool`, `string`, `any`, `array`

**Optional field flags**:
- `required` — for command request params (default: false)
- `default` — default value for request params
- `omit_in_serialize` — if true, skip this field in JSON/C++ serialization
- `ref_type` — for array fields, references the inner type name

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
| `protocol/proto.h` | C++ | Device firmware (ArduinoJson) |
| `protocol/proto.rs` | Rust | Tauri backend (serde) |
| `protocol/types.ts` | TypeScript | Frontend (Vue/Svelte) |

### Usage

```bash
# Generate all targets
python3 scripts/gen_proto.py

# Generate specific targets
python3 scripts/gen_proto.py --target cpp
python3 scripts/gen_proto.py --target rust
python3 scripts/gen_proto.py --target ts

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

1. **Single source of truth**: All protocol changes go only into `protocol.toml`
2. **No manual sync**: Generated files are never hand-edited
3. **Readable output**: Generated C++/Rust/TS code has proper comments and formatting
4. **Backward compatible**: Existing hand-written code (`testcmds.cpp`) can coexist
   with generated code — migrate incrementally
5. **Low dependency**: Generator uses only Python 3.11+ stdlib (`tomllib`)
