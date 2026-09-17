"""
C macro generators for BoardConfig.h and board_config.cmake.

Each gen_* function returns a list[str] of output lines.
assemble_header() and generate_cmake() produce the final file content.
"""

from .pin_utils import generate_pin_macro, generate_pin_struct, generate_pin_array_macro

# ── Mapping tables ───────────────────────────────────────────────────────────

MCU_HEADER_MAP = {
    "STM32H7": '#include "stm32h7xx.h"',
    "STM32F4": '#include "stm32f4xx.h"',
    "STM32F1": '#include "stm32f1xx.h"',
}

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

KEYPAD_DRIVE_MODE_MAP = {
    "scan_matrix": "ScanMatrix",
    "io_direct": "IODirect",
    "spi_74hc165": "SpiDriven74HC165",
}
KEYPAD_ACTIVE_MODE_MAP = {"none": "None", "low": "Low", "high": "High"}

UART_PERIPHERAL_ENUM_MAP = {f"UART{i}": f"UartInstance::Uart{i}" for i in range(1, 9)}
SPI_PERIPHERAL_ENUM_MAP = {f"SPI{i}": f"SpiInstance::Spi{i}" for i in range(1, 7)}

FLASH_CHIP_MAP = {"w25qxx": "W25QXX"}


def lookup_value(mapping: dict[str, str], label: str, value,
                 what: str = "firmware value") -> str:
    """Map a declared config value onto the constant the firmware defines.

    The maps above carry one entry per value the platform layer defines
    (`UartInstance` in bus_uart.h, `SpiInstance` in bus_spi.h, the USB
    peripheral macros, the MCU headers), so a value outside a map has nothing
    behind it in the firmware: there is no default to fall back on, and a
    made-up entry would put a constant for undeclared hardware into the
    generated file.
    """
    mapped = mapping.get(value)
    if mapped is None:
        raise ValueError(
            f"{label} is '{value}', which names no {what}. "
            f"Valid values: {', '.join(sorted(mapping))}"
        )
    return mapped


def lookup_peripheral(enum_map: dict[str, str], bus: str, index: int,
                      entry: dict) -> str:
    """Map a declared peripheral onto the firmware instance it names."""
    return lookup_value(enum_map, f"bus.{bus}[{index}].peripheral",
                        entry["peripheral"], what="firmware instance")


# ── Pin lines (LED, misc) ────────────────────────────────────────────────────

def gen_pin_lines(cfg: dict) -> list[str]:
    """Generate pin macro lines for non-keypad, non-usb keys (LED etc.)."""
    lines: list[str] = []
    for key in cfg:
        if key in ("keypad", "usb", "bus", "flash", "board_info"):
            continue
        val = cfg[key]
        if isinstance(val, dict) and "pin" in val:
            lines.append(generate_pin_macro(f"BDCFG_{key.upper()}_PIN", val["pin"]))
            if "active_low" in val:
                lines.append(
                    f"#define {'BDCFG_' + key.upper() + '_ACTIVE_LOW':<28} "
                    f"{'true' if val['active_low'] else 'false'}"
                )
    return lines


# ── Keypad ───────────────────────────────────────────────────────────────────

def gen_keypad_lines(kp: dict | None) -> list[str]:
    """Generate keypad C macro lines."""
    if not kp:
        return []

    lines: list[str] = []
    dm = kp.get("drive_mode", "").lower()

    mode_val = lookup_value(KEYPAD_DRIVE_MODE_MAP, "keypad.drive_mode", dm)
    lines.append(f"#define {'BDCFG_KEYPAD_DRIVE_MODE':<28} KeypadConfig::Mode::{mode_val}")

    am = kp.get("active_mode", "none").lower()
    active_val = lookup_value(KEYPAD_ACTIVE_MODE_MAP, "keypad.active_mode", am)
    lines.append(f"#define {'BDCFG_KEYPAD_ACTIVE_MODE':<28} KeypadConfig::Active::{active_val}")

    if dm == "scan_matrix":
        _gen_keypad_scan_matrix(kp, lines)
    elif dm == "io_direct":
        _gen_keypad_io_direct(kp, lines)
    elif dm == "spi_74hc165":
        _gen_keypad_spi_chips(kp, lines)

    _gen_button_map(kp, lines)
    return lines


def _gen_keypad_scan_matrix(kp: dict, lines: list[str]) -> None:
    dp = kp.get("drive_pins", [])
    if dp:
        lines.append(f"#define {'BDCFG_KEYPAD_DRIVE_PIN_NUM':<28} {len(dp)}")
        lines.append(generate_pin_array_macro("BDCFG_KEYPAD_DRIVE_IO_LIST", dp))

    sp = kp.get("sense_pins", [])
    if sp:
        lines.append(f"#define {'BDCFG_KEYPAD_SENSE_PIN_NUM':<28} {len(sp)}")
        lines.append(generate_pin_array_macro("BDCFG_KEYPAD_SENSE_IO_LIST", sp))

    km = kp.get("key_map", {})
    if km and dp and sp:
        drive_num = len(dp)
        sense_num = len(sp)
        data = km.get("data", [])
        total = drive_num * sense_num

        lines.append("")
        lines.append("#define BDCFG_KEYPAD_KEY_MAP \\")

        max_index = 0
        # Emit as 2-D rows matching TOML layout
        for r in range(drive_num):
            row_start = r * sense_num
            row_vals = []
            for c in range(sense_num):
                val = data[row_start + c]
                if val is None:
                    val = 0xFF
                if val != 0xFF and val > max_index:
                    max_index = val
                row_vals.append(f"{val:3d}")
            row_str = ", ".join(row_vals)
            if r < drive_num - 1:
                lines.append(f"    {{{row_str}}}, \\")
            else:
                lines.append(f"    {{{row_str}}}")

        lines.append("")
        lines.append("")
        lines.append(f"#define {'BDCFG_KEYPAD_MAX_KEY_INDEX':<28} {max_index}")
        lines.append(
            f"#define {'BDCFG_KEYPAD_MASK_ARRAY_SIZE':<28} {(max_index + 32) // 32}"
        )


def _gen_keypad_io_direct(kp: dict, lines: list[str]) -> None:
    dp = kp.get("direct_pins", [])
    if dp:
        lines.append(f"#define {'BDCFG_KEYPAD_DIRECT_PINS_NUM':<28} {len(dp)}")
        lines.append(generate_pin_array_macro("BDCFG_KEYPAD_DIRECT_PINS", dp))


def _gen_keypad_spi_chips(kp: dict, lines: list[str]) -> None:
    if "spi_chips" in kp:
        lines.append(f"#define {'BDCFG_KEYPAD_SPI_CHIPS':<28} {kp['spi_chips']}")


def _gen_button_map(kp: dict, lines: list[str]) -> None:
    bm = kp.get("button_map", {})
    if not bm:
        return
    lines.append("")
    lines.append("#define BDCFG_KEYPAD_BUTTON_MAP \\")
    sorted_keys = sorted(bm.keys())
    for i, k in enumerate(sorted_keys):
        mask_name = f"GAMEPAD_MASK_{bm[k].upper()}"
        if i < len(sorted_keys) - 1:
            lines.append(f"    {{{k}, {mask_name:<20}}}, \\")
        else:
            lines.append(f"    {{{k}, {mask_name:<20}}}")
    lines.append("")


# ── USB ──────────────────────────────────────────────────────────────────────

def gen_usb_lines(usb: dict | None) -> list[str]:
    """Generate USB C macro lines."""
    if not usb:
        return []
    lines: list[str] = []
    if "hw_periph" in usb:
        pv = lookup_value(USB_PERIPHERAL_MAP, "usb.hw_periph",
                          usb["hw_periph"])
        lines.append(f"#define BDCFG_IF_{pv}")
    if "speed" in usb:
        sv = lookup_value(USB_SPEED_MAP, "usb.speed", usb["speed"])
        lines.append(f"#define BDCFG_SPEED_{sv}")
    if "wired_report_hz" in usb:
        lines.append(
            f"#define {'BDCFG_REPORT_RATE_HZ':<28} {usb['wired_report_hz']}"
        )
    return lines


# ── UART ─────────────────────────────────────────────────────────────────────

def gen_uart_lines(bus: dict | None) -> list[str]:
    """Generate UART C macro lines from bus.uart config."""
    if not bus:
        return []
    uart_list = bus.get("uart", [])
    if not uart_list:
        return []

    lines: list[str] = []

    for i in range(len(uart_list)):
        lines.append(f"#define {'BDCFG_USE_UART_' + str(i + 1):<28}")

    # Only an entry that declares both a binding and a peripheral becomes a bus
    # instance, and the descriptor table below carries exactly those, in this
    # order: the instance number — and with it BUS_UART_<n> — is a position in
    # this list, not a position in the TOML array. Each instance carries its
    # TOML index for the diagnostics below.
    instances = [
        (i, u)
        for i, u in enumerate(uart_list)
        if u.get("bind") and u.get("peripheral")
    ]
    if not instances:
        return lines

    lines.append("")
    lines.append(f"#define BDCFG_USE_UART_COUNT {len(instances)}")
    lines.append("")

    for j, (_, u) in enumerate(instances):
        lines.append(
            f"#define {'BDCFG_' + u['bind'].upper() + '_UART':<28} BUS_UART_{j + 1}"
        )

    desc_entries: list[str] = []
    for i, u in instances:
        enum_val = lookup_peripheral(UART_PERIPHERAL_ENUM_MAP, "uart", i, u)
        tx_str = generate_pin_struct(u["tx"])
        rx_str = generate_pin_struct(u.get("rx", u["tx"]))
        baud = u.get("baud", 115200)
        desc_entries.append(
            f"    {{{enum_val}, {tx_str}, {rx_str}, {baud}}}"
        )

    if desc_entries:
        lines.append("")
        lines.append(f"#define {'BDCFG_UART_DESC_DATA':<28} \\")
        for j, entry in enumerate(desc_entries):
            if j < len(desc_entries) - 1:
                lines.append(f"    {entry}, \\")
            else:
                lines.append(f"    {entry}")

    return lines


# ── SPI ──────────────────────────────────────────────────────────────────────

def gen_spi_lines(bus: dict | None) -> list[str]:
    """Generate SPI flash C macro lines from bus.spi config."""
    if not bus:
        return []
    flash_list = bus.get("spi", [])
    if not flash_list:
        return []

    lines: list[str] = []

    for i in range(len(flash_list)):
        lines.append(f"#define {'BDCFG_USE_SPI_' + str(i + 1):<28}")

    # Only an entry that declares both a binding and a peripheral becomes a bus
    # instance, and the descriptor table below carries exactly those, in this
    # order: the instance number — and with it BUS_SPI_<n> — is a position in
    # this list, not a position in the TOML array. Each instance carries its
    # TOML index for the diagnostics below.
    instances = [
        (i, f)
        for i, f in enumerate(flash_list)
        if f.get("bind") and f.get("peripheral")
    ]
    if not instances:
        return lines

    lines.append("")
    lines.append(f"#define BDCFG_USE_SPI_COUNT {len(instances)}")
    lines.append("")

    for j, (_, f) in enumerate(instances):
        lines.append(
            f"#define {'BDCFG_' + f['bind'].upper() + '_SPI':<28} BUS_SPI_{j + 1}"
        )

    desc_entries: list[str] = []
    for i, f in instances:
        enum_val = lookup_peripheral(SPI_PERIPHERAL_ENUM_MAP, "spi", i, f)
        for pin_name in ("sclk", "mosi", "miso", "ncs"):
            if pin_name not in f:
                raise ValueError(
                    f"bus.spi[{i}].{pin_name} is required"
                )
        sclk_str = generate_pin_struct(f["sclk"])
        mosi_str = generate_pin_struct(f["mosi"])
        miso_str = generate_pin_struct(f["miso"])
        ncs_str = generate_pin_struct(f["ncs"])
        bus_pins = f"{{{sclk_str}, {mosi_str}, {miso_str}}}"
        desc_entries.append(
            f"    {{{enum_val}, {bus_pins}, {ncs_str}}}"
        )

    if desc_entries:
        lines.append("")
        lines.append(f"#define {'BDCFG_SPI_DESC_DATA':<28} \\")
        for j, entry in enumerate(desc_entries):
            if j < len(desc_entries) - 1:
                lines.append(f"    {entry}, \\")
            else:
                lines.append(f"    {entry}")

    return lines


# ── Flash ────────────────────────────────────────────────────────────────────

def gen_flash_lines(flash: dict | None) -> list[str]:
    """Generate the flash enable switch and the chip selection macro.

    A board without a flash chip declares chip = "none". A board that omits the
    section is read the same way: the section is absent because there is nothing
    to declare, not because a value was forgotten.
    """
    chip = (flash or {}).get("chip", "none")
    if chip == "none":
        return ["#define BDCFG_HAS_FLASH 0"]

    macro_suffix = FLASH_CHIP_MAP.get(chip)
    if not macro_suffix:
        raise ValueError(
            f"Unknown flash chip: {chip}. "
            f"Supported: none, {', '.join(sorted(FLASH_CHIP_MAP))}"
        )
    return [
        "#define BDCFG_HAS_FLASH 1",
        f"#define BDCFG_FLASH_CHIP_{macro_suffix}",
    ]


# ── Header / CMake assembly ──────────────────────────────────────────────────

def assemble_header(
    mcu_series: str,
    board_info: dict,
    pin_lines: list[str],
    keypad_lines: list[str],
    usb_lines: list[str],
    uart_lines: list[str],
    spi_lines: list[str],
    flash_lines: list[str],
) -> str:
    """Assemble the full BoardConfig.h content."""
    mcu_header = lookup_value(MCU_HEADER_MAP, "board_info.mcu_series",
                              mcu_series)

    content = (
        "/*\n"
        " * This file is a part of ThetaGP.\n"
        " *\n"
        " * ThetaGP is free software: you can redistribute it and/or modify\n"
        " * it under the terms of the GNU General Public License as published by\n"
        " * the Free Software Foundation, either version 3 of the License, or\n"
        " * (at your option) any later version.\n"
        " *\n"
        " * ThetaGP is distributed in the hope that it will be useful,\n"
        " * but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
        " * GNU General Public License for more details.\n"
        " *\n"
        " * You should have received a copy of the GNU General Public License\n"
        " * along with this program.\n"
        " *\n"
        " * If not, see <https://www.gnu.org/licenses/>.\n"
        " */\n"
        "\n"
        "#pragma once\n"
    )

    # The series only appears through its map, so the MCU header is always
    # there: a series with no header is a config error, reported by the
    # lookup above.
    content += f"\n{mcu_header}\n\n"

    for line in pin_lines:
        content += line + "\n"

    if keypad_lines:
        content += "\n"
        for line in keypad_lines:
            content += line + "\n"

    if usb_lines:
        content += "\n"
        for line in usb_lines:
            content += line + "\n"

    if uart_lines:
        content += "\n"
        for line in uart_lines:
            content += line + "\n"

    if spi_lines:
        content += "\n"
        for line in spi_lines:
            content += line + "\n"

    if flash_lines:
        content += "\n"
        for line in flash_lines:
            content += line + "\n"

    return content


def generate_cmake(board_info: dict, target_value: str) -> str:
    """Generate board_config.cmake content."""
    lines = [
        f'set(BOARD_IDENTIFIER "{board_info.get("identifier", "")}")',
        f'set(BOARD_NAME "{board_info.get("name", "")}")',
        f'set(BOARD_MCU "{board_info.get("mcu", "")}")',
        f'set(BOARD_MCU_SERIES "{board_info["mcu_series"]}")',
        f'set(BOARD_CHIP "{board_info.get("chip", "")}")',
        f'set(TARGET "{target_value}")',
    ]
    return "\n".join(lines)
