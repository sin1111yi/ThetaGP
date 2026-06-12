# ThetaGP Configuration System — Comprehensive Migration Plan

**Author**: Shen Moxuan (Steven / Moxuan), Architect @ Liuchu Studio
**Date**: 2026-06-12
**Version**: 1.0
**Status**: Draft

---

## Table of Contents

1. [Current Architecture Panorama](#1-current-architecture-panorama)
2. [Completed Milestones](#2-completed-milestones)
3. [Remaining Milestones](#3-remaining-milestones)
4. [Detailed Step Descriptions](#4-detailed-step-descriptions)
5. [Branch Strategy](#5-branch-strategy)
6. [Acceptance Criteria](#6-acceptance-criteria)
7. [Suggested Execution Order](#7-suggested-execution-order)
8. [Risk Register](#8-risk-register)

---

## 1. Current Architecture Panorama

The ThetaGP configuration system spans THREE distinct layers with different representation formats:

```
Layer                    Format          Toolchain           Status
─────────────────────────────────────────────────────────────────────
① Board-level hardware  Lua → C macros  generate_config.lua  ✅ Working
   config (pinmux, bus                   (Lua)                (Lua source)
   instances, flash chip)

② Runtime config        C struct         Hand-written        ✅ Working
   (gamepad behavior,    (ConfigStore)   (config_store.h)     (hardcoded C)
   SOCD, deadzones,      JSON over CDC                       JSON over CDC
   LED, calibration)                                          via ProfileStore

③ Protocol definition   TOML            gen_proto.py (Python) ✅ Working
   (CDC commands, types,                 → proto.h/.rs/.ts   (config domain
   error codes)                                               = reserved)

④ PeripheralsManager    Lua→C macros   BoardConfig generates  ✅ Already redesigned
   (bus descriptor tables,                SPI_DESC_DATA etc.     DESC_TABLE pattern
   runtime bus mgmt)                      consumed by            + central Bus registry
```

### Data Flow (Current)

```
┌─ Build Time ─────────────────────────────────────────┐
│ BoardConfig.lua → Lua generators → BoardConfig.h      │
│  (pinmux, bus instance macros, flash chip select)     │
│                                                        │
│ protocol/protocol.toml → gen_proto.py → proto.h/.rs  │
│  (CDC command dispatch, serialization helpers)         │
└────────────────────────────────────────────────────────┘

┌─ Runtime ────────────────────────────────────────────┐
│ ProfileStore (Flash) ──loadActive()──→ s_staging      │
│   │  (BootMeta Ring + User Ring)                     │
│   ↓                                                   │
│ parseProfile() → s_config (ConfigStore in RAM)        │
│   │                                                   │
│   ↓ (if dirty)                                        │
│ ConfigManager::saveProfile() → serialize → flash      │
│                                                        │
│ CDC commands (profile.*) ←→ ProfileStore               │
│ CDC commands (config.*) = ERR_NOT_SUPPORTED           │
└────────────────────────────────────────────────────────┘
```

### Pain Points

| Issue | Location | Impact |
|-------|----------|--------|
| BoardConfig is Lua, not TOML | `configs/<target>/BoardConfig.lua` | Cannot reuse protocol.toml toolchain; Lua required for builds |
| ConfigStore fields hand-written | `config_store.h` (hardcoded C struct) | Schema drift between device firmware and host tools |
| No config TOML schema | missing | Keys/types/ranges not machine-readable |
| Config domain = `reserved` | `protocol.toml` lines 403-477 | Cannot set/get keys at runtime |
| Profile has no head | User Ring stores pure JSON | No metadata (version, checksum, type) on individual profiles |
| No device self-description | `config.get_device_info` returns 3 fields only | Host cannot discover capabilities at runtime |
| PeripheralsManager ad-hoc | `peripheralsmgr.cpp` uses static arrays | Already redesigned: DESC_TABLE pattern + central Bus registry ✅ |
| No profile test suite | missing | Cannot verify compaction, multi-profile, edge cases |

---

## 2. Completed Milestones ✅

These are the 4 commits already on `main` (HEAD at `e35f65b`).

### ✅ M0: CDC Main-loop Refactor
**Commit**: `3a5a974 refactor(cdc): move command processing from ISR to main-loop task`

Moved CDC command processing from ISR context to a main-loop task via `TaskManager`. This is a prerequisite for any non-trivial command (flash I/O cannot happen in ISR).

### ✅ M1: ProfileStore + ConfigManager + CDC Profile Commands
**Commit**: `9dc0897 feat(profile): add ProfileStore, ConfigManager and CDC profile commands`

Core Profile Flash v3 implementation:
- `profile_flash.h/.cpp` — ProfileStore with BootMeta Ring + Address Ring + User Ring on W25Q64
- `configmgr.h/.cpp` — ConfigManager as runtime bridge
- `config_store.h/.cpp` — ConfigStore struct + parseProfile()
- `profile_cmd_handler.h/.cpp` — 9 CDC profile commands (start, end, get, list, create, delete, select, status, raw)

### ✅ M2: Deprecated WearLevel Removal
**Commit**: `7b49215 refactor(flash): remove deprecated WearLevel and flash_wl_handler`

Removed the old key-value WearLevel storage layer and its handler, fully replacing with ProfileStore.

### ✅ M3: ConfigStore Cleanup
**Commit**: `e35f65b style: rename DMA_BSS to COMMON_ZERO_INIT, drop pack(1) from ConfigStore`

Renamed `DMA_BSS` to `COMMON_ZERO_INIT` per coding style guidelines, removed `pack(1)` from ConfigStore (no longer needed since it's not directly memcpy'd to flash).

---

## 3. Remaining Milestones

### Prioritized Roadmap

```
Phase 0 [Foundation] — Schema & Communication Layer
  ├── M4: Config Schema TOML Definition
  ├── M5: TOML-Based Config Code Generation
  └── M6: Config Domain Implementation (set_key/get_key/save/load/factory_reset)

Phase 1 [Profile Enhancement]
  ├── M7: Profile Head + Payload
  ├── M8: Device Self-Description Generation
  └── M9: Profile System Test Suite

Phase 2 [Architecture Redesign] ✅
  └── M10: PeripheralsManager Redesign (Bus/DevMem/Generator) — already done on `main`

→ Only remaining: M11 BoardConfig TOML migration
```

---

## 4. Detailed Step Descriptions

### M4: Config Schema TOML Definition

| Field | Value |
|-------|-------|
| **What** | Create a `config.schema.toml` that defines all runtime-configurable keys with types, ranges, defaults, descriptions, and grouping |
| **Why** | Single source of truth for runtime config schema — consumed by codegen for both device C struct and host-side validation |
| **Pre-req** | None (can start immediately) |
| **Files** | `protocol/config.schema.toml` (new), reference `protocol/protocol.toml` |
| **Effort** | M |
| **Risk** | Low — pure data definition |

**Content outline**:

```toml
[meta]
name = "ThetaGP Runtime Configuration Schema"
version = "1"

[groups]
map = "Button/axis mapping and SOCD"
stick = "Analog stick parameters"
trigger = "Trigger parameters"
usb = "USB/HID parameters (reboot-required)"
led = "RGB LED parameters"
sys = "System/debug parameters"
cal = "Calibration offsets"

[[keys]]
group = "map"
name = "socd_mode"
type = "u8"
range = "0..4"
default = 0
description = "SOCD cleaning mode (0=UpPriority, 1=Neutral, 2=LastInput, 3=FirstInput, 4=Bypass)"

[[keys]]
group = "map"
name = "four_way_mode"
type = "bool"
default = false
description = "Four-way mode filter"

# ... (all keys from docs/cdc-config-schema.md §3, ~40 keys)
```

**Parallel work**: Update `docs/cdc-config-schema.md` to reference this TOML as the SSOT (currently the doc IS the schema).

---

### M5: TOML-Based Config Code Generation

| Field | Value |
|-------|-------|
| **What** | Extend `gen_proto.py` (or create `gen_config.py`) to read `config.schema.toml` and produce: (a) C++ `ConfigStore` struct, (b) JSON serialization/deserialization functions, (c) Rust/TS config types |
| **Why** | Eliminate hand-maintained `config_store.h/.cpp`; ensure device firmware struct is always in sync with host tools |
| **Pre-req** | M4 (config schema TOML must exist) |
| **Files** | `scripts/gen_config.py` (new), `src/gamepad/config/config_store.h` (regenerated), `src/gamepad/config/config_store.cpp` (regenerated), `protocol/config.rs` (new), `protocol/config.ts` (new) |
| **Effort** | L |
| **Risk** | Medium — codegen for C struct + ArduinoJson serialization needs careful handling of `btn_map[32]` (array fields) |

**Key design decisions**:
- Button map array (`btn_map[32]`) is a special case — need TOML schema to express `type = "u16[32]"`.
- Struct packing: generated code uses `#pragma pack(push, 1)` only if flash direct-memcpy is used; otherwise no packing.
- Need a `static_assert(sizeof(ConfigStore) == N)` guard in generated output.

**Architecture diagram after M5**:

```
protocol/config.schema.toml  ──→  gen_config.py  ──→  config_store.h (C struct)
       (SSOT)                                       ──→  config_store.cpp (serde)
                                                    ──→  config.rs (Rust types)
                                                    ──→  config.ts (TS types)
```

---

### M6: Config Domain Implementation

| Field | Value |
|-------|-------|
| **What** | Implement the 5 `config.*` CDC commands: `set_key`, `get_key`, `save`, `load`, `factory_reset`. Currently all return `ERR_NOT_SUPPORTED`. |
| **Why** | Required for host-side runtime config modification without profile-level transfers. `set_key`/`get_key` enable individual key R/W; `save`/`load` provide explicit persistence control. |
| **Pre-req** | M4 (schema defines valid keys); M5 (generated serde); optionally M7 (profile head for version tracking) |
| **Files** | `src/test/config_cmd_handler.h/.cpp` (new), `src/test/init.cpp` (register handlers), `protocol/protocol.toml` (remove `reserved` remark) |
| **Effort** | M |
| **Risk** | Medium — `set_key` needs runtime JSON path parsing; `factory_reset` needs to handle profile0 read-only constraint |

**Behavior spec**:

```python
set_key(key, value):
  - If key not in schema → ERR_INVALID_PARAM
  - If value type mismatches → ERR_INVALID_PARAM
  - Update s_config field
  - Set dirty flag (if tracking implemented)
  - Return { key: key }

get_key(key):
  - If key not in schema → ERR_INVALID_PARAM
  - Return { key: key, value: <current_value> }

save:
  - Call ConfigManager::saveProfile()
  - Returns {} (empty response)

load:
  - Call ProfileStore::loadActive() → parseProfile() → s_config
  - Returns {}

factory_reset:
  - Reset s_config to factory defaults
  - Do NOT auto-save (user must call save)
  - Returns {}
```

**Key mapping**: The JSON key `"map.socd_mode"` must map to `s_config.socd_mode`. The M5 codegen should produce a lookup table or switch-case for this.

---

### M7: Profile Head + Payload

| Field | Value |
|-------|-------|
| **What** | Add a binary header (head) to each User Ring profile entry. Currently User Ring stores pure JSON. The head carries metadata: magic, profile version, CRC32 of JSON payload, creation timestamp/seq, flags (read-only, deleted, compressed). |
| **Why** | Pure JSON cannot carry metadata; CRC is per-entry not per-profile; version tracking for schema migration; flags for future features (compression, encryption). |
| **Pre-req** | M1 (ProfileStore exists) |
| **Files** | `src/drivers/device/flash/profile_flash.h/.cpp` (add `ProfileHead` struct, modify User Ring read/write) |
| **Effort** | M |
| **Risk** | Low — backwards compatible if User Ring is empty or treated as v0; but WILL break existing flashed profiles. Need a migration path or accept that existing devices need reflash. |

**Proposed `ProfileHead`** (16 bytes, pack(1)):

```cpp
#pragma pack(push, 1)
struct ProfileHead {
  uint32_t magic;         // "THGP" (ThetaGP Head)
  uint16_t version;       // schema version (matches config.schema.toml version)
  uint16_t flags;         // bit0=deleted, bit1=readonly, bit2=compressed...
  uint32_t crc32;         // CRC32 of JSON body
  uint16_t bodyLen;       // actual JSON body length (≤ PROFILE_JSON_MAX)
};
#pragma pack(pop)
```

**New User Ring layout**:
```
[ProfileHead (16B)] [JSON body (≤ 2048B)] [padding (to 4096)]
```

This changes the per-slot usable JSON space from 2048 to 2032, which is acceptable.

**Backwards compatibility**:
- Existing User Ring without head: detect by reading first 4 bytes — if not "THGP", assume old format (pure JSON starting at offset 0).
- Set a define `PROFILE_HEAD_V1_MAGIC = 0x50474854` and fallback gracefully.

---

### M8: Device Self-Description Generation

| Field | Value |
|-------|-------|
| **What** | Extend `config.get_device_info` to return a complete device descriptor generated at build time from BoardConfig + config schema. Include: board, fw version, build date, available peripherals, config schema version, supported features, hardware capabilities. |
| **Why** | Host needs to know what the device supports without hardcoding. Enables dynamic UI generation, schema negotiation, feature detection. |
| **Pre-req** | M4 (schema version); M5 (codegen can embed build-time info) |
| **Files** | `src/test/testsys.h/.cpp` (extend `get_device_info` handler), `protocol/protocol.toml` (extend response fields), `scripts/gen_config.py` (generate device info lookup) |
| **Effort** | M |
| **Risk** | Low — additive change, does not break existing commands |

**Proposed enhanced response**:

```json
{
  "board": "BoringTechH743",
  "fw_version": "0.5.0",
  "build_date": "2026-06-12",
  "build_time": "14:30:00",
  "serial": "THGP-00001",
  "config_schema_version": 1,
  "flash_size_kb": 8192,
  "peripherals": {
    "spi": 1,
    "uart": 1,
    "keypad_matrix": [2, 2],
    "usb_speed": "full_speed"
  },
  "features": ["profile", "config_cdc", "socd", "injection_test"],
  "profiles_max": 16,
  "profile_json_max": 2048
}
```

The build-time generation can be done by extending `gen_config.py` to emit a `build_device_info.h` that contains `#define` constants derived from BoardConfig + config.schema.toml.

---

### M9: Profile System Test Suite

| Field | Value |
|-------|-------|
| **What** | Implement comprehensive end-to-end tests for the Profile system: write → load → modify → save → switch → delete → compaction. Both device-side (C++ test API) and host-side (Python CDC test scripts). |
| **Why** | Profile Flash v3 (M1-M3) was merged without a formal test suite. Compaction, multi-profile, and edge cases (full ring, power loss) are untested. |
| **Pre-req** | M1 (ProfileStore exists and works) |
| **Files** | `src/test/profile_cmd_handler.h/.cpp` (add test utilities), `scripts/test/test_profile.py` (new, extends test_cdc.py) |
| **Effort** | L |
| **Risk** | Low-medium — testing on real hardware requires flashing; can do unit tests on PC if flash driver is mocked |

**Test cases**:

| # | Test | Scenario |
|---|------|----------|
| 1 | Factory default after erase | Flash with no valid BootMeta → device boots with config defaults |
| 2 | Profile0 write-once | writeFactoryProfile succeeds once; second call fails or no-ops |
| 3 | Create user profile | createProfile returns newId=1, profileCount increments |
| 4 | Load and verify | loadActive returns JSON matching what was written |
| 5 | Modify profile | modifyProfile updates User Ring, old entry becomes stale |
| 6 | Switch profile | selectProfile changes activeId, new profile loads to RAM |
| 7 | Delete profile | deleteProfile marks entry, profileCount decrements |
| 8 | List profiles | list returns correct IDs and active status |
| 9 | BootMeta ring wraparound | 64+ profile changes → BootMeta ring wraps correctly |
| 10 | Address ring wraparound | 128+ operations → Address ring wraps correctly |
| 11 | User Ring compaction | Fill sectors → compaction moves valid profiles to head |
| 12 | Power-loss mid-compaction | Simulate incomplete compaction → boot recovers to Profile0 |
| 13 | CRC validation | Corrupted BootMeta → fallback to Profile0 |
| 14 | Multiple profile support | 16 profiles written, listed, switched between |

---

### ✅ M10: PeripheralsManager Redesign (Bus/DevMem/Generator)

**Status**: Complete

The PeripheralsManager redesign was already implemented alongside the Profile Flash v3 work. The archived plan (`~/.hermes/plans/archive/theta-gp-config-arch-redesign.md`) was used as the design document.

**Implemented**:
| Design | Location | |
|--------|----------|--|
| `Bus` abstract base class with `setBuffers()` | `src/drivers/peripherals/bus/bus.h` | ✅ |
| `SpiDesc` / `UartDesc` structs | `bus_spi.h`, `bus_uart.h` | ✅ |
| SpiBus/UartBus constructors accepting `const SpiDesc &` | `bus_spi.h:78` | ✅ |
| `BUS_SPI_N` abstract index constants | `bus_spi.h:48-53` | ✅ |
| PeripheralsManager singleton with flat Bus arrays | `peripheralsmgr.cpp:80-100` | ✅ |
| `SPI_DESC_DATA` / `UART_DESC_DATA` composite macros | `BoardConfig.h:65-75` | ✅ |
| DevMem shared memory pool | `devmem.h/.cpp` | ✅ |
| FlashBase receives Bus via constructor injection | `flash_base.h:60-61` | ✅ |
| `COMMON_DATA` section for bus arrays | `peripheralsmgr.cpp:82` | ✅ |
| DMA-safe buffer allocation via MempoolManager + DevMem | `flash_w25qxx.h:37` comment, `devmem.cpp` | ✅ |

**Not implemented** (optional future enhancement):
- Runtime bind-by-name lookup (`bus("flash")` instead of `spiBus(FLASH_SPI)`)
  - Current pattern `FLASH_SPI → BUS_SPI_1 → 0` achieves the same at compile time
- `BusBase*` polymorphic pointer array in PeripheralsManager
  - Not needed since typed `spiBus(int)`/`uartBus(int)` accessors cover all use cases

**No further action needed** — milestone is complete.

---

### M11: BoardConfig TOML Migration

| Field | Value |
|-------|-------|
| **What** | Migrate `configs/<target>/BoardConfig.lua` from Lua to TOML. Create `configs/<target>/board.toml` with same semantics. Update `generate_config.lua` to read TOML instead of Lua. Or create `generate_config.py` to replace the Lua pipeline entirely. |
| **Why** | Unified TOML toolchain: protocol.toml → config.schema.toml → board.toml. Eliminates Lua dependency from the build pipeline. |
| **Pre-req** | M5 (TOML codegen infrastructure exists); build system can switch reader |
| **Files** | `configs/BoringTechH743/board.toml` (new), `configs/ThetaGPH7/board.toml` (new), `scripts/generate_config.lua` (update to read TOML), or `scripts/generate_config.py` (new Python replacement) |
| **Effort** | L (peripheral data is simple) |
| **Risk** | Low — Lua→TOML is straightforward data migration; the generators already emit the same output regardless of input format |

**TOML structure**:

```toml
[board]
identifier = "BoringTechH743"
name = "BoringTechH743"
mcu = "STM32H743xx"
mcu_series = "STM32H7"

[led0]
pin = "PC0"
active_low = false

[keypad]
drive_mode = "scan_matrix"
active_mode = "low"

[[keypad.drive_pins]]
pin = "PD8"

[[keypad.drive_pins]]
pin = "PD9"

# ... etc
```

---

## 5. Branch Strategy

```
main (e35f65b)
│  Profile Flash v3 completed
│
├── config-schema         ← M4 + M5 (create schema + codegen)
│   ├── config-schema-v1  ← M4 only (schema TOML definition, review)
│   └── config-codegen    ← M5 only (gen_config.py, regenerate config_store)
│
├── config-domain         ← M6 (implement config.* commands)
│   └── (depends on config-schema merged first)
│
├── profile-head          ← M7 (add ProfileHead to User Ring)
│   └── (can branch from main, independent of schema TOML)
│
├── device-self-desc      ← M8 (enhanced get_device_info)
│   └── (can branch from config-domain or config-schema)
│
├── profile-tests         ← M9 (test suite)
│   └── (can branch from main, test existing ProfileStore)
│
├── peripherals-mgr-v2    ← M10 (PeripheralsManager redesign)
│   └── (entirely separate, long-lived feature branch)
│
└── board-config-toml     ← M11 (BoardConfig TOML migration)
    └── (last, depends on M5 infrastructure existing)
```

**Merge order**:
1. `profile-tests` → `main` (immediately — parallelizable, no risk)
2. `config-schema` → `main` (review schema thoroughly)
3. `profile-head` → `main` (or merge into `config-schema` if head includes schema version)
4. `config-domain` → `main` (needs schema + codegen)
5. `device-self-desc` → `main` (needs schema)
6. `peripherals-mgr-v2` — merged into main (already done)
7. `board-config-toml` → `main` (last, only when Lua dependency must die)

---

## 6. Acceptance Criteria

### M4 (Config Schema TOML)
- `protocol/config.schema.toml` exists and defines all 40+ keys from `docs/cdc-config-schema.md`
- Schema can be parsed by Python `tomllib`
- Schema includes: group, name, type, range, default, description for each key

### M5 (Config Code Generation)
- `python3 scripts/gen_config.py` produces valid C++ header
- Generated `ConfigStore` struct matches hand-written size (currently ~150 bytes with btn_map[32])
- Generated `serializeConfig()` and `deserializeConfig()` compile and pass tests
- `static_assert(sizeof(ConfigStore) == EXPECTED)` passes
- Rust and TS types are generated and usable

### M6 (Config Domain)
- `config.set_key` with valid key → updates s_config, returns success
- `config.set_key` with invalid key → returns `ERR_INVALID_PARAM`
- `config.get_key` with valid key → returns current value
- `config.get_key` with invalid key → returns `ERR_INVALID_PARAM`
- `config.save` → persists current s_config to profile store
- `config.load` → reloads from profile store into s_config
- `config.factory_reset` → resets s_config to defaults, does NOT auto-save

### M7 (Profile Head)
- User Ring entries have 16-byte `ProfileHead` before JSON body
- `ProfileHead.magic == "THGP"` validates entry
- `ProfileHead.crc32` covers JSON body; mismatch → log warning, skip entry
- Backwards compatibility with old User Ring (pure JSON) at init time
- Existing tests pass without modification

### M8 (Device Self-Description)
- `config.get_device_info` returns 10+ fields (was 3)
- Generated `build_device_info.h` contains correct constants
- Peripherals section reflects actual BoardConfig hardware

### M9 (Profile Test Suite)
- All 14 test cases from §4 pass on device
- Python CDC test script `test_profile.py` can run against real hardware
- Test results documented in test log

### ✅ M10 (PeripheralsManager Redesign)
- Verified already implemented:
  - `PeripheralsManager::spiBus(FLASH_SPI)` returns correct SPI bus instance (FLASH_SPI = BUS_SPI_1 = 0)
  - `DevMem` pool allocations succeed and are DMA-safe (confirmed in `devmem.cpp`)
- All existing peripheral code compiles without `#ifdef USE_*` explosion
- Performance: no increase in init time or RAM usage

### M11 (BoardConfig TOML)
- `board.toml` parses and generates identical `BoardConfig.h` as `BoardConfig.lua`
- Both targets build successfully with TOML source
- Build diff: generated `BoardConfig.h` is byte-for-byte identical

---

## 7. Suggested Execution Order

### Phase 0 — Foundation (Weeks 1-3)

```
Week 1: M4 Config Schema TOML
  Day 1-2: Define all keys, types, ranges in config.schema.toml
  Day 3-4: Review against docs/cdc-config-schema.md (resolve discrepancies)
  Day 5: Update cdc-config-schema.md to reference TOML as SSOT

Week 2-3: M5 Config Code Generation
  Day 1-3: Extend gen_proto.py pattern → gen_config.py
  Day 4-5: Generate C++ struct, verify size, compile on target
  Day 6-7: Generate Rust/TS types, verify host-side
  Day 8-10: Remove hand-written config_store.h/.cpp, replace with generated
```

### Phase 0.5 — Immediate Parallel Task

```
M9 Profile Test Suite (parallel with Phase 0)
  Week 1-2: Write Python CDC test scripts
  Week 2-3: Test on real hardware, fix issues found
  Merge to main immediately after passing
```

### Phase 1 — Profile Enhancement (Weeks 4-6)

```
Week 4: M7 Profile Head + Payload
  Day 1-2: Define ProfileHead struct, modify User Ring layout
  Day 3-4: Backwards compatibility logic for old format
  Day 5-6: Update ProfileStore read/write functions
  Day 7: Integration test with test suite

Week 5: M6 Config Domain Implementation
  Day 1-2: Implement config_cmd_handler based on M5 codegen
  Day 3-4: set_key / get_key with schema lookup
  Day 5: save / load / factory_reset
  Day 6-7: Integration tests via CDC

Week 6: M8 Device Self-Description
  Day 1-2: Extend get_device_info handler
  Day 3-4: Build-time descriptor generation
  Day 5-6: Integration test
```

### Phase 2 — Architecture Redesign (Weeks 7-12)

```
Weeks 7-10: M10 PeripheralsManager Redesign
  Week 7: DevMem pool implementation
  Week 8: Bus registry + bind-by-name lookup
  Week 9: Update generators for new DESC_TABLE format
  Week 10: Migration of all peripheral consumers

Weeks 11-12: M11 BoardConfig TOML Migration
  Day 1-3: Create board.toml for both targets
  Day 4-6: Update generate_config.lua (or write generate_config.py)
  Day 7-10: Testing, diff verification, build validation
```

---

## 8. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| ConfigStore struct generated by codegen differs from hand-written version | Build failure, runtime data corruption | Medium | Generated code must include `static_assert(sizeof == N)`; verify with integration tests |
| Profile head breaks existing flashed devices | Device boots with defaults, user loses config | High (for existing devices) | Implement backwards-compat detection (check first 4 bytes for magic); document reflash requirement |
| set_key/get_key path parsing too complex for device | Code bloat, RAM pressure | Low | Use flat schema (no nested JSON paths in device); host handles nesting, device uses flat lookup |
| PeripheralsManager was redesigned during Profile Flash v3 | Long review cycles, merge conflicts | Already done ✅ | None needed |
| Lua build dependency hard to remove | Phase 2 delayed | Low | Both Lua and TOML readers can coexist during migration; gradual deprecation |
| Compaction with power loss causes data loss | Device loses all user profiles | Low | Current design already handles this: fall back to Profile0 from Sector 1 backup |
| Config schema version mismatch between device and host | Host sends unknown keys, device misinterprets | Medium | `config.get_device_info` includes schema version; host must check before sending; device rejects unknown keys with `ERR_INVALID_PARAM` |

---

## Appendix A: File Change Summary

| Milestone | New Files | Modified Files | Deleted Files |
|-----------|-----------|----------------|---------------|
| M4 | `protocol/config.schema.toml` | `docs/cdc-config-schema.md` | — |
| M5 | `scripts/gen_config.py` | `src/gamepad/config/config_store.h` (regenerated), `src/gamepad/config/config_store.cpp` (regenerated), `protocol/config.rs`, `protocol/config.ts` | (possibly old config_store.h if removed) |
| M6 | `src/test/config_cmd_handler.h/.cpp` | `src/test/init.cpp`, `protocol/protocol.toml` | — |
| M7 | — | `src/drivers/device/flash/profile_flash.h/.cpp` | — |
| M8 | `src/test/device_info.h` (maybe) | `src/test/testsys.h/.cpp`, `protocol/protocol.toml`, `scripts/gen_config.py` | — |
| M9 | `scripts/test/test_profile.py` | — | — |
| M10 | `src/drivers/peripherals/devmem.h/.cpp` | `src/drivers/peripherals/peripheralsmgr.h/.cpp`, `scripts/config_lib/generators/spi.lua`, `scripts/config_lib/generators/uart.lua` | — |
| M11 | `configs/BoringTechH743/board.toml`, `configs/ThetaGPH7/board.toml` | `scripts/generate_config.lua` (or new `scripts/generate_config.py`) | — |

## Appendix B: Architecture After Migration

```
┌─ Build Time ──────────────────────────────────────────────────────┐
│ protocol/config.schema.toml ──→ gen_config.py ──→ ConfigStore.h   │
│     (runtime config SSOT)                     ──→ config.rs/.ts   │
│                                                  ──→ serde funcs   │
│                                                                     │
│ configs/<target>/board.toml  ──→ gen_board.py ──→ BoardConfig.h   │
│     (hardware config SSOT)                    ──→ DESC_TABLE data  │
│                                                                     │
│ protocol/protocol.toml       ──→ gen_proto.py ──→ proto.h/.rs/.ts │
│     (CDC protocol SSOT)                                           │
└─────────────────────────────────────────────────────────────────────┘

┌─ Runtime ─────────────────────────────────────────────────────────┐
│ ProfileStore (Flash) ──loadActive()──→ s_staging                   │
│   │  [ProfileHead + JSON body]                                     │
│   ↓                                                                │
│ parseProfile() → s_config (generated ConfigStore)                  │
│   │                                                                │
│   ↓ (config.set_key / dirty flag)                                  │
│ ConfigManager::saveProfile() → serialize → flash                   │
│                                                                     │
│ CDC commands:                                                       │
│   profile.* ←→ ProfileStore (existing)                             │
│   config.*  ←→ ConfigManager (NEW — set_key/get_key/save/load/     │
│                factory_reset via generated serde lookup)            │
│   sys.*     ←→ System (enhanced get_device_info with build-time    │
│                self-description)                                    │
│                                                                     │
│ PeripheralsManager:                                                 │
│   bus("flash") → SpiBus instance from DevMem pool                  │
│   bus("logger") → UartBus instance                                 │
└─────────────────────────────────────────────────────────────────────┘
```

---

*End of Plan*
