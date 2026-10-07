#!/usr/bin/env python3
# This file is a part of ThetaGP.
#
# ThetaGP is free software: you can redistribute it
# and/or modify it under the terms of the GNU General
# Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# ThetaGP is distributed in the hope that it will be
# useful, but WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A
# PARTICULAR PURPOSE. See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public
# License along with this program.
#
# If not, see <https://www.gnu.org/licenses/>.
#
# Test: the board declaration table, and the header and CMake variables it
#       produces
# Target: scripts/config/{schema,engine,output,tables,pin_utils}.py, host build
# Method: reads the boards the repository declares, plus one written here that
#         covers what they leave out (several LEDs, a three wide key matrix
#         with an empty key, two buses of each kind with one unbound, high speed
#         USB), and compares the header each produces with the text under
#         scripts/test/board_config/, line for line with trailing blanks
#         ignored. Then it breaks one field of a sound declaration at a time and
#         checks the message that names what is wrong.
# Expect: "N checks, 0 failed" and exit 0.
# Error:  exit 1 = a check did not hold (the check names it); exit 2 = the
#         config package could not be imported.
# Usage:  python3 scripts/test/test_board_config.py
#

import os
import sys
import tomllib

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO_ROOT, "scripts"))

try:
    from config import (
        BOARD_SCHEMA,
        assemble_cmake,
        assemble_header,
        emit,
        validate,
    )
except ImportError as err:
    print(f"ERROR: the config package did not import: {err}", file=sys.stderr)
    sys.exit(2)

GOLDEN_DIR = os.path.join(REPO_ROOT, "scripts", "test", "board_config")

COVERAGE_TOML = """
[board_info]
identifier = "Coverage"
name       = "Coverage"
mcu        = "STM32H743xx"
mcu_series = "STM32H7"
chip       = "STM32H743VI"

[led.gpio_only]
pin        = "PB0"
active_low = false

[led.bare]
pin = "PB1"

[led.strip2]
pin    = "PE9"
source = "TIM1_CH4"
number = 12

[keypad]
drive_mode  = "Scan_Matrix"
active_mode = "high"
drive_pins  = ["PD8", "PD9"]
sense_pins  = ["PC4", "PC5", "PC6"]
key_map = [
    [0, 1, 2],
    [3, 0xFF, 5],
]
button_map = [
    [0, "UP"], [1, "B1"], [5, "L1"],
]

[usb]
hw_periph       = "USB1"
speed           = "high_speed"
wired_report_hz = 8000

[[bus.uart]]
bind       = "logger"
peripheral = "UART1"
tx         = "PA9"
rx         = "PA10"

[[bus.uart]]
peripheral = "UART2"
tx         = "PA2"
baud       = 921600

[[bus.spi]]
bind       = "flash"
peripheral = "SPI2"
sclk       = "PB13"
mosi       = "PB15"
miso       = "PB14"
ncs        = "PB12"

[[bus.spi]]
peripheral = "SPI4"
sclk       = "PE12"
mosi       = "PE14"
miso       = "PE13"
ncs        = "PE11"

[flash]
chip = "w25qxx"
"""

SOUND_TOML = """
[board_info]
identifier = "Case"
name       = "Case"
mcu        = "STM32H743xx"
mcu_series = "STM32H7"
chip       = "STM32H743VI"

[led.run0]
pin        = "PC0"
active_low = true

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
"""

BUTTON_MAP = 'button_map = [\n    [0, "B1"],\n    [1, "B2"],\n]'

# One field of the sound declaration replaced, and the message that has to come
# back. Every rule the table holds is one row here.
BROKEN = [
    ('identifier = "Case"', 'identifier = "Ca-se!"',
     "board_info.identifier must contain only alphanumeric characters "
     "and underscores"),
    ('chip       = "STM32H743VI"\n', "", "board_info.chip is required"),
    ('mcu_series = "STM32H7"', 'mcu_series = "STM32F7"',
     "Invalid board_info.mcu_series 'STM32F7'. "
     "Valid values: STM32F1, STM32F4, STM32H7"),
    ('pin        = "PC0"\n', "", "led.run0.pin is required"),
    ('pin        = "PC0"', 'pin        = "QX9"',
     "led.run0.pin: Invalid pin format 'QX9' (expected 'PA0' format)"),
    ('[led.run0]\npin        = "PC0"\nactive_low = true\n', "[led]\n",
     "led must hold at least one LED table"),
    ('[led.run0]', '[led]\nrun0 = 3', "led.run0 must be a table"),
    ('[led.run0]', '[led.strip1]\npin = "PE14"\nsource = "TIM1_CH4"\n'
                   'number = 65\n\n[led.run0]',
     "led.strip1.number must be between 1 and 64"),
    ('[led.run0]', '[led.strip1]\npin = "PE14"\nsource = "TIM1_CH4"\n'
                   'number = true\n\n[led.run0]',
     "led.strip1.number must be an integer"),
    ('[led.run0]', '[led.strip1]\npin = "PE14"\nsource = "TIM9_CH1"\n'
                   'number = 8\n\n[led.run0]',
     "Invalid led.strip1.source 'TIM9_CH1'. Valid values: TIM1_CH4"),
    ("[keypad]", "[nokeypad]", "keypad is required"),
    ('drive_mode  = "scan_matrix"', 'drive_mode  = "matrix"',
     "Invalid keypad.drive_mode 'matrix'. "
     "Valid values: io_direct, scan_matrix, spi_74hc165"),
    ('drive_pins  = ["PD8", "PD9"]\n', "",
     "keypad.drive_pins is required for scan_matrix"),
    ('drive_pins  = ["PD8", "PD9"]', 'drive_pins  = ["PD8", "9"]',
     "keypad.drive_pins[1]: Invalid pin format '9' (expected 'PA0' format)"),
    ('drive_pins  = ["PD8", "PD9"]',
     'drive_pins  = ["PD8", "PD9", "PD10", "PD11", "PD12", "PD13", "PD14", '
     '"PD15", "PB0"]',
     "keypad.drive_pins cannot have more than 8 pins"),
    ("    [0, 1],\n    [2, 3],", "    [0, 1],",
     "keypad.key_map has 1 rows, expected 2 (drive_pins count)"),
    ("    [0, 1],\n    [2, 3],", "    [0, 1, 2],\n    [2, 3],",
     "keypad.key_map row 0 has 3 columns, expected 2"),
    ("    [0, 1],\n    [2, 3],", "    [0, 64],\n    [2, 3],",
     "keypad.key_map[0][1] must be 0-63 or 0xFF (got 64)"),
    ('drive_pins  = ["PD8", "PD9"]\nsense_pins  = ["PC4", "PC5"]',
     'drive_pins  = ["PD8", "PD9", "PD10", "PD11", "PD12", "PD13", "PD14", '
     '"PD15"]\nsense_pins  = ["PC4", "PC5", "PC6", "PC7", "PB0", "PB1", '
     '"PB2", "PB3", "PE0"]',
     "Total keys (72) cannot exceed 64 (drive=8, sense=9)"),
    ('drive_mode  = "scan_matrix"', 'drive_mode  = "io_direct"',
     "keypad.direct_pins is required for io_direct"),
    ('drive_mode  = "scan_matrix"', 'drive_mode  = "spi_74hc165"',
     "keypad.spi_chips is required for spi_74hc165"),
    (BUTTON_MAP, 'button_map = [\n    [0, "ZZ"],\n]',
     "keypad.button_map[0] 'ZZ' is not a valid button. "
     "Valid examples: B1, L1, S1, UP"),
    (BUTTON_MAP, 'button_map = [\n    [99, "B1"],\n]',
     "keypad.button_map[0] index 99 must be a number 0-31"),
    (BUTTON_MAP, 'button_map = [\n    [0, "B1"],\n    [0, "B2"],\n]',
     "keypad.button_map[1] duplicate index 0"),
    ('hw_periph = "USB2"', 'hw_periph = "USB9"',
     "Invalid usb.hw_periph 'USB9'. Valid values: ULPI, USB1, USB2"),
    ('speed     = "full_speed"', 'speed     = "super_speed"',
     "Invalid usb.speed 'super_speed'. Valid values: full_speed, high_speed"),
    ("[usb]", "[usb]\nwired_report_hz = 4000",
     "polls the interrupt endpoint at most 1000 times per second"),
    ("[usb]", "[usb]\nwired_report_hz = 333",
     "usb.wired_report_hz is 333, which does not divide 1000000"),
    ("[usb]", '[usb]\nwired_report_hz = "fast"',
     "usb.wired_report_hz must be a number (got 'fast')"),
    ("[usb]", "[usb]\nwired_report_hz = 0",
     "usb.wired_report_hz must be positive (got 0)"),
    ('peripheral = "UART1"', 'peripheral = "UART9"',
     "Invalid bus.uart[0].peripheral 'UART9'. Valid values: UART1, UART2, "
     "UART3, UART4, UART5, UART6, UART7, UART8"),
    ('tx         = "PA9"\n', "", "bus.uart[0].tx is required"),
    ('rx         = "PA10"', 'rx         = "A10"',
     "bus.uart[0].rx: Invalid pin format 'A10' (expected 'PA0' format)"),
    ('peripheral = "UART1"\ntx         = "PA9"',
     'peripheral = "UART1"\ntx         = "PA9"\nbaud = 0',
     "bus.uart[0].baud must be at least 1"),
    ('peripheral = "SPI2"', 'peripheral = "SPI7"',
     "Invalid bus.spi[0].peripheral 'SPI7'. Valid values: SPI1, SPI2, SPI3, "
     "SPI4, SPI5, SPI6"),
    ('miso       = "PB14"\n', "", "bus.spi[0].miso is required"),
    ('ncs        = "PB12"', 'ncs        = "PBx"',
     "bus.spi[0].ncs: Invalid pin number in 'PBx'"),
    ('chip = "w25qxx"', 'chip = "w99"',
     "Invalid flash.chip 'w99'. Valid values: none, w25qxx"),
    ('bind       = "flash"', 'bind       = "spiflash"',
     "flash.chip is 'w25qxx' but no bus.spi entry binds it"),
]

CHECKS: list[tuple[str, bool, str]] = []


def check(what: str, ok: bool, detail: str = "") -> None:
    CHECKS.append((what, ok, detail))


def lines_of(text: str) -> list[str]:
    """A file's lines, trailing blanks ignored."""
    return [line.rstrip() for line in text.splitlines()]


def golden(name: str) -> str:
    with open(os.path.join(GOLDEN_DIR, f"{name}.h")) as f:
        return f.read()


def board_toml(target: str) -> dict:
    path = os.path.join(REPO_ROOT, "configs", target, "BoardConfig.toml")
    with open(path, "rb") as f:
        return tomllib.load(f)


def first_difference(expected: list[str], produced: list[str]) -> str:
    for i, (want, got) in enumerate(zip(expected, produced)):
        if want != got:
            return f"line {i + 1}:\n    want {want!r}\n    got  {got!r}"
    if len(expected) != len(produced):
        return f"{len(expected)} lines expected, {len(produced)} produced"
    return ""


def check_board(target: str, cfg: dict) -> None:
    errors = validate(BOARD_SCHEMA, cfg)
    check(f"{target}: a sound declaration reports nothing", not errors,
          "; ".join(errors))
    if errors:
        return

    produced = lines_of(assemble_header(emit(BOARD_SCHEMA, cfg)))
    expected = lines_of(golden(target))
    check(f"{target}: the header is what it was", expected == produced,
          first_difference(expected, produced))


def check_broken_declarations() -> None:
    for old, new, message in BROKEN:
        if old not in SOUND_TOML:
            check("a case names a line to break", False,
                  f"{old!r} is not in the sound declaration")
            continue
        errors = validate(BOARD_SCHEMA, tomllib.loads(SOUND_TOML.replace(old, new, 1)))
        check(f"broken declaration reports: {message[:56]}",
              any(message in e for e in errors),
              "reported: " + ("; ".join(errors) if errors else "nothing"))


def check_cmake() -> None:
    expected = [
        'set(BOARD_IDENTIFIER "BoringTechH743")',
        'set(BOARD_NAME "BoringTechH743")',
        'set(BOARD_MCU "STM32H743xx")',
        'set(BOARD_MCU_SERIES "STM32H7")',
        'set(BOARD_CHIP "STM32H743VI")',
        'set(TARGET "BoringTechH743")',
    ]
    cfg = board_toml("BoringTechH743")
    produced = assemble_cmake(emit(BOARD_SCHEMA, cfg), "BoringTechH743")
    check("BoringTechH743: board_config.cmake is what it was",
          produced.splitlines() == expected, produced)


def check_reference() -> None:
    """The reference on disk is what the declaration makes of it.

    It is what a board author reads, so a field the declaration gains has to
    reach it: generating it here and comparing is what holds the two together.
    """
    from config.gen_board_doc import sections

    path = os.path.join(REPO_ROOT, "configs", "BOARD_CONFIG_REFERENCE.md")
    with open(path) as f:
        on_disk = f.read()
    check("the board reference is current",
          on_disk == "\n".join(sections()) + "\n",
          "run: python3 scripts/config/gen_board_doc.py")


def main() -> int:
    check_board("BoringTechH743", board_toml("BoringTechH743"))
    check_board("ThetaGPH7", board_toml("ThetaGPH7"))
    check_board("Coverage", tomllib.loads(COVERAGE_TOML))

    check("the declaration the cases break is sound",
          not validate(BOARD_SCHEMA, tomllib.loads(SOUND_TOML)))

    check_broken_declarations()
    check_cmake()
    check_reference()

    failed = [c for c in CHECKS if not c[1]]
    for what, _, detail in failed:
        print(f"FAIL: {what}")
        if detail:
            print(f"      {detail}")
    print(f"{len(CHECKS)} checks, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
