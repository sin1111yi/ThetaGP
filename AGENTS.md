# Agent Behavior Specification

This document standardizes AI agent behavior. Read the corresponding section only when you need to perform the relevant action.

## Table of Contents

1. [Behavior](#0-behavior)
2. [Commit Specification](#1-commit-specification)
3. [Coding Style](#3-coding-style)
4. [Thinking](#2-thinking)
5. [Query Priority](#4-query-priority)
6. [Build & Flash](#5-build--flash)
7. [Hardware Platform Constraints](#6-hardware-platform-constraints)
   - [Memory section classification](#memory-section-classification)
   - [System RAM access properties](#system-ram-access-properties)
   - [PERSISTENT data (survives warm reset)](#persistent-data-survives-warm-reset)
8. [Device Driver Architecture](#7-device-driver-architecture)
9. [Configuration System](#8-configuration-system)
10. [Review Focus Areas](#9-review-focus-areas)

---

## 0. Behavior

- **Never commit without explicit permission**
- **Never modify library files or third-party dependencies**

---

## 1. Commit Specification

### Language

- **Allowed**: English

### Format

```
<type>(<scope>): <subject>

<body>

Model: <model-name> <model-provider>
Agent: <agent-name>
```

### Type

- `feat`: new feature
- `fix`: bug fix
- `refactor`: code refactoring (non-functional change)
- `docs`: documentation
- `test`: testing
- `chore`: build/tooling
- **Custom types**: permitted (e.g., `perf`, `ci`, `build`)

### Subject

- Use imperative mood ("add" not "added")
- No leading capital letter
- No trailing period
- Keep under 50 characters

### Body

- Each line ≤ 72 characters
- Explain WHY and HOW, not WHAT
- Use `-` bullet points for details

### Example

Proposed commit:

```
fix(usb): correct clock macros and port configuration

- Fix USB clock macros: USB1_OTG_HS -> USB_OTG_HS
- Change BOARD_TUD_RHPORT from 0 to 1 for correct port mapping
- Update ULPI, high-speed and full-speed pin initialization macros

Model: MiniMax-M2.7 MiniMax
Agent: opencode
```

Should I commit this change?

---

## 2. Thinking

The agent's thinking and output language should match the host machine's current locale. Check locale with `locale` or `echo $LANG` command.

---

## 3. Coding Style

### Member Initialization

Use C++11 inline member initializers instead of constructor initializer
lists. Reserve `: member(value)` syntax only for base class constructors
and reference members.

```cpp
// Good
class Foo {
  int _count = 0;
  Bar *_bar = nullptr;
  bool _ready = false;
  Foo() = default;
};

// Bad
class Foo {
  int _count;
  Bar *_bar;
  bool _ready;
  Foo() : _count(0), _bar(nullptr), _ready(false) {}
};
```

```cpp
// Good: base class ctor (required)
class Derived : public Base {
  Derived() : Base(someArg) {}
};

// Good: reference member (required)
class Holder {
  Dep &_dep;
  Holder() : _dep(Dep::getInstance()) {}
};
```

### Comments

Comments describe what the code does, nothing else.

- Write **property statements** only — what this code is/does. Never
  reference design documents, ADR numbers, decision history, rejected
  alternatives, or lessons learned. Those live in `docs/`; a comment that
  cites them rots silently when the document moves on.
- Drop parenthetical asides that are not part of the code's meaning.
- **English only.** No Chinese in comments.
- No doxygen/template blocks. If the code already says it clearly, write
  no comment. Section separators are `// ── section ──`.

---

## 4. Query Priority

Before any operation, query files in this order:

1. **AGENTS.md** - this file
2. **README.md** - project overview and build instructions
3. **Project structure** - understand code organization
4. **Relevant headers** - understand interfaces and types
5. **Similar implementations** - reference existing code patterns

---

## 5. Build & Flash

### Supported targets

- `BoringTechH743` — main development target (STM32H743)
- `ThetaGPH7` — secondary target

### Building

```bash
# Build
cmake -B build -DTARGET=BoringTechH743
cmake --build build

# Build with test API enabled
cmake -B build -DTARGET=BoringTechH743 -DTHETAGP_CFG_TEST=ON
cmake --build build
```

Note: `THETAGP_CFG_TEST` is the compile switch that pulls in the test
domain handlers (see `src/CMakeLists.txt`). The old
`THETAGP_ENABLE_TEST_API` name is retired.

### Flashing

**Use `BOARD_CHIP`, which holds the probe-rs target name.**

```bash
probe-rs download --chip ${BOARD_CHIP} build/ThetaGP_*.elf
probe-rs reset --chip ${BOARD_CHIP}
```

`BOARD_CHIP` comes from `configs/<TARGET>/board_config.cmake` (generated from
`BoardConfig.toml` `[board_info].chip`); both targets currently carry
`STM32H743VI`, which is a probe-rs target (verified working). Both `download`
and `reset` need it.

Do **not** put ST's **orderable part number** in that field. `STM32H743VITx`
(package and temperature suffix included) is a legal-looking value that
probe-rs 0.32.0 has no target for: `--chip STM32H743VITx` fails with
"Unable to load specification for chip / The connected chip could not
automatically be determined". The orderable part number belongs in
`[board_info].mcu`.

The debug adapter is CMSIS-DAP (VID:PID 0d28:0204). Connect via SWD, udev rule at `/etc/udev/rules.d/99-cmsis-dap.rules`.

### Serial output

Logger outputs on UART1 (PA9 TX, PA10 RX) at 115200 baud. The CMSIS-DAP (DAPLink) provides a VCP bridge on `ttyACM0`.

### CDC command channel

The CDC ACM virtual serial port carrying JSON commands is present in **every**
build — the interface is unconditional in the USB descriptors and
`CFG_TUD_CDC` is always 1. `-DTHETAGP_CFG_TEST=ON` gates only the `test.*`
command set: without it those handlers compile to no-op stubs. The `sys.*`
domain is always live, and `profile.*` is live whenever the board declares a
flash chip (`THETAGP_CFG_HAS_FLASH`).

Plugged via the device's own USB interface (not the debug probe). The ttyACM
number shifts across flashes; locate it via the stable symlink instead of a
hardcoded node:

```bash
ls -l /dev/serial/by-id/usb-ThetaGamepad*if01*
# e.g. usb-ThetaGamepad_BoringTechH743-if01 -> ../../ttyACM0

# Send a ping
echo '{"cmd":"sys.ping","queued":0}' > /dev/serial/by-id/usb-ThetaGamepad*if01*
# Read response
cat /dev/serial/by-id/usb-ThetaGamepad*if01*
```

See `docs/cdc-json-protocol.md` for the full protocol specification and the
automated suites under `scripts/test/`:

- `test_cdc_protocol.py` — sys domain + read-only test domain commands
  (`test.flash_info`, `test.mem_info`, `test.flash_read`,
  `test.spi_mode`; `test.mem_info` reports the six raw linker memory
  regions). Destructive commands (`test.chip_erase`,
  `test.erase_sector`, `test.compaction`) are intentionally not executed
  here; they are covered by the profile suite. The suite is board-aware:
  it derives whether the board declares an external flash from
  `sys.get_usage.ext_flash_total_sectors`, and when it does not, the
  flash/SPI commands are reported as SKIP with that reason instead of
  FAIL — those commands are compiled out without a chip, so a failure
  there would say nothing. On a board with a flash they must all pass.
  A run against a firmware built without `THETAGP_CFG_TEST` skips both
  the test-domain group and the checks that compare it against
  `sys.get_usage`.
- `test_profile.py` — profile store lifecycle (create/delete/select/save/
  load, wraparound, compaction, CRC, 16-profile limit).
- `cdc_serial.py` — shared serial I/O helper (stable-symlink port
  discovery, raw mode setup).

The gamepad/HID injection commands (`test.set_mode`, `test.inject_*`,
`test.set_override`, history/reset) were removed in commit 2408e32
(refactor(test): remove TestInjector and gamepad/HID report hooks) and do
not exist in current firmware.

---

## 6. Hardware Platform Constraints

### MCU

- STM32H743 (Cortex-M7 r1p1, 480 MHz — PLL1 M=5/N=192/P=2 on a 25 MHz HSE)
- Flash: 2MB (dual bank), RAM: 1MB (distributed across DTCM, AXI SRAM, SRAMs)

### Memory section classification

Macro chain (defined in `build_info.h` + `target_platform.h`):
```
COMMON_DATA              → RAM_DATA → __attribute__((section(".ram_data"), aligned(32)))            → System RAM (DMA-accessible) (e.g. AXI SRAM)
COMMON_ZERO_INIT               → RAM_BSS  → __attribute__((section(".ram_bss"), aligned(32)))             → System RAM (DMA-accessible), NOLOAD (zero-init, no Flash copy)
FAST_DATA             → DTCM_RAM_DATA → __attribute__((section(".dtcmram_data")))                → fast CPU-local RAM (DTCM), initialized
FAST_DATA_ZERO_INIT   → DTCM_RAM_BSS  → __attribute__((section(".dtcmram_bss")))                 → fast CPU-local RAM (DTCM), NOLOAD (zero-init, no Flash copy)
FAST_CODE             → DTCM_RAM_CODE → __attribute__((section(".dtcmram_code")))                   → hot-path function in DTCM RAM (0-wait, copied from Flash)
FAST_CODE_PREF        → reserved — prefer fast RAM, fallback to Flash
FAST_CODE_NOINLINE    → FAST_CODE + __attribute__((noinline))
COMMON_DATA_AUTO         → Convenience: `static COMMON_DATA` (shorthand for `static COMMON_DATA ...`)
```

Three kinds of memory are available for variables:
- **Fast RAM (FAST_DATA / FAST_DATA_ZERO_INIT)** — low-latency, CPU-private (DTCM). **Not accessible by DMA.** Use for hot-path data.
- **System RAM (COMMON_DATA / COMMON_ZERO_INIT)** — larger, DMA-accessible (AXI SRAM). Use for buffers and cold data.
- **DTCMRAM stack** — CPU stack (8 KB), placed at top of DTCM.

Which memory maps to which physical region depends on the target platform. Check `target_platform.h` in the platform directory for the exact mapping.

#### MUST use `COMMON_DATA` / `COMMON_ZERO_INIT`

Variables that are either DMA-accessed, or large/cold enough to justify saving fast RAM.

| Condition | Macro | Examples |
|-----------|-------|----------|
| **DMA buffers** (fast RAM is not DMA-accessible) | `COMMON_ZERO_INIT` | CDC buffers, USB descriptors, SPI staging buffers (`s_nv3kSpiTxBuf`, `s_flashSpiTxBuf`) |
| **ISR dispatch tables** (low-frequency ISRs) | `COMMON_ZERO_INIT` | DMA/SPI/UART/TIM ISR callback tables |
| **Large NOLOAD buffers** (allocated once, touched off the hot path) | `COMMON_ZERO_INIT` | CDC command queue (`FrameLayer::_cmdQueue`, 16,384 B), test-domain response staging (`s_testRespBuf`, 4,096 B) |
| **Log/infrastructure buffers** (slow path) | `COMMON_ZERO_INIT` | Log ring buffer |
| **Peripheral config tables** (constructor-initialized) | `COMMON_DATA` | SPI/UART bus descriptor arrays |
| **Init-once, rarely-touched values** | `COMMON_ZERO_INIT` | CPU frequency cache, config sizes, USB state |
| **Low-frequency volatile flags** (ISR-safe with Non-Cacheable RAM) | `COMMON_ZERO_INIT` | Mounted/suspended flags |

Rules:
- **`COMMON_ZERO_INIT`** — variable is zero-initialized (NOLOAD section, no Flash copy). Use for any variable with no initializer, `= {}`, `= 0`, or `= NULL`.
- **`COMMON_DATA`** — variable has non-zero initial value or constructor. Uses Flash load image.
- **`COMMON_DATA_AUTO`** — shorthand for `static COMMON_DATA`.
- **`aligned(32)`** — automatically applied by both DMA macros for cache line alignment.
- **`FAST_DATA_ZERO_INIT`** — hot path variable, zero-initialized in DTCM (NOLOAD, no Flash copy).
- **`FAST_DATA`** — hot path variable with initial value, initialized from Flash load image into DTCM.
- **Registration** — any new static buffer of 256 B or more must be recorded
  in the buffer register (name, size, macro, region) so total static usage
  stays reviewable in one place. The register currently lives in
  `docs/analysis/adr-0003-mempool-removal.md` §3.1.

Usage:
```cpp
// DMA-safe (AXI SRAM)
COMMON_ZERO_INIT static uint8_t s_buffer[1024];     // zero-init, no Flash overhead
COMMON_DATA static MyObj obj = { .val = 42 }; // has initial value, Flash load
COMMON_DATA_AUTO uint32_t counters[8];        // shorthand

// Fast CPU-local (DTCMRAM)
FAST_DATA_ZERO_INIT static uint32_t usTicks = 0;           // zero-init hot path
FAST_DATA static MyObj hotObj = { .val = 42 };             // initialized hot path
```

#### MUST NOT use `COMMON_DATA` / `COMMON_ZERO_INIT`

**const/constexpr data** belongs in Flash/ROM, not in RAM:
```cpp
// WRONG — forces constexpr data from Flash into RAM
COMMON_ZERO_INIT static constexpr uint32_t lookup[] = { ... };

// RIGHT — stays in Flash
static constexpr uint32_t lookup[] = { ... };
static const std::array<Type, N> table = { ... };
```

**Hot path variables** belong in fast RAM (0-wait or minimal latency) — use `FAST_DATA` / `FAST_DATA_ZERO_INIT`:
| Variable type | Reason |
|---------------|--------|
| Timing bases (`usTicks`, `sysTickUptime`, etc.) | Read on every `micros()`/`delay_us()` call |
| Hooks called from poll loops | Gamepad poll / HID send hot path |
| Scheduler records | Dispatched every tick |
| Gamepad state variables | Updated every 1-2ms |
| Singleton instances | Accessed frequently through call chains |

#### SHOULD stay in fast RAM by default

Unmarked `static` variables land in `.data`/`.bss` (fast RAM). This is fine for:
- Small counters and flags accessed on hot paths
- Singleton instances
- Callback pointers called frequently

Do NOT add `FAST_DATA`/`FAST_DATA_ZERO_INIT` speculatively. Only move to one of these when a variable meets the hot-path criteria above.

Do NOT add `COMMON_DATA`/`COMMON_ZERO_INIT` speculatively. Only move to one of these when a variable meets the MUST criteria above.

### System RAM access properties

The system RAM used for COMMON_DATA/COMMON_ZERO_INIT may run under different cache/memory-order properties depending on the target. Two common configurations possible with MPU:

- **Non-cacheable** — writes are visible immediately; `volatile` semantics work correctly with no special handling; no DMA coherency issues
- **Cacheable write-back** — better read performance; requires explicit cache maintenance (D-cache clean/invalidate) for DMA buffers; volatile reads may return stale cached data

Check the board's MPU setup in the system startup (`target_stm32h7xx.c` or equivalent) to confirm which configuration applies.

### PERSISTENT data (survives warm reset)

BetaFlight defines a `.persistent_data` section for data that must survive a warm (software) reset but not a cold (power cycle) reset:

```c
// BetaFlight implementation
#define PERSISTENT __attribute__((section(".persistent_data"), aligned(4)))
```

This is placed in a dedicated RAM region that the startup code does NOT zero on warm reset (only on cold boot). Uses:
- **Calibration values** (accelerometer offsets, gyro temperature coefficients)
- **Boot counters** (detect crash loops)
- **Config that must survive a MCU reset without Flash write**

For ThetaGP, we haven't implemented this yet. To add it, you'd need:
1. Reserve a RAM region in the linker script (e.g. a section in backup SRAM or the end of AXI SRAM)
2. Modify the startup code to skip zeroing this section on warm reset
3. Detect warm vs cold reset via the RCC reset flags register (`RCC->CSR`)
4. Define the `PERSISTENT` macro and place data in that section

If you need this, a good starting point is the backup SRAM (`.ram_d3_data`, 0x38000000, 64KB) which retains data during standby — it only needs a RTC backup battery.

### UART DMA pattern

- LL-layer control only — no `HAL_UART_Transmit_DMA` or `HAL_DMA_Start_IT`
- DMA1 Stream0 for RX, Stream1 for TX (per CubeMX reference)
- CR3 DMAT/DMAR bits set/cleared manually
- State tracked via `DmaChannel::isBusy()`, never via HAL's `gState`/`RxState`
- TX and RX use independent DMA streams — `isTxBusy()` and `isRxBusy()` are separate

### SPI NCS

Chip select (NCS) is software-controlled. The NCS pin must be configured as `GPIO::Mode::OutputPushPull` (not `AlternateFunctionPushPull`), otherwise `Gpio::set()`/`reset()` in `SpiBus::transfer()` will have no effect.

### CMSIS-DAP

DAPLink firmware CMSIS-DAPv2. Connects via SWD. udev rule: `SUBSYSTEM=="usb", ATTR{idVendor}=="0d28", ATTR{idProduct}=="0204", MODE="0666"`.

---

## 7. Device Driver Architecture

### Inheritance

```
Device (device.h) — base class with init(), isInitialized(), getName()
  ├── FlashBase — abstract SPI NOR Flash
  │     └── W25qxxFlash — W25QXX implementation
  ├── Logger — UART logging
  ├── Keypad — button matrix
  └── SystemTimer — timing services
```

### Registration flow

```
ThetaGamepad::setup()
  → DeviceManager::registerDevice(&SomeDevice::getInstance())
  → DeviceManager::initDevices()
    → SomeDevice::init() called for each uninitialized device
```

All devices go through `DeviceManager`. The `init()` method sets `_initialized = true` on success.

### Adding a new device

1. Create a class inheriting `Device` (or an intermediate abstract like `FlashBase`)
2. Implement `void init() override`
3. Use singleton pattern (`static Xxx &getInstance()`)
4. Register in `ThetaGP.cpp` before `initDevices()`

### Flash driver pattern

New SPI Flash chips inherit `FlashBase` and use the `FLASH_SPI_INIT(name)` macro:

```cpp
class Gd25qFlash : public FlashBase {
  // Implement read(), write(), eraseSector(), eraseChip(), readId(),
  // getInfo(), isBusy()
};
```

BoardConfig macros (`BDCFG_SPI_*`, `BDCFG_LOGGER_UART`, etc.) are generated from `BoardConfig.toml` via the Python config generator. A bind macro names its bus by index: `BDCFG_FLASH_SPI` resolves to `BUS_SPI_1`, which the driver turns into the bus instance it talks to.

---

## 8. Configuration System

### Pipeline

```
BoardConfig.toml → scripts/generate_config.py → BoardConfig.h + board_config.cmake
```

### Configuration layers

Every parameter belongs to exactly one of two layers. The question that
decides it: does changing this value depend on **how the firmware behaves**,
or on **what this board looks like**?

| Layer | Where | Holds |
|-------|-------|-------|
| Firmware | `src/conf/ThetaGP_Config.h` | Behaviour identical across every build target unless the board's electrical properties decide otherwise: report rate (`[usb] wired_report_hz` overrides it, bounded by the USB speed), keypad scan frequency, debounce windows, timeouts, retry counts. |
| Board | `configs/<target>/BoardConfig.toml` | Pin assignment, peripheral selection, USB hardware and speed, and anything the board's electrical properties decide. |

A firmware parameter carries a default and is normally left alone. Each one
is wrapped in `#ifndef` so a board-specific value can override it, which is
why `ThetaGP_Config.h` is included **after** `BoardConfig.h`.

A firmware parameter may still be decided by the board's electrical
properties — `THETAGP_CFG_KEYPAD_GPIO_SETTLE_US` is one: its default assumes
a 30–50 kΩ pull-up against 10–20 pF, so a board that changes the pull-up or
lengthens the trace must recompute it. Such values stay in
`ThetaGP_Config.h` because the default holds for ordinary boards; state the
assumption in the comment so a misfiring keypad points straight at it.

The wire report rate follows the same shape for a different reason: the most a
link can carry is set by the USB speed the board declares, so the rate is kept
as a firmware default of 1000 Hz and a board may raise it with
`[usb] wired_report_hz` (generated as `BDCFG_REPORT_RATE_HZ`, bridged into
`THETAGP_CFG_USB_REPORT_RATE_HZ`, rejected above the ceiling the speed allows).

### Adding a new peripheral

1. Add config data to `configs/<target>/BoardConfig.toml` under the appropriate `bus` key
2. Add generator logic to `scripts/generate_config.py` following the existing pattern (see `gen_uart_lines()` or `gen_spi_lines()`)

### Macro naming convention

- Instance names use underscores to avoid HAL macro conflicts: `SPI_2` not `SPI2`, `UART_1` not `UART1`
- Bind prefix + underscore suffix pattern: `LOGGER_UART`, `FLASH_SPI`
- A bind macro names a bus instance by index: `BDCFG_LOGGER_UART` → `BUS_UART_1` → the instance the driver receives. The pin fields live in the generated descriptor tables, not in per-pin macros.
- The macros the board config generates carry the `BDCFG_` prefix (`BDCFG_SPEED_HS`, `BDCFG_KEYPAD_*`, `BDCFG_LED0_*`, `BDCFG_UART_*`, `BDCFG_SPI_*`, `BDCFG_LOGGER_UART`, `BDCFG_FLASH_SPI`, `BDCFG_HAS_FLASH`, …); the firmware layer bridges them into the `THETAGP_CFG_*` names the software reads

### UART example (BoardConfig.toml)

```toml
[[bus.uart]]
bind       = "logger"
peripheral = "UART1"
tx         = "PA9"
rx         = "PA10"

[[bus.spi]]
bind       = "flash"
peripheral = "SPI2"
sclk       = "PB13"
mosi       = "PB15"
miso       = "PB14"
ncs        = "PB12"
```

---

## 9. Review Focus Areas

Reviewer checks code against these dimensions, with emphasis on embedded-specific concerns:

| Dimension | Focus for this project |
|-----------|----------------------|
| 🔴 Correctness | DMA buffer placement (DTCMRAM vs AXI SRAM), register bit operations, ISR safety, race conditions between ISR and main context |
| 🟠 Robustness | `__HAL_LOCK` cannot be used in void ISR functions (macro contains `return HAL_BUSY`), error flags in ISR should be logged not silently cleared, polling timeouts should be named constants |
| 🟡 Readability | C++11 inline member initializers preferred over constructor init-lists, pure C callback type (`typedef void (*Callback)(void*)`) — no `std::function` |
| 🔵 Performance | DMA vs polling choice, buffer sizes, CR3 DMAT/DMAR sequencing (set after `_dmaTx/Rx->start()`, clear in completion callback) |
| ⚪ Style | `override` on all virtual overrides, `#pragma once`, `_prefix` for members, namespace `ThetaGP::Drivers::*` |
| ⚪ Architecture | New devices must inherit `Device` and register via `DeviceManager`. Custom ISR pattern is intentional — do not propose switching to HAL callbacks. `struct dummy` in ISR macros is intentional semicolon absorber. |