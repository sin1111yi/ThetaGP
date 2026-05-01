# ThetaGP

<p align="center">
  <img src="asset/thetagp-logo.png" alt="ThetaGP Logo" width="400">
</p>

Multi-MCU universal gamepad firmware with USB HID + CDC support and
Lua-based board configuration system.

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
- Configurable button mappings via BoardConfig.lua
- TinyUSB stack
- No RTOS — cooperative task scheduler
- Static memory pool allocator (no malloc/free)

## Prerequisites

- CMake 3.22+
- ARM GNU Toolchain (`arm-none-eabi-gcc` 13.3.1+)
- Lua 5.3+
- OpenOCD (for flashing)

## Quick Start

```bash
# The following example uses the ThetaGPH7 target (STM32H743)

# Configure and build
lua tool.lua config build --target ThetaGPH7

# Flash
lua tool.lua flash --target ThetaGPH7 --openocd-cfg openocd/stm32h7x_dual_bank-cmsis-dap.cfg

# Or all at once
lua tool.lua all --target ThetaGPH7 --openocd-cfg openocd/stm32h7x_dual_bank-cmsis-dap.cfg
```

Run `lua tool.lua help` for a full list of commands and options.

## Project Structure

```
ThetaGP/
├── configs/                    Board configurations
│   └── <TARGET>/
│       ├── BoardConfig.lua     Input: pins, keypad, USB, UART
│       ├── BoardConfig.h       Generated: C macros from Lua
│       └── board_config.cmake  Generated: build variables
├── platform/                   MCU-specific code
│   ├── CMakeLists.txt          CMake platform selection
│   └── <FAMILY>/
│       ├── cmake/              Toolchain
│       ├── peripherals/        HAL implementations
│       ├── startup/            CMSIS startup file
│       ├── link/               Linker script
│       └── system/             HAL config, system init, syscalls
├── scripts/                    Build & configuration tooling
│   ├── tool.lua               Build/configure/flash helper
│   ├── fetch_deps.lua          Dependency fetcher
│   ├── generate_config.lua     Lua → C macro generator
│   └── config_lib/             Generator & validator modules
├── src/                        Application code
│   ├── conf/                   TinyUSB configuration
│   ├── drivers/                Device & gamepad driver abstraction
│   ├── gamepad/                Core gamepad logic & scheduler
│   ├── peripherals/            MCU-agnostic peripheral interfaces
│   ├── utils/                  Memory pool, logging, atomic, time
│   ├── ThetaGP.cpp             Entry point
│   ├── ThetaGPTasks.cpp        Task definitions
│   └── taskmanager.cpp/h       Task lifecycle
├── lib/                        Third-party libraries
├── openocd/                    OpenOCD configs
├── AGENT.md                    AI agent behavior spec
└── README.md                   This file
```

## Configuration System (Lua Scripts)

Board configuration is driven by a Lua-based pipeline:

- **`configs/<TARGET>/BoardConfig.lua`** — User-editable configuration file
  defining pins, keypad matrix, USB settings, UART, and button mappings
- **`scripts/generate_config.lua`** — Reads BoardConfig.lua, runs
  validators, and generates `BoardConfig.h` (C macros) and
  `board_config.cmake`
- **`scripts/config_lib/validators/`** — Validates configuration fields
  (pin format, key map dimensions, button names, etc.)
- **`scripts/config_lib/generators/`** — Produces `#define` macros from
  validated configuration data
- **`scripts/fetch_deps.lua`** — Downloads external dependencies
  (TinyUSB, mbedTLS, ArduinoJson) via git

To regenerate after editing `BoardConfig.lua`:

```bash
lua tool.lua config --target <TARGET>
```

Or simply build — CMake automatically invokes `generate_config.lua`
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
