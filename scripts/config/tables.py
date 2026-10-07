"""
The value tables a board's declaration maps through, and the limits its
values are held to.

A table carries one entry per value the platform layer defines — a peripheral,
a timer channel, a chip. A declared value outside its table names nothing the
firmware has, so it is rejected: there is no entry to fall back on and no
constant to emit.
"""

# ── MCU ──────────────────────────────────────────────────────────────────────

MCU_HEADER_MAP = {
    "STM32H7": "stm32h7xx.h",
    "STM32F4": "stm32f4xx.h",
    "STM32F1": "stm32f1xx.h",
}

# ── USB ──────────────────────────────────────────────────────────────────────

USB_PERIPHERAL_MAP = {"USB1": "OTG1", "USB2": "OTG2", "ULPI": "ULPI"}
USB_SPEED_MAP = {"high_speed": "HS", "full_speed": "FS"}

# Reports per second each link can carry. Every speed in USB_SPEED_MAP needs an
# entry; the check below runs at import so adding a speed cannot silently drop
# the ceiling that validation and its messages are built from.
USB_SPEED_CEILING_HZ = {"high_speed": 8000, "full_speed": 1000}
if set(USB_SPEED_CEILING_HZ) != set(USB_SPEED_MAP):
    raise RuntimeError(
        "USB_SPEED_CEILING_HZ must cover exactly the speeds in USB_SPEED_MAP"
    )

# ── Keypad ───────────────────────────────────────────────────────────────────

KEYPAD_DRIVE_MODE_MAP = {
    "scan_matrix": "ScanMatrix",
    "io_direct": "IODirect",
    "spi_74hc165": "SpiDriven74HC165",
}
KEYPAD_ACTIVE_MODE_MAP = {"none": "None", "low": "Low", "high": "High"}

BUTTON_SUFFIXES = frozenset({
    "UP", "DOWN", "LEFT", "RIGHT",
    "B1", "B2", "B3", "B4",
    "L1", "R1", "L2", "R2",
    "S1", "S2", "L3", "R3",
    "A1", "A2", "A3", "A4",
    "DU", "DD", "DL", "DR",
    "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",
})

# Pins one drive or sense list may carry, chips one keypad may be built from,
# keys one matrix may sample, and buttons one matrix may name.
MAX_KEYPAD_CHIPS = 4
MAX_KEYPAD_KEYS = 64
MAX_KEYPAD_BUTTONS = 32
MAX_KEY_INDEX = 63
MAX_BUTTON_INDEX = 31
NO_KEY = 0xFF

# ── Buses ────────────────────────────────────────────────────────────────────

# The firmware's instance enums (uart_bus.h, spi_bus.h) carry exactly the
# entries in those maps: UART1–UART8 have no LPUART, SPI1–SPI6 have no SPI7.
UART_PERIPHERAL_ENUM_MAP = {f"UART{i}": f"UartInstance::Uart{i}" for i in range(1, 9)}
SPI_PERIPHERAL_ENUM_MAP = {f"SPI{i}": f"SpiInstance::Spi{i}" for i in range(1, 7)}

# ── Flash ────────────────────────────────────────────────────────────────────

FLASH_CHIP_MAP = {"w25qxx": "W25QXX"}

# "none" is in no map: it is the board stating it has no chip, which the
# generator answers with the switch alone.
VALID_FLASH_CHIPS = {"none"} | set(FLASH_CHIP_MAP)

# ── LEDs ─────────────────────────────────────────────────────────────────────

# Timer channels a strip's data line can come out of, keyed by the source a
# board declares: each entry names a TimerChannel in timer.h.
LED_TIMER_CHANNEL_MAP = {
    "TIM1_CH4": "TimerChannel::Tim1Ch4",
}

# The strip table's name: it is the binding the firmware looks the strip's
# macros up by, and its presence is what the presence switch reports.
LED_STRIP_TABLE = "rgb_strip"

# Pixels one strip may carry; the driver's encode buffer is sized from it.
MAX_LED_PIXELS = 64
