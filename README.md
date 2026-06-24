# ThetaGP

<p align="center">
  <img src="asset/thetagp-logo.png" alt="ThetaGP Logo" width="400">
</p>

Multi-MCU universal gamepad firmware with USB HID + CDC support and
TOML-based board configuration.

## Supported MCUs

| MCU Family | Status |
|------------|--------|
| STM32H7    | ✅ Supported |

## Features

- USB HID gamepad (GP2040-CE compatible button mapping)
- Scan-matrix keypad input with debounce
- Configurable debug log output over UART
- TOML board configuration with validation
- **CDC ACM test channel** — JSON protocol over USB virtual serial port
- **Test API** — inject/observe GamepadRawInput and HIDReport via CDC
- TinyUSB stack (auto-fetched, auto-updated)
- No RTOS — cooperative task scheduler
- Static memory pool allocator (no malloc/free)
- ArduinoJson v7.4.3 for JSON serialization

## Hardware Requirements

| Component | Requirement | Notes |
|-----------|-------------|-------|
| **SPI Flash** | **Required** | W25Q128 (16MB) or compatible. Used for WearLevel config storage and OTA staging. |
| MCU | STM32H7 series | Other families may work with platform porting |
| USB Connector | USB-C or USB Micro-B | For HID + CDC communication |
| Debug Probe | CMSIS-DAP / ST-Link / J-Link | Required for flashing and debug. probe-rs supports all three. |

## Prerequisites

- CMake 3.22+
- ARM GNU Toolchain (`arm-none-eabi-gcc`)
- Python 3.11+
- Git (for dependency fetching)
- [probe-rs](https://probe.rs) (for flashing/debugging)

## Quick Start

```bash
# Configure (fetches dependencies automatically on first run)
cmake -B build -DTARGET=BoringTechH743

# Build
cmake --build build

# Flash
cmake --build build --target flash
```

## Project Structure

```
ThetaGP/
├── configs/                    Board configurations
│   ├── CONFIGURATION.md        Configuration authoring guide
│   └── <TARGET>/
│       ├── BoardConfig.toml    Input: pins, keypad, USB, UART
│       ├── BoardConfig.h       Generated: C macros from TOML
│       └── board_config.cmake  Generated: build variables
├── platform/                   MCU-specific code
│   ├── CMakeLists.txt
│   └── STM32/
│       ├── cmake/              Toolchain
│       ├── peripherals/        HAL implementations
│       ├── startup/            CMSIS startup
│       ├── link/               Linker script
│       └── system/             HAL config, system init, syscalls
├── protocol/                   Protocol definition & generated code
│   ├── protocol.toml           Single source of truth (CDC JSON protocol)
│   └── proto.h                 Generated C++ header
├── scripts/                    Build tooling
│   ├── generate_config.py      Board config generator (TOML → C macros)
│   ├── gen_proto.py            Protocol code generator
│   ├── config/                 Generator modules (validators, generators, pin utils)
│   └── test/                   Python test suite via CDC ACM
├── src/                        Application code
│   ├── conf/                   TinyUSB configuration
│   ├── drivers/                Device & gamepad drivers
│   ├── gamepad/                Core gamepad logic & scheduler
│   ├── test/                   Test API
│   ├── utils/                  Memory pool, logging, atomic, time
│   └── taskmanager.cpp/h       Task lifecycle
├── lib/                        Third-party libraries (auto-fetched)
│   └── CMakeLists.txt          Dependency declarations + fetch logic
├── docs/                       Design documentation
├── AGENTS.md                   AI agent behavior spec
└── README.md                   This file
```

## Configuration

Board configuration uses TOML files under `configs/<TARGET>/BoardConfig.toml`.
At configure time, `scripts/generate_config.py` validates the config and
generates `BoardConfig.h` (C `#define` macros) and `board_config.cmake`.

See **[configs/CONFIGURATION.md](configs/CONFIGURATION.md)** for the full
configuration guide — all fields, valid values, and examples.

To regenerate after editing:

```bash
cmake -B build -DTARGET=<TARGET>
```

## Dependencies

Third-party libraries are declared in `lib/CMakeLists.txt` and fetched
automatically during CMake configure:

| Library | Source | Purpose |
|---------|--------|---------|
| TinyUSB | github.com/sin1111yi/tinyusb | USB device/host stack |
| mbedTLS | github.com/Mbed-TLS/mbedtls | Cryptographic library |
| frozen | github.com/cesanta/frozen | C JSON parser (build-time config) |

On each configure, the build system checks upstream for updates and
fast-forwards if behind. No manual submodule management needed.

## Adding an MCU Family

1. Add toolchain config in `platform/<FAMILY>/cmake/`
2. Add startup + linker files
3. Implement peripheral HALs in `platform/<FAMILY>/peripherals/`
4. Add CMake condition in `platform/CMakeLists.txt`

## Adding a New Board

1. Create `configs/<BOARD>/BoardConfig.toml` (see `CONFIGURATION.md`)
2. Build with `cmake -B build -DTARGET=<BOARD>`

## Architecture

- **No dynamic allocation**: All memory is static or pool-allocated via `MempoolManager`
- **No RTOS**: Cooperative scheduler in `gamepad/scheduler/` driven by `TaskManager`
- **USB stack**: TinyUSB handles device enumeration and HID class driver registration
- **Peripheral abstraction**: MCU-agnostic enums in headers, HAL mapping in platform `.cpp` files

## Acknowledgements

- **GP2040-CE** — GPDriver/Manager design pattern inspiration
  https://github.com/OpenStickCommunity/GP2040-CE
- **Betaflight** — Cooperative scheduler, ISR structure, NVIC/atomic primitives
  https://github.com/betaflight/betaflight

## License

GPL-3.0
