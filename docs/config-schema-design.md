# ThetaGP Config Schema TOML — Design Specification

**Milestone**: M4 — Config Schema TOML Definition  
**Author**: Shen Moxuan (沈墨轩), Liuchu Studio  
**Version**: 1.0 (Schema v1)  
**Date**: 2026-06-12  
**Branch**: config-schema-v1  
**Status**: Draft  

---

## Table of Contents

1. [Purpose](#1-purpose)  
2. [Design Principles](#2-design-principles)  
3. [Schema Structure](#3-schema-structure)  
4. [Key Lifecycle](#4-key-lifecycle)  
5. [Versioning Strategy](#5-versioning-strategy)  
6. [Code Generation Approach](#6-code-generation-approach)  
7. [Integration with Existing System](#7-integration-with-existing-system)  
8. [Risk Assessment](#8-risk-assessment)  
9. [Relationship to protocol.toml](#9-relationship-to-protocoltoml)  
10. [Appendix: Key Inventory](#10-appendix-key-inventory)  

---

## 1. Purpose

The ThetaGP Config Schema TOML (`protocol/config.schema.toml`) is the **single source of truth** for all runtime-configurable parameters. It formalizes what was previously documented only in a human-readable markdown file (`docs/cdc-config-schema.md`) and implemented ad-hoc in hand-written C++ structs and serialization code.

### 1.1 Goals

1. **Unification**: Ensure the device firmware (C++), backend (Rust/Tauri), frontend (TypeScript), and host-side tools all agree on the set of config keys, their types, valid ranges, defaults, and JSON encoding.

2. **Automation**: Eliminate manual maintenance of three parallel serialization layers. A single TOML source drives code generation for all targets.

3. **Validation**: The schema encodes range constraints and type information that can be used for both runtime validation on the device and pre-validation on the host side.

4. **Traceability**: Every config key has a documented `affects` field describing which subsystem it influences, making impact analysis possible at schema-read time.

### 1.2 Non-Goals

- Compile-time hardware configuration (pin assignments, bus instances, peripheral selection) stays in `BoardConfig.lua` / `BoardConfig.h`.  
- SPI flash storage format and migration logic are documented in `docs/cdc-config-schema.md` and implemented in the storage layer — the schema TOML only describes the logical keys.  
- Multi-profile support is deferred to M7+ (ProfileStore already exists; schema version in profile head is M7).

---

## 2. Design Principles

### 2.1 Type System

The schema uses a constrained type system that maps directly to C, Rust, and JSON types:

| TOML type | C type      | Rust type | TS type   | Size    | Notes                                   |
|-----------|-------------|-----------|-----------|---------|-----------------------------------------|
| `bool`    | `uint8_t`   | `bool`    | `boolean` | 1 byte  | Stored as `uint8_t` in packed struct     |
| `u8`      | `uint8_t`   | `u8`      | `number`  | 1 byte  | Unsigned 8-bit                          |
| `u16`     | `uint16_t`  | `u16`     | `number`  | 2 bytes | Unsigned 16-bit                         |
| `u32`     | `uint32_t`  | `u32`     | `number`  | 4 bytes | (Reserved for future use)               |
| `s16`     | `int16_t`   | `i16`     | `number`  | 2 bytes | Signed 16-bit (calibration offsets)     |
| `array`   | `type[N]`   | `Vec<T>`  | `T[]`     | varies  | Fixed-size array; `array_type` + `array_size` params |

No variable-length strings, nested objects, or dynamic containers in config values. The config struct is a flat, packed C aggregate that can be memcpy'd trivially.

### 2.2 Key Naming Convention

```
group.key_name
```

- All lowercase with underscore separators (snake_case).  
- Dot-separated hierarchy for logical grouping.  
- No trailing dots, no spaces, no hyphens.  
- The group name becomes the top-level JSON key in the serialized profile blob.  

### 2.3 JSON Serialization Convention

Each key has a `json` field specifying its short-form name in the JSON profile blob. These are deliberately terse (e.g., `"socd"`, `"lx_dz"`, `"bri"`) to minimize profile JSON size, which matters because the ProfileStore operates with a limited staging buffer (`PROFILE_JSON_MAX`).

The JSON group names are also configurable via `codegen.json_group_names` (e.g., `map → "map"`, `trig → "trig"`).

### 2.4 Key Entry Fields

Each key in the schema defines:

| Field          | Required | Description                                                  |
|----------------|----------|--------------------------------------------------------------|
| `name`         | Yes      | Canonical dot-path key name (e.g., `"socd_mode"`)           |
| `type`         | Yes      | TOML type name (`u8`, `u16`, `s16`, `bool`, `array`)        |
| `range`        | Yes      | `[min, max]` inclusive range for validation                  |
| `default`      | Yes      | Factory default value                                        |
| `json`         | Yes      | Short JSON key name for serialization                        |
| `c_field`      | Yes      | C struct field name (snake_case)                             |
| `description`  | Yes      | Human-readable description                                   |
| `reboot`       | No       | If `true`, requires device reset to take effect (default: false) |
| `affects`      | Yes      | Which subsystem/pipe the key influences                     |
| `array_type`   | No       | Required if `type = "array"` (e.g., `"u16"`)                |
| `array_size`   | No       | Required if `type = "array"` (e.g., `32`)                   |

---

## 3. Schema Structure

### 3.1 Top-Level Sections

```
[meta]            — Schema metadata (name, version, description, refs)
[codegen]         — Code generation settings (paths, type maps, namespace)
[[group]]         — One or more logical groups of config keys
  [group.enums]   — Enum definitions (optional, per group)
  [[group.keys]]  — Individual key definitions within the group
```

### 3.2 Groups

Seven groups are defined in Schema v1:

| Group  | Keys | Description                        |
|--------|------|------------------------------------|
| `map`  | 9    | SOCD mode, dpad mode, axis invert, button map |
| `stick`| 10   | Analog stick deadzones, sensitivity, curve, EMA |
| `trig` | 2    | Trigger deadzones                  |
| `usb`  | 1    | USB polling rate (reboot)          |
| `led`  | 5    | Brightness, mode, hue, saturation, speed |
| `sys`  | 3    | Log level, debounce samples/threshold |
| `cal`  | 4    | Calibration center offsets (signed) |
| **Total** | **34** |                                    |

### 3.3 Enum Definitions

Enums are defined inline within each group under `[group.enums]`. Each enum specifies:
- `type` — the enum type name for code generation
- `values` — array of `{ name, value, description }` entries

For example, `SOCDMode` is defined inside the `map` group and generates:
- C++: `enum class SOCDMode : uint8_t { ... }`  
- Rust: `#[repr(u8)] pub enum SOCDMode { ... }`  
- TS: `enum SOCDMode { ... }`  

### 3.4 The `[codegen]` Section

This section tells the generator:
- Where to write output files
- The type mapping for each target language
- The C++ namespace
- JSON group name overrides

```
[codegen]
c_struct_header = "src/gamepad/config/config_store.h"
c_serde_source  = "src/gamepad/config/config_store.cpp"
c_namespace     = "ThetaGP::Gamepad::Config"
rust_module     = "protocol/config_types.rs"
ts_module       = "protocol/config_types.ts"
```

---

## 4. Key Lifecycle

### 4.1 Adding a New Key

1. Add a `[[group.keys]]` entry in `config.schema.toml` with all required fields.  
2. Bump the schema version in `[meta].version` (e.g., `1 → 2`).  
3. Run `scripts/gen_config.py` to regenerate all target files.  
4. Implement runtime reads of the new field in the relevant subsystem.  
5. Add a migration path in the firmware's `migrateConfig()` function (see §6.2 of `cdc-config-schema.md`).  

### 4.2 Removing a Key

1. Delete the `[[group.keys]]` entry.  
2. Bump the schema version.  
3. Regenerate.  
4. The firmware migration function must zero-fill the removed field's bytes on load from an older schema version.  

### 4.3 Modifying a Key

- **Range change**: Only validation logic changes; no struct layout impact. Bump schema version.  
- **Type change** (e.g., `u8 → u16`): Bump version, regenerate, add migration in firmware.  
- **Default change**: Bump version; the migration function writes the new default only if the field's stored value was the old default (heuristic).  
- **JSON key change**: Update the `json` field for the key. No schema version bump needed for the config wire protocol, but the ProfileStore blob format changes (M7 concern).  

### 4.4 Deprecation

Deprecated keys are not removed from the schema immediately. Instead:
1. Add `deprecated = true` to the key entry.  
2. The code generator emits a compile-time deprecation warning in C++ (via `[[deprecated]]`).  
3. After two firmware versions, the key can be removed and the schema version bumped.  

---

## 5. Versioning Strategy

### 5.1 Schema Version Number

A single `uint16_t` version number (`meta.version`) tracks the schema. Bumped on any structural change (field added, removed, resized, reordered, or type changed).

| Version | Description              |
|---------|--------------------------|
| 0       | Uninitialized / no schema |
| 1       | Initial schema (M4)       |
| 2+      | Future                    |

### 5.2 Device Advertising

The device reports its schema version via the `config.get_device_info` command. When this command is implemented, the response will include:

```json
{
  "status": "ok",
  "board": "BoringTechH743",
  "fw_version": "0.2.0",
  "serial": "00000001",
  "config_schema_version": 1
}
```

The existing `config.get_device_info` command in `protocol.toml` returns `board`, `fw_version`, and `serial`. Adding `config_schema_version` (or including it in a future extended info command) is deferred to M7.

### 5.3 Host Negotiation

The host (Tauri backend / frontend) can:
1. Read `config_schema_version` from device info at connection time.  
2. Compare against the host-side schema version (hardcoded in the compiled Tauri binary).  
3. If the host schema is newer, the host must still only send keys the device understands (the schema TOML is the contract).  
4. If the device schema is newer, the host should log a warning about missing client-side knowledge.  

### 5.4 Flash Migration

The flash storage format (defined in `docs/cdc-config-schema.md` §5) embeds the schema version in its header. On `config.load`, the firmware:
1. Reads the stored version from the flash header.  
2. If stored version < current version, calls `migrateConfig()` to transform in-RAM struct in-place.  
3. Marks config dirty so the next `config.save` writes the current version.  

---

## 6. Code Generation Approach

### 6.1 Generator Script: `scripts/gen_config.py`

A new Python script (analogous to `scripts/gen_proto.py`) that reads `protocol/config.schema.toml` and generates:

**C++ ConfigStore struct** (`src/gamepad/config/config_store.h`):
- Packed struct with all fields matching `c_field` names
- Inline member initializers for defaults
- `uint16_t btn_map[32]` array
- `COMMON_ZERO_INIT` extern declaration for `s_config`
- `#pragma pack(push, 1)` / `#pragma pack(pop)` wrapping

**C++ JSON serde** (`src/gamepad/config/config_store.cpp`):
- `parseProfile()` function — reads JSON into struct fields using ArduinoJson `|` operator
- `serializeProfile()` function — writes struct fields into JSON groups
- The serialization mirrors the existing hand-written code in `config_store.cpp` and `configmgr.cpp`

**C++ KeyEntry table** (inline in a header or generated separately):
- `constexpr` array of `KeyEntry` structs for runtime lookup
- Each entry: name string, `offsetof`, type ID, min/max range, reboot flag
- Linear search is acceptable for ~34 keys at human-rate CDC command speed

**Rust types** (`protocol/config_types.rs`):
- `#[derive(Serialize, Deserialize)]` structs matching the JSON profile format
- Full serde rename annotations for the short JSON keys
- Enum types with `#[repr(u8)]` and serde rename

**TypeScript types** (`protocol/config_types.ts`):
- Interface definitions for each group
- Enum constants (or string enums)
- Validation helpers (range checks)

### 6.2 Special Cases

**btn_map[32] array** (`map.btn_map`):
- C++: `uint16_t btn_map[32]` with default initialization to `0xFFFF` (unassigned)
- JSON: serialized as `"btn_map": [65535, 65535, ...]` (32-element array)
- Rust: `btn_map: Vec<u16>` with serde default
- TS: `btn_map: number[]` with length validation

**Calibration s16 fields** (`cal.*`):
- C++: `int16_t` type (signed)
- JSON: serialized as integer (can be negative)
- Range: [-1024, 1024] — the schema range field uses `[min, max]` syntax which handles negatives for `s16` type

**Bool fields**:
- C++: Stored as `uint8_t` (0/1), not `bool`, to avoid struct packing issues
- JSON: Serialized as `0` or `1` (ArduinoJson `|` operator handles `uint8_t` → JSON int)
- Rust/TS: Treated as boolean

**Reboot-required keys**:
- The C++ KeyEntry table includes the `reboot` flag
- `config.set_key` returns success immediately but marks `reboot_pending` flag
- The USB init code checks reboot-required keys at boot time

### 6.3 Generation Flow

```
config.schema.toml
       │
       ▼
  gen_config.py
       │
       ├─── config_store.h         (C++ struct + inline defaults)
       ├─── config_store.cpp       (JSON serialize/deserialize)
       ├─── config_types.rs         (Rust types + serde)
       ├─── config_types.ts         (TS interfaces + validation)
       └─── (optional) key_table.h (C++ KeyEntry lookup array)
```

The generator is invoked as part of the build process, similar to `gen_proto.py`:

```bash
python3 scripts/gen_config.py                          # all targets
python3 scripts/gen_config.py --target cpp             # C++ only
python3 scripts/gen_config.py --target rust            # Rust only
python3 scripts/gen_config.py --target ts              # TS only
python3 scripts/gen_config.py --dry-run               # print to stdout
```

### 6.4 Key Mapping to Code

Each `[[group.keys]]` entry maps to three names:

| Schema field | Purpose                          | Example                |
|-------------|----------------------------------|------------------------|
| `name`      | Canonical dot-path key           | `"socd_mode"`         |
| `c_field`   | C++ struct field name            | `"socd_mode"`         |
| `json`      | JSON serialization key           | `"socd"`              |
| `json (group)` | JSON group container name     | `"map"`               |

The full JSON path for a config key is: `<json_group>.<json>` (e.g., `"map.socd"`).

---

## 7. Integration with Existing System

### 7.1 ProfileStore Interaction

The existing `ProfileStore` (in `drivers/device/flash/`) stores config as a JSON blob per profile. The flow is:

1. **Save** (`ConfigManager::saveProfile()`):
   - Builds a JSON document from the live `s_config` struct using group→key→value structure
   - Serializes to `s_staging` buffer
   - Calls `ProfileStore::modifyProfile()` which writes to SPI flash
   - The schema version will be embedded in the profile head (M7 addition)

2. **Load** (`ConfigManager::loadProfile()`):
   - Calls `ProfileStore::loadActive()` which fills `s_staging` with JSON
   - Calls `parseProfile()` which deserializes JSON into `s_config`
   - Schema version can be checked to trigger migration

### 7.2 ConfigManager Bridge

`ConfigManager` (in `src/gamepad/config/configmgr.h/.cpp`) bridges between `ProfileStore` and `ConfigStore`:

- `config()` returns a reference to the live `s_config` struct
- `saveProfile()` serializes `s_config` to JSON and writes through `ProfileStore`
- `selectProfile()` loads a different profile, which deserializes into `s_config`

The generated `serializeProfile()` and `parseProfile()` functions replace the hand-written serialization in `configmgr.cpp`.

### 7.3 CDC set_key/get_key

The `config.set_key` and `config.get_key` commands (defined in `protocol.toml`) use key names like `"map.socd_mode"` over the wire. The firmware:

1. Receives a JSON request with `"key": "map.socd_mode"` and `"value": 2`
2. Parses the key into group + key name
3. Looks up the `KeyEntry` by dot-path name
4. Validates the value against the entry's range
5. Writes to `s_config` at the stored `offsetof` offset
6. Marks dirty

The KeyEntry table is generated from the schema, ensuring that the set of keys the device knows matches the schema exactly.

### 7.4 Future: Schema Version in Profile Head (M7)

Once schema versioning is integrated into the ProfileStore (M7), each profile's JSON blob will include a `"_schema_version"` field at the top level:

```json
{
  "_schema_version": 1,
  "map": { "socd": 0, ... },
  "stick": { "lx_dz": 512, ... }
}
```

This allows the device to detect schema mismatches per-profile and apply migrations on load.

---

## 8. Risk Assessment

### 8.1 Schema Drift

**Risk**: The generated C++ struct diverges from the hand-written code that already exists.

**Mitigation**: The M4 deliverable includes both the schema TOML and the design document. The code generator (`gen_config.py`) is created in a follow-up task. Until the generator is ready, the hand-written `config_store.h` remains the source of truth. When the generator is activated, a CI check will compare the generated output against the checked-in hand-written file and fail if they differ.

For the transition period, maintain a `schema_drift_check.py` script that:
1. Parses the schema TOML
2. Parses the hand-written C++ struct header
3. Reports any fields that exist in one but not the other

### 8.2 Backward Compatibility

**Risk**: A firmware update with a higher schema version cannot read profiles written by an older firmware.

**Mitigation**: The migration function (`migrateConfig()`) is always defined in the current firmware. It handles all transitions from version N to version N+1. The migration is idempotent.

**Risk**: An old firmware cannot read a profile written by a newer firmware.

**Mitigation**: The device will refuse to load a profile with `_schema_version > CURRENT_SCHEMA_VERSION` and fall back to factory defaults. The host side should warn the user.

### 8.3 Enum vs u8 Trade-offs

Enum values like `SOCDMode`, `DpadMode`, `PollRate`, `LEDMode`, and `LogLevel` are stored as `u8` in the C struct, not C++ `enum class`. This is a deliberate choice:

- **Pro (u8)**: Trivially serializable; no casting needed for `memcpy` to flash; ArduinoJson handles u8 naturally
- **Con (u8)**: No type safety in C++; invalid values can be written via CDC without compile-time protection

The generated KeyEntry table provides runtime validation (range check), which mitigates the type safety loss. For Rust and TypeScript, proper enums are generated with full type safety.

### 8.4 Array Field Complexity

The `btn_map[32]` array is the most complex field in the schema. Risks:
- Array serialization requires special handling in the generator (loop over elements)
- Default initialization (all `0xFFFF`) is non-trivial
- The array adds 64 bytes to the struct size (32 × uint16_t)

The schema handles this via `type = "array"` with `array_type = "u16"` and `array_size = 32`.

### 8.5 Size Budget

Current struct size (from hand-written code): ~120 bytes (including `btn_map[32]` at 64 bytes). This is well within the 4 KB SPI flash sector budget and the 512-byte ArduinoJson document budget for individual operations.

---

## 9. Relationship to protocol.toml

The two TOML files serve complementary but distinct purposes:

### 9.1 Separation of Concerns

| Aspect              | `protocol.toml`                       | `config.schema.toml`                  |
|---------------------|---------------------------------------|---------------------------------------|
| Domain              | CDC JSON command protocol             | Runtime configuration data schema     |
| Defines             | Commands, types, enums, error codes   | Config keys, ranges, defaults, enums  |
| Consumer            | `gen_proto.py` → proto.h/rs/ts       | `gen_config.py` → config files        |
| Commands            | `set_key`, `get_key`, `save`, `load`  | N/A (these are the transport)         |
| Data types          | Fixed protocol types (structs)        | Config value types (scalar, array)    |
| Scope               | Wire format                           | Persistent storage format             |
| Change frequency    | Low (stable once defined)             | Higher (new features add keys)        |

### 9.2 How They Interact

The `config` domain in `protocol.toml` defines commands like `config.set_key` that accept a `"key"` and a `"value"`. The keys and values are defined in `config.schema.toml`. The device firmware uses both:

1. `protocol/protocol.toml` → `proto.h` → defines the command dispatcher
2. `protocol/config.schema.toml` → `config_store.h` + `key_table.h` → defines the data
3. The command handler for `config.set_key` reads the JSON, validates the key against the generated KeyEntry table, and writes the value to the generated ConfigStore struct

### 9.3 Cross-References

- `protocol.toml` references `config.schema.toml` via `protocol_ref = "protocol.toml@config domain"` in the meta section
- `config.schema.toml` references `protocol.toml` in its `[meta].doc_ref` field

---

## 10. Appendix: Key Inventory

Complete list of all 34 config keys in Schema v1, grouped with full metadata:

### map (9 keys)

| Dot-path name          | C field       | JSON key   | Type   | Range      | Default | Reboot |
|------------------------|---------------|------------|--------|------------|---------|--------|
| `map.socd_mode`        | `socd_mode`   | `socd`     | u8     | 0-4        | 0       | No     |
| `map.four_way_mode`    | `four_way_mode`| `four_way`  | bool   | 0-1        | 0       | No     |
| `map.dpad_mode`        | `dpad_mode`   | `dpad`     | u8     | 0-2        | 0       | No     |
| `map.invert_x`         | `inv_x`       | `inv_x`    | bool   | 0-1        | 0       | No     |
| `map.invert_y`         | `inv_y`       | `inv_y`    | bool   | 0-1        | 0       | No     |
| `map.invert_rx`        | `inv_rx`      | `inv_rx`   | bool   | 0-1        | 0       | No     |
| `map.invert_ry`        | `inv_ry`      | `inv_ry`   | bool   | 0-1        | 0       | No     |
| `map.swap_sticks`      | `swap_sticks` | `swap`     | bool   | 0-1        | 0       | No     |
| `map.btn_map`          | `btn_map`     | `btn_map`  | array  | N/A        | 0xFFFF  | No     |

### stick (10 keys)

| Dot-path name               | C field     | JSON key  | Type | Range      | Default |
|-----------------------------|-------------|-----------|------|------------|---------|
| `stick.lx_deadzone`         | `lx_dz`     | `lx_dz`   | u16  | 0-32767    | 512     |
| `stick.ly_deadzone`         | `ly_dz`     | `ly_dz`   | u16  | 0-32767    | 512     |
| `stick.rx_deadzone`         | `rx_dz`     | `rx_dz`   | u16  | 0-32767    | 512     |
| `stick.ry_deadzone`         | `ry_dz`     | `ry_dz`   | u16  | 0-32767    | 512     |
| `stick.lx_sensitivity`      | `lx_sens`   | `lx_sens` | u8   | 0-255      | 128     |
| `stick.ly_sensitivity`      | `ly_sens`   | `ly_sens` | u8   | 0-255      | 128     |
| `stick.rx_sensitivity`      | `rx_sens`   | `rx_sens` | u8   | 0-255      | 128     |
| `stick.ry_sensitivity`      | `ry_sens`   | `ry_sens` | u8   | 0-255      | 128     |
| `stick.response_curve`      | `curve`     | `curve`   | u8   | 0-2        | 0       |
| `stick.ema_alpha`           | `ema`       | `ema`     | u8   | 0-255      | 0       |

### trig (2 keys)

| Dot-path name           | C field | JSON key | Type | Range  | Default |
|-------------------------|---------|----------|------|--------|---------|
| `trig.lt_deadzone`      | `lt_dz` | `lt_dz`  | u8   | 0-127  | 8       |
| `trig.rt_deadzone`      | `rt_dz` | `rt_dz`  | u8   | 0-127  | 8       |

### usb (1 key)

| Dot-path name        | C field     | JSON key | Type | Range | Default | Reboot |
|----------------------|-------------|----------|------|-------|---------|--------|
| `usb.poll_rate`      | `poll_rate` | `poll`   | u8   | 0-3   | 2       | Yes    |

### led (5 keys)

| Dot-path name         | C field           | JSON key | Type | Range  | Default |
|-----------------------|-------------------|----------|------|--------|---------|
| `led.brightness`      | `led_brightness`  | `bri`    | u8   | 0-255  | 128     |
| `led.mode`            | `led_mode`        | `mode`   | u8   | 0-4    | 0       |
| `led.hue`             | `led_hue`         | `hue`    | u16  | 0-360  | 180     |
| `led.saturation`      | `led_saturation`  | `sat`    | u8   | 0-255  | 255     |
| `led.speed`           | `led_speed`       | `spd`    | u8   | 0-255  | 128     |

### sys (3 keys)

| Dot-path name               | C field             | JSON key    | Type | Range | Default |
|-----------------------------|---------------------|-------------|------|-------|---------|
| `sys.log_level`             | `log_level`         | `log`       | u8   | 0-3   | 1       |
| `sys.debounce_samples`      | `debounce_samples`  | `deb_samp`  | u8   | 4-64  | 16      |
| `sys.debounce_threshold`    | `debounce_threshold`| `deb_thr`   | u8   | 1-64  | 12      |

### cal (4 keys)

| Dot-path name         | C field   | JSON key | Type | Range          | Default |
|-----------------------|-----------|----------|------|----------------|---------|
| `cal.lx_center`       | `cal_lx`  | `lx_c`   | s16  | -1024 to 1024  | 0       |
| `cal.ly_center`       | `cal_ly`  | `ly_c`   | s16  | -1024 to 1024  | 0       |
| `cal.rx_center`       | `cal_rx`  | `rx_c`   | s16  | -1024 to 1024  | 0       |
| `cal.ry_center`       | `cal_ry`  | `ry_c`   | s16  | -1024 to 1024  | 0       |

### Enum Summary

| Enum Name     | Group | Values                                    |
|---------------|-------|-------------------------------------------|
| `SOCDMode`    | map   | UpPriority(0), Neutral(1), SecondInputPriority(2), FirstInputPriority(3), Bypass(4) |
| `DpadMode`    | map   | Dpad(0), LeftAnalog(1), RightAnalog(2)   |
| `PollRate`    | usb   | 125Hz(0), 250Hz(1), 500Hz(2), 1000Hz(3)  |
| `LEDMode`     | led   | Off(0), Static(1), Pulse(2), Rainbow(3), Battery(4) |
| `LogLevel`    | sys   | Error(0), Warn(1), Info(2), Debug(3)      |

---

*End of document.*
