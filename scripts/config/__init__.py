"""
ThetaGP board config library — TOML validation and C macro generation.
"""

from .pin_utils import (
    PORT_MAP,
    validate_pin_format,
    parse_pin,
    generate_pin_macro,
    generate_pin_struct,
    generate_pin_array_macro,
)
from .validators import validate_config
from .generators import (
    MCU_HEADER_MAP,
    gen_pin_lines,
    gen_keypad_lines,
    gen_usb_lines,
    gen_uart_lines,
    gen_spi_lines,
    gen_flash_lines,
    gen_display_lines,
    assemble_header,
    generate_cmake,
)

__all__ = [
    "PORT_MAP",
    "validate_pin_format",
    "parse_pin",
    "generate_pin_macro",
    "generate_pin_struct",
    "generate_pin_array_macro",
    "validate_config",
    "MCU_HEADER_MAP",
    "gen_pin_lines",
    "gen_keypad_lines",
    "gen_usb_lines",
    "gen_uart_lines",
    "gen_spi_lines",
    "gen_flash_lines",
    "gen_display_lines",
    "assemble_header",
    "generate_cmake",
]
