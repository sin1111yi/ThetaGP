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
Agent: <agent-name> <agent-email>
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
Agent: opencode opencode@anomaly.co
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
# Using Lua build script (preferred)
lua tool.lua build --target BoringTechH743

# Or CMake directly
cmake -B build -DTARGET=BoringTechH743
cmake --build build -- -j$(nproc)
```

### Flashing

```bash
lua tool.lua flash --target BoringTechH743 --openocd-cfg openocd/stm32h7x_dual_bank-cmsis-dap.cfg
```

The debug adapter is CMSIS-DAP (VID:PID 0d28:0204). Connect via SWD, udev rule at `/etc/udev/rules.d/99-cmsis-dap.rules`.

### Serial output

Logger outputs on UART1 (PA9 TX, PA10 RX) at 115200 baud. The CMSIS-DAP provides a VCP bridge on `ttyACM0`.

---

## 6. Hardware Platform Constraints

### MCU

- STM32H743 (Cortex-M7 r1p1, 400MHz)
- Flash: 2MB (dual bank), RAM: 1MB (distributed across DTCM, AXI SRAM, SRAMs)

### Memory layout (critical)

| Region | Address | DMA accessible? | Use |
|--------|---------|-----------------|-----|
| DTCMRAM | 0x20000000 | **NO** | Stack, hot data |
| AXI SRAM (`.ram_data`) | 0x24000000 | **YES** | DMA buffers, USB descriptors |
| SRAM1/2/3 | 0x30000000 | YES | General purpose |

**Rule:** All DMA buffers must be placed in AXI SRAM via `COMMON_CODE` / `__attribute__((section(".ram_data")))`. Buffers in DTCMRAM will silently fail DMA transfers.

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

BoardConfig macros (`SPI_2_PERIPHERAL`, `SPI_2_SCLK`, etc.) are generated from `BoardConfig.lua` via the SPI generator. The pattern uses `CONTACT3(FLASH_SPI, _, name)` to resolve `FLASH_SPI` → `SPI_2` → `SPI_2_PERIPHERAL`.

---

## 8. Configuration System

### Pipeline

```
BoardConfig.lua → generators/ → BoardConfig.h + board_config.cmake
```

### Adding a new peripheral

1. Add config data to `configs/<target>/BoardConfig.lua` under the appropriate `bus` key
2. Create a generator in `scripts/config_lib/generators/` following the existing pattern (see `uart.lua` or `spi.lua`)
3. Register the generator in `generators/init.lua`
4. Wire it in `scripts/generate_config.lua`

### Macro naming convention

- Instance names use underscores to avoid HAL macro conflicts: `SPI_2` not `SPI2`, `UART_1` not `UART1`
- Bind prefix + underscore suffix pattern: `LOGGER_UART`, `FLASH_SPI`
- Sub-macros use the instance name as prefix: `UART_1_TX_PIN`, `SPI_2_SCLK`
- Sub-macros are resolved via `CONTACT3(bind_macro, _, name)`

### UART example (BoardConfig.lua)

```lua
bus = {
    uart = {
        { bind = "logger", peripheral = "UART1", tx = "PA9", rx = "PA10" },
    },
    spi = {
        { bind = "flash", peripheral = "SPI2", sclk = "PB13",
          mosi = "PB15", miso = "PB14", ncs = "PB12" },
    },
}
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