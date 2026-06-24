# ThetaGP

<p align="center">
  <img src="asset/thetagp-logo.png" alt="ThetaGP Logo" width="400">
</p>

Multi-MCU universal gamepad firmware with USB HID + CDC support and
TOML-based board configuration system.

## Supported MCUs

| MCU Family | Status |
|------------|--------|
| STM32H7    | ✅ Supported |

Adding support for a new MCU family requires:
- Platform HAL implementation under `platform/<FAMILY>/peripherals/`
- Startup file, linker script, and toolchain configuration
- Enums defined in `src/drivers/peripherals/` are MCU-agnostic;
  platform-specific mapping tables are in the respective `.cpp` files

## Features

- USB HID gamepad (GP2040-CE compatible button mapping)
- Scan-matrix keypad input with debounce
- Configurable debug log output over UART
- Configurable button mappings via BoardConfig.toml
- **CDC ACM test channel** — JSON protocol over USB virtual serial port
- **Test API** — inject/observe GamepadRawInput and HIDReport via CDC, with
  queue injection, history recording, and override mode
- TinyUSB stack
- No RTOS — cooperative task scheduler
- Static memory pool allocator (no malloc/free)
- ArduinoJson v7.4.3 for JSON serialization

## Hardware Requirements

| Component | Requirement | Notes |
|-----------|-------------|-------|
| **SPI Flash** | **Required** | W25Q128 (16MB) or compatible. Used for WearLevel config storage and OTA staging. Must be wired to the MCU's SPI bus. |
| MCU | STM32H7 series | Other families may work with platform porting |
| USB Connector | USB-C or USB Micro-B | For HID + CDC communication |
| Debug Probe | SWD (CMSIS-DAP / ST-Link / J-Link) | Required for flashing and debug |

The SPI flash is mandatory — the firmware stores persistent configuration (button mappings, device settings) and OTA firmware images on it via the WearLevel controller. Without an SPI flash, the firmware will fail to initialize.

## Prerequisites

- CMake 3.22+
- ARM GNU Toolchain (`arm-none-eabi-gcc` 13.3.1+)
- Python 3.11+ (for protocol and config generation)
- OpenOCD (for flashing)

## Quick Start

```bash
# The following example uses the ThetaGPH7 target (STM32H743)

# Configure and build
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
│   └── <TARGET>/
│       ├── BoardConfig.toml    Input: pins, keypad, USB, UART
│       ├── BoardConfig.h       Generated: C macros from TOML
│       └── board_config.cmake  Generated: build variables
├── platform/                   MCU-specific code
│   ├── CMakeLists.txt          CMake platform selection
│   └── <FAMILY>/
│       ├── cmake/              Toolchain
│       ├── peripherals/        HAL implementations
│       ├── startup/            CMSIS startup file
│       ├── link/               Linker script
│       └── system/             HAL config, system init, syscalls
├── openocd/                    OpenOCD configs
├── protocol/                   Protocol definition & generated code
│   ├── protocol.toml           Single source of truth (CDC JSON protocol)
│   └── proto.h                 Generated C++ header (auto-regenerated at configure time)
├── scripts/                    Build & configuration tooling
│   ├── gen_proto.py            Protocol code generator (TOML → C++/Rust/TS)
│   └── generate_config.py      TOML → C macro generator
├── src/                        Application code
│   ├── conf/                   TinyUSB configuration
│   ├── drivers/                Device & gamepad driver abstraction
│   ├── gamepad/                Core gamepad logic & scheduler
│   ├── test/                   Test API (FrameLayer, Dispatcher, TestInjector)
│   ├── utils/                  Memory pool, logging, atomic, time
│   ├── ThetaGP.cpp             Entry point
│   ├── ThetaGPTasks.cpp        Task definitions
│   └── taskmanager.cpp/h       Task lifecycle
├── docs/                       Design documentation
├── lib/                        Third-party libraries
├── AGENTS.md                   AI agent behavior spec
└── README.md                   This file
```

## Configuration System (TOML)

Board configuration is driven by a TOML-based pipeline:

- **`configs/<TARGET>/BoardConfig.toml`** — User-editable configuration file
  defining pins, keypad matrix, USB settings, UART, and button mappings
- **`scripts/generate_config.py`** — Reads BoardConfig.toml, runs
  validators, and generates `BoardConfig.h` (C macros) and
  `board_config.cmake`
Dependencies (TinyUSB, mbedTLS, frozen) are fetched automatically by
CMake `FetchContent` during configure.

To regenerate after editing `BoardConfig.toml`:

```bash
cmake -B build -DTARGET=<TARGET>
```

Or simply build — CMake automatically invokes `generate_config.py`
during configuration.

## Adding an MCU Family

1. Add toolchain config in `platform/<FAMILY>/cmake/`
2. Add startup + linker files
3. Implement peripheral HALs in `platform/<FAMILY>/peripherals/`
4. Add CMake condition in `platform/CMakeLists.txt`

The peripheral interface headers in `src/drivers/peripherals/` use
abstract enums (e.g. `GPIO::Mode::Input`). Each platform's `.cpp`
provides a mapping table or function (e.g. `Gpio::toHalMode()`) to
translate to the SDK-specific constants.

## Architecture Notes

- **No dynamic allocation**: All memory is static or pool-allocated
  via `MempoolManager`
- **Peripheral abstraction**: Enums in headers use MCU-agnostic values;
  HAL mapping is done in platform `.cpp` files via `toHal*()` static
  methods
- **No RTOS**: Cooperative scheduler in `gamepad/scheduler/` driven by
  `TaskManager` with a periodic timer tick
- **USB stack**: TinyUSB handles device enumeration and HID class
  driver registration

## Acknowledgements

- **GP2040-CE** — The GPDriver and Manager design pattern in this
  project is inspired by the GP2040-CE firmware architecture.
  https://github.com/OpenStickCommunity/GP2040-CE

- **Betaflight** — The cooperative task scheduler, interrupt handler
  structure, NVIC priority macros, atomic primitives, and UART
  alternate-function lookup approach are derived from the Betaflight
  flight controller firmware.
  https://github.com/betaflight/betaflight

## Support

For bug reports and feature requests, please open an issue on the
GitHub repository.

## Contact

sin1111yi@foxmail.com

## Contributing

This project is currently in early development. The codebase is still
evolving, and contribution guidelines will be established as the
project matures.

## License

GPL-3.0
