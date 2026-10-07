"""
The board's own declaration: every field a BoardConfig.toml carries, the
values it is accepted under, and what it becomes.

One row per field. A row names the field, the kind that holds its rules, and
the macro or cmake variable it is written to — so a field's name, its accepted
values and its output are read from one place, by both walks in engine.py.

The tables stand in the order the header lists them, and the rows in the order
a table's lines are written.
"""

from .engine import (
    ABSENT,
    ARRAY,
    MAP,
    PRESENT,
    Bool,
    Custom,
    Enum,
    Flag,
    Group,
    Int,
    Pin,
    PinArray,
    Presence,
    Table,
    Text,
    Word,
)
from .output import (
    bus_lines,
    check_button_map,
    check_flash,
    check_key_map,
    check_report_rate,
    lines_button_map,
    lines_flash,
    lines_key_map,
)
from .tables import (
    KEYPAD_ACTIVE_MODE_MAP,
    KEYPAD_DRIVE_MODE_MAP,
    LED_STRIP_TABLE,
    LED_TIMER_CHANNEL_MAP,
    MAX_KEYPAD_CHIPS,
    MCU_HEADER_MAP,
    SPI_PERIPHERAL_ENUM_MAP,
    UART_PERIPHERAL_ENUM_MAP,
    USB_PERIPHERAL_MAP,
    USB_SPEED_MAP,
    VALID_FLASH_CHIPS,
)

# Pixels one strip may carry; the driver's encode buffer is sized from it.
MAX_LED_PIXELS = 64

# The baud a UART is brought up at when a board does not state one.
DEFAULT_BAUD = 115200

BOARD_SCHEMA: list[Table] = [
    Table(("board_info",), items=[
        Word("identifier", required=True, cmake="BOARD_IDENTIFIER"),
        Text("name", required=True, cmake="BOARD_NAME"),
        Text("mcu", required=True, cmake="BOARD_MCU"),
        Enum("mcu_series", MCU_HEADER_MAP, required=True, include=True,
             cmake="BOARD_MCU_SERIES"),
        Text("chip", required=True, cmake="BOARD_CHIP"),
    ]),

    # Each table under led is one LED, and its name is the binding the firmware
    # looks the macros up by. A table that names a source is a strip; one
    # without is a single LED on its own pin.
    Table(("led",), kind=MAP, what="LED",
          preamble=[Presence("BDCFG_HAS_RGB_STRIP", LED_STRIP_TABLE)],
          items=[
              Group(("source", PRESENT), [
                  Pin("pin", required=True, macro="BDCFG_LED_{NAME}_PIN"),
                  Enum("source", LED_TIMER_CHANNEL_MAP, required=True,
                       macro="BDCFG_LED_{NAME}_SOURCE"),
                  Int("number", 1, MAX_LED_PIXELS, required=True,
                      macro="BDCFG_LED_{NAME}_NUMBER"),
              ]),
              Group(("source", ABSENT), [
                  Pin("pin", required=True, macro="BDCFG_LED_{NAME}_PIN"),
                  Bool("active_low", macro="BDCFG_LED_{NAME}_ACTIVE_LOW"),
              ]),
          ]),

    Table(("keypad",), required=True, items=[
        Enum("drive_mode", KEYPAD_DRIVE_MODE_MAP, required=True, lower=True,
             prefix="KeypadConfig::Mode::", macro="BDCFG_KEYPAD_DRIVE_MODE"),
        Enum("active_mode", KEYPAD_ACTIVE_MODE_MAP, lower=True,
             prefix="KeypadConfig::Active::", default="none",
             macro="BDCFG_KEYPAD_ACTIVE_MODE"),

        Group(("drive_mode", "scan_matrix"), [
            PinArray("drive_pins", "BDCFG_KEYPAD_DRIVE_PIN_NUM",
                     "BDCFG_KEYPAD_DRIVE_IO_LIST", required=True),
            PinArray("sense_pins", "BDCFG_KEYPAD_SENSE_PIN_NUM",
                     "BDCFG_KEYPAD_SENSE_IO_LIST", required=True),
            Custom("key_map", checks=check_key_map, lines=lines_key_map),
        ], note="for scan_matrix"),

        Group(("drive_mode", "io_direct"), [
            PinArray("direct_pins", "BDCFG_KEYPAD_DIRECT_PINS_NUM",
                     "BDCFG_KEYPAD_DIRECT_PINS", required=True),
        ], note="for io_direct"),

        Group(("drive_mode", "spi_74hc165"), [
            Int("spi_chips", 1, MAX_KEYPAD_CHIPS, required=True,
                macro="BDCFG_KEYPAD_SPI_CHIPS"),
        ], note="for spi_74hc165"),

        Custom("button_map", checks=check_button_map, lines=lines_button_map),
    ]),

    Table(("usb",), items=[
        Flag("hw_periph", USB_PERIPHERAL_MAP, prefix="BDCFG_IF_"),
        Flag("speed", USB_SPEED_MAP, prefix="BDCFG_SPEED_"),
        Custom("wired_report_hz", checks=check_report_rate,
               macro="BDCFG_REPORT_RATE_HZ"),
    ]),

    Table(("bus", "uart"), kind=ARRAY, items=[
        Text("bind"),
        Enum("peripheral", UART_PERIPHERAL_ENUM_MAP, required=True),
        Pin("tx", required=True),
        Pin("rx"),
        Int("baud", 1),
    ], lines=bus_lines(
        "UART",
        descriptor=["peripheral", "tx", "rx", "baud"],
        defaults={"rx": "@tx", "baud": DEFAULT_BAUD},
    )),

    Table(("bus", "spi"), kind=ARRAY, items=[
        Text("bind"),
        Enum("peripheral", SPI_PERIPHERAL_ENUM_MAP, required=True),
        Pin("sclk", required=True),
        Pin("mosi", required=True),
        Pin("miso", required=True),
        Pin("ncs", required=True),
    ], lines=bus_lines(
        "SPI",
        descriptor=["peripheral", ["sclk", "mosi", "miso"], "ncs"],
        defaults={},
    )),

    # A board that declares no chip has none. `none` is the switch's own value
    # rather than a chip, so a board states it, or says nothing at all.
    Table(("flash",), reports=check_flash, lines=lines_flash, items=[
        Enum("chip", VALID_FLASH_CHIPS),
    ]),
]
