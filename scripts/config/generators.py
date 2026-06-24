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

KEYPAD_DRIVE_MODE_MAP = {
    "scan_matrix": "ScanMatrix",
    "io_direct": "IODirect",
    "spi_74hc165": "SpiDriven74HC165",
}
KEYPAD_ACTIVE_MODE_MAP = {"none": "None", "low": "Low", "high": "High"}

UART_PERIPHERAL_ENUM_MAP = {f"UART{i}": f"UartInstance::Uart{i}" for i in range(1, 9)}
SPI_PERIPHERAL_ENUM_MAP = {f"SPI{i}": f"SpiInstance::Spi{i}" for i in range(1, 7)}

FLASH_CHIP_MAP = {"w25qxx": "W25QXX"}


# ── Pin lines (LED, misc) ────────────────────────────────────────────────────

def gen_pin_lines(cfg: dict) -> list[str]:
    """Generate pin macro lines for non-keypad, non-usb keys (LED etc.)."""
    lines: list[str] = []
    for key in cfg:
        if key in ("keypad", "usb", "bus", "flash", "board_info"):
            continue
        val = cfg[key]
        if isinstance(val, dict) and "pin" in val:
            lines.append(generate_pin_macro(f"{key.upper()}_PIN", val["pin"]))
            if "active_low" in val:
                lines.append(
                    f"#define {key.upper() + '_ACTIVE_LOW':<28} "
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

    mode_val = KEYPAD_DRIVE_MODE_MAP.get(dm, "")
    lines.append(f"#define {'KEYPAD_DRIVE_MODE':<28} KeypadConfig::Mode::{mode_val}")

    am = kp.get("active_mode", "none").lower()
    active_val = KEYPAD_ACTIVE_MODE_MAP.get(am, "None")
    lines.append(f"#define {'KEYPAD_ACTIVE_MODE':<28} KeypadConfig::Active::{active_val}")

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
        lines.append(f"#define {'KEYPAD_DRIVE_PIN_NUM':<28} {len(dp)}")
        lines.append(generate_pin_array_macro("KEYPAD_DRIVE_IO_LIST", dp))

    sp = kp.get("sense_pins", [])
    if sp:
        lines.append(f"#define {'KEYPAD_SENSE_PIN_NUM':<28} {len(sp)}")
        lines.append(generate_pin_array_macro("KEYPAD_SENSE_IO_LIST", sp))

    km = kp.get("key_map", {})
    if km and dp and sp:
        drive_num = len(dp)
        sense_num = len(sp)
        data = km.get("data", [])
        total = drive_num * sense_num

        lines.append("")
        lines.append("#define KEYPAD_KEY_MAP \\")

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
        lines.append(f"#define {'KEYPAD_MAX_KEY_INDEX':<28} {max_index}")
        lines.append(
            f"#define {'KEYPAD_MASK_ARRAY_SIZE':<28} {(max_index + 32) // 32}"
        )


def _gen_keypad_io_direct(kp: dict, lines: list[str]) -> None:
    dp = kp.get("direct_pins", [])
    if dp:
        lines.append(f"#define {'KEYPAD_DIRECT_PINS_NUM':<28} {len(dp)}")
        lines.append(generate_pin_array_macro("KEYPAD_DIRECT_PINS", dp))


def _gen_keypad_spi_chips(kp: dict, lines: list[str]) -> None:
    if "spi_chips" in kp:
        lines.append(f"#define {'KEYPAD_SPI_CHIPS':<28} {kp['spi_chips']}")


def _gen_button_map(kp: dict, lines: list[str]) -> None:
    bm = kp.get("button_map", {})
    if not bm:
        return
    lines.append("")
    lines.append("#define KEYPAD_BUTTON_MAP \\")
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
        pv = USB_PERIPHERAL_MAP.get(usb["hw_periph"], "USB1_OTG")
        lines.append(f"#define USBHW_IF_{pv}")
    if "speed" in usb:
        sv = USB_SPEED_MAP.get(usb["speed"], "FS")
        lines.append(f"#define USBHW_SPEED_{sv}")
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
        lines.append(f"#define {'USE_UART_' + str(i + 1):<28}")

    bind_count = sum(1 for u in uart_list if u.get("bind"))
    if bind_count == 0:
        return lines

    lines.append("")
    lines.append(f"#define USE_UART_COUNT {bind_count}")
    lines.append("")

    for i, u in enumerate(uart_list):
        if u.get("bind") and u.get("peripheral"):
            lines.append(
                f"#define {u['bind'].upper() + '_UART':<28} BUS_UART_{i + 1}"
            )

    desc_entries: list[str] = []
    for i, u in enumerate(uart_list):
        if u.get("bind") and u.get("peripheral"):
            enum_val = UART_PERIPHERAL_ENUM_MAP.get(
                u["peripheral"], f"UartInstance::Uart{i + 1}"
            )
            tx_str = generate_pin_struct(u["tx"])
            rx_str = generate_pin_struct(u.get("rx", u["tx"]))
            baud = u.get("baud", 115200)
            desc_entries.append(
                f"    {{{enum_val}, {tx_str}, {rx_str}, {baud}}}"
            )

    if desc_entries:
        lines.append("")
        lines.append(f"#define {'UART_DESC_DATA':<28} \\")
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
        lines.append(f"#define {'USE_SPI_' + str(i + 1):<28}")

    bind_count = sum(1 for f in flash_list if f.get("bind"))
    if bind_count == 0:
        return lines

    lines.append("")
    lines.append(f"#define USE_SPI_COUNT {bind_count}")
    lines.append("")

    for i, f in enumerate(flash_list):
        if f.get("bind") and f.get("peripheral"):
            lines.append(
                f"#define {f['bind'].upper() + '_SPI':<28} BUS_SPI_{i + 1}"
            )

    desc_entries: list[str] = []
    for i, f in enumerate(flash_list):
        if f.get("bind") and f.get("peripheral"):
            enum_val = SPI_PERIPHERAL_ENUM_MAP.get(
                f["peripheral"], f"SpiInstance::Spi{i + 1}"
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
        lines.append(f"#define {'SPI_DESC_DATA':<28} \\")
        for j, entry in enumerate(desc_entries):
            if j < len(desc_entries) - 1:
                lines.append(f"    {entry}, \\")
            else:
                lines.append(f"    {entry}")

    return lines


# ── Flash ────────────────────────────────────────────────────────────────────

def gen_flash_lines(flash: dict | None) -> list[str]:
    """Generate flash chip selection macro."""
    if not flash or "chip" not in flash:
        return []
    chip = flash["chip"]
    macro_suffix = FLASH_CHIP_MAP.get(chip)
    if not macro_suffix:
        raise ValueError(
            f"Unknown flash chip: {chip}. "
            f"Supported: {', '.join(sorted(FLASH_CHIP_MAP))}"
        )
    return [f"#define FLASH_CHIP_{macro_suffix}"]


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
    mcu_header = MCU_HEADER_MAP.get(mcu_series, "")

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

    if mcu_header:
        content += f"\n{mcu_header}\n\n"
    else:
        content += "\n"

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
        f'set(BOARD_MCU_SERIES "{board_info.get("mcu_series", "")}")',
        f'set(TARGET "{target_value}")',
    ]
    return "\n".join(lines)
