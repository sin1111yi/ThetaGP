# Board Configuration Guide

`BoardConfig.toml` is the single source of truth for per-board pin mappings,
peripheral configuration, and keypad layout. It is read at build time by
`scripts/generate_config.py` and compiled into C `#define` macros
(`BoardConfig.h`) and CMake variables (`board_config.cmake`).

- **Location**: `configs/<TARGET>/BoardConfig.toml`
- **Format**: [TOML](https://toml.io)
- **Generator**: `scripts/generate_config.py` (Python 3.11+, stdlib `tomllib`)

## Quick Reference

```toml
[board_info]
identifier     = "MyBoard"
name           = "MyBoard Rev.A"
mcu            = "STM32H743xx"
mcu_series     = "STM32H7"

[led0]
pin        = "PC0"
active_low = false

[keypad]
drive_mode  = "scan_matrix"
active_mode = "low"
drive_pins  = ["PD8", "PD9"]
sense_pins  = ["PC4", "PC5"]
key_map = [
    [0, 1],
    [2, 3],
]
button_map = [
    [0, "B1"],
    [1, "B2"],
    [2, "B3"],
    [3, "B4"],
]

[usb]
hw_periph = "USB2"
speed     = "full_speed"

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

[flash]
chip = "w25qxx"
```

## Sections

### `[board_info]` — required

| Field | Type | Description |
|-------|------|-------------|
| `identifier` | string | Alphanumeric + underscores. Used in build paths. |
| `name` | string | Human-readable board name. |
| `mcu` | string | MCU part number, e.g. `STM32H743xx`. |
| `mcu_series` | string | One of: `STM32H7`, `STM32F4`, `STM32F1`. |

### Pin macros — optional

Any top-level key with a `pin` field (except `keypad`, `usb`, `bus`, `flash`,
`board_info`) generates a pin macro:

```toml
[led0]
pin        = "PC0"
active_low = false

[led1]
pin        = "PC1"
active_low = true
```

Generates:

```c
#define LED0_PIN                     {Port::PortC, Pin::Pin0}
#define LED0_ACTIVE_LOW              false
#define LED1_PIN                     {Port::PortC, Pin::Pin1}
#define LED1_ACTIVE_LOW              true
```

Pin strings use `P<port><pin>` format: `PA0`–`PI15`. Port letters are A–I.

### `[keypad]` — required

| Field | Type | Description |
|-------|------|-------------|
| `drive_mode` | string | `scan_matrix`, `io_direct`, or `spi_74hc165` |
| `active_mode` | string | `none`, `low`, or `high` (default: `none`) |
| `drive_pins` | string[] | Drive pin list (scan_matrix). 1–8 pins. |
| `sense_pins` | string[] | Sense pin list (scan_matrix). 1–8 pins. |
| `direct_pins` | string[] | Direct pin list (io_direct). 1–8 pins. |
| `spi_chips` | int | Number of 74HC165 chips (spi_74hc165). 1–4. |
| `key_map` | 2-D array | Drive × sense matrix of key indices (scan_matrix). |
| `button_map` | 2-D array | `[index, button_name]` pairs. Max 32 entries. |

**Key map** — each row corresponds to a drive pin, each column to a sense pin.
Use `0xFF` (or the integer `255`) for empty positions:

```toml
key_map = [
    [0, 0xff, 2],
    [3,    4, 5],
]
```

Valid key indices: 0–63. `0xFF` means "no key."

**Button names** — valid values:

```
UP, DOWN, LEFT, RIGHT,
B1, B2, B3, B4,
L1, R1, L2, R2,
S1, S2, L3, R3,
A1, A2, A3, A4,
DU, DD, DL, DR,
E1, E2, E3, E4, E5, E6, E7, E8
```

### `[usb]` — optional

| Field | Type | Description |
|-------|------|-------------|
| `hw_periph` | string | `USB1`, `USB2`, or `ULPI` |
| `speed` | string | `high_speed` or `full_speed` |

### `[[bus.uart]]` — optional, repeatable

| Field | Type | Description |
|-------|------|-------------|
| `bind` | string | Logical name (e.g. `logger`). Required for USE_UART_COUNT. |
| `peripheral` | string | `UART1`–`UART8` or `LPUART1` |
| `tx` | string | TX pin (`PA0` format) |
| `rx` | string | RX pin (`PA0` format), optional |
| `baud` | int | Baud rate (default: 115200) |

### `[[bus.spi]]` — optional, repeatable

| Field | Type | Description |
|-------|------|-------------|
| `bind` | string | Logical name (e.g. `flash`). |
| `peripheral` | string | `SPI1`–`SPI6` |
| `sclk` | string | Clock pin |
| `mosi` | string | MOSI pin |
| `miso` | string | MISO pin |
| `ncs` | string | Chip select pin |

### `[flash]` — optional

| Field | Type | Description |
|-------|------|-------------|
| `chip` | string | Flash chip driver: `w25qxx` |

## Regenerating Config

After editing `BoardConfig.toml`, regenerate the C header and CMake file:

```bash
# Directly
python3 scripts/generate_config.py --target <TARGET>

# Or via the build system
lua tool.lua config --target <TARGET>
```

CMake also invokes the generator automatically during configuration.
