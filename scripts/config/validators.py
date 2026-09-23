"""
BoardConfig.toml validators.

All public entry point: validate_config(cfg) → list[str] of errors (empty = ok).
"""

from .generators import (
    FLASH_CHIP_MAP,
    KEYPAD_ACTIVE_MODE_MAP,
    KEYPAD_DRIVE_MODE_MAP,
    MCU_HEADER_MAP,
    SPI_PERIPHERAL_ENUM_MAP,
    UART_PERIPHERAL_ENUM_MAP,
    USB_PERIPHERAL_MAP,
    USB_SPEED_CEILING_HZ,
    USB_SPEED_MAP,
)
from .pin_utils import validate_pin_format

# ── Valid value sets ─────────────────────────────────────────────────────────
#
# The sets that mirror a generator map are read off that map, so one table
# decides both "what may be written" and "what gets emitted": no value passes
# validation and then fails to map, and no value the generator can emit is
# unreachable from a config file. BUTTON_SUFFIX_LIST below has no generator map
# and is written by hand.

VALID_MCU_SERIES = set(MCU_HEADER_MAP)

VALID_DRIVE_MODES = set(KEYPAD_DRIVE_MODE_MAP)
VALID_ACTIVE_MODES = set(KEYPAD_ACTIVE_MODE_MAP)

BUTTON_SUFFIX_LIST = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "B1", "B2", "B3", "B4",
    "L1", "R1", "L2", "R2",
    "S1", "S2", "L3", "R3",
    "A1", "A2", "A3", "A4",
    "DU", "DD", "DL", "DR",
    "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",
}

VALID_USB_PERIPHS = set(USB_PERIPHERAL_MAP)
VALID_USB_SPEEDS = set(USB_SPEED_MAP)

# Reports per second each link speed can poll the interrupt endpoint at: one
# transaction per 1 ms frame on full speed, one per 125 us microframe on high
# speed.
USB_REPORT_RATE_CEILING_HZ = USB_SPEED_CEILING_HZ  # the generator's table, not a second copy

# "none" is in no map: it is the board stating it has no chip, which the
# generator answers with the switch alone.
VALID_FLASH_CHIPS = {"none"} | set(FLASH_CHIP_MAP)

# The firmware's instance enums (uart_bus.h, spi_bus.h) carry exactly the
# entries in those maps: UART1–UART8 have no LPUART, SPI1–SPI6 have no SPI7.
VALID_UART_PERIPHERALS = set(UART_PERIPHERAL_ENUM_MAP)
VALID_SPI_PERIPHERALS = set(SPI_PERIPHERAL_ENUM_MAP)


# ── Public API ───────────────────────────────────────────────────────────────

def validate_config(cfg: dict) -> list[str]:
    """Validate a BoardConfig dict. Returns list of error strings (empty = ok)."""
    errors: list[str] = []

    _validate_board_info(cfg.get("board_info", {}), errors)
    _validate_keypad(cfg.get("keypad", {}), errors)
    _validate_usb(cfg.get("usb", {}), errors)
    _validate_bus(cfg.get("bus", {}), errors)
    _validate_flash(cfg.get("flash"), cfg.get("bus", {}), errors)

    return errors


# ── board_info ───────────────────────────────────────────────────────────────

def _validate_board_info(bi: dict, errors: list[str]) -> None:
    required = ["identifier", "name", "mcu", "mcu_series", "chip"]
    for field in required:
        if field not in bi:
            errors.append(f"board_info.{field} is required")

    if "mcu_series" in bi and bi["mcu_series"] not in VALID_MCU_SERIES:
        errors.append(
            f"Unsupported mcu_series '{bi['mcu_series']}'. "
            f"Valid values: {', '.join(sorted(VALID_MCU_SERIES))}"
        )

    if "identifier" in bi and not bi["identifier"].replace("_", "").isalnum():
        errors.append(
            "board_info.identifier must contain only alphanumeric characters "
            "and underscores"
        )


# ── keypad ───────────────────────────────────────────────────────────────────

def _validate_keypad(kp: dict, errors: list[str]) -> None:
    if not kp:
        errors.append("keypad is required")
        return

    if "drive_mode" not in kp:
        errors.append("keypad.drive_mode is required")
        return

    dm = kp["drive_mode"].lower()
    if dm not in VALID_DRIVE_MODES:
        errors.append(
            f"Invalid drive_mode '{kp['drive_mode']}'. "
            f"Valid values: {', '.join(sorted(VALID_DRIVE_MODES))}"
        )
        return

    if dm == "scan_matrix":
        _validate_scan_matrix(kp, errors)
    elif dm == "io_direct":
        _validate_io_direct(kp, errors)
    elif dm == "spi_74hc165":
        _validate_spi_chips(kp, errors)

    if "active_mode" in kp:
        am = kp["active_mode"].lower()
        if am not in VALID_ACTIVE_MODES:
            errors.append(
                f"Invalid active_mode '{kp['active_mode']}'. "
                f"Valid values: {', '.join(sorted(VALID_ACTIVE_MODES))}"
            )

    if "button_map" in kp:
        _validate_button_map(kp, errors)


def _validate_pin_array(pins, array_name: str, errors: list[str],
                        prefix: str) -> None:
    if not isinstance(pins, list):
        errors.append(f"{prefix}{array_name} must be an array")
        return
    if len(pins) == 0:
        errors.append(f"{prefix}{array_name} must have at least 1 pin")
        return
    if len(pins) > 8:
        errors.append(f"{prefix}{array_name} cannot have more than 8 pins")
        return
    for i, p in enumerate(pins):
        if isinstance(p, str):
            err = validate_pin_format(p)
            if err:
                errors.append(f"{prefix}{array_name}[{i}]: {err}")
        elif isinstance(p, dict) and "pin" in p:
            err = validate_pin_format(p["pin"])
            if err:
                errors.append(f"{prefix}{array_name}[{i}]: {err}")
        else:
            errors.append(
                f"{prefix}{array_name}[{i}] must be a pin string "
                "or table (e.g. 'PA0')"
            )


def _validate_key_map(km, dp_len: int, sp_len: int, errors: list[str]) -> None:
    total_keys = dp_len * sp_len
    if total_keys > 64:
        errors.append(
            f"Total keys ({total_keys}) cannot exceed 64 "
            f"(drive={dp_len}, sense={sp_len})"
        )

    if isinstance(km, list):
        # 2-D array: [[0,1],[2,3]]
        if len(km) != dp_len:
            errors.append(
                f"key_map has {len(km)} rows, expected {dp_len} (drive_pins count)"
            )
        for r, row in enumerate(km):
            if not isinstance(row, list):
                errors.append(f"key_map row {r} must be an array")
                continue
            if len(row) != sp_len:
                errors.append(
                    f"key_map row {r} has {len(row)} columns, expected {sp_len}"
                )
            for c, val in enumerate(row):
                if val is not None and val != 0xFF:
                    if not isinstance(val, int) or val < 0 or val > 63:
                        errors.append(
                            f"key_map[{r}][{c}] must be 0-63 or 0xFF (got {val})"
                        )
    else:
        # Legacy flat dict: {columns, data}
        columns = km.get("columns", 0)
        data = km.get("data", [])
        if columns != sp_len:
            errors.append(
                f"key_map.columns ({columns}) must match sense_pins count ({sp_len})"
            )
        if len(data) != total_keys:
            errors.append(
                f"key_map.data length ({len(data)}) must be "
                f"drive*columns ({total_keys})"
            )
        for idx, val in enumerate(data):
            if val is not None and val != 0xFF:
                if not isinstance(val, int) or val < 0 or val > 63:
                    errors.append(
                        f"key_map.data[{idx}] must be 0-63 or 0xFF (got {val})"
                    )


def _validate_scan_matrix(kp: dict, errors: list[str]) -> None:
    dp = kp.get("drive_pins")
    sp = kp.get("sense_pins")
    if not dp:
        errors.append("keypad.drive_pins is required for scan_matrix")
    else:
        _validate_pin_array(dp, "drive_pins", errors, "keypad.")
    if not sp:
        errors.append("keypad.sense_pins is required for scan_matrix")
    else:
        _validate_pin_array(sp, "sense_pins", errors, "keypad.")

    if dp and sp and isinstance(dp, list) and isinstance(sp, list):
        km = kp.get("key_map")
        if km:
            _validate_key_map(km, len(dp), len(sp), errors)


def _validate_io_direct(kp: dict, errors: list[str]) -> None:
    dp = kp.get("direct_pins")
    if not dp:
        errors.append("keypad.direct_pins is required for io_direct")
    else:
        _validate_pin_array(dp, "direct_pins", errors, "keypad.")


def _validate_spi_chips(kp: dict, errors: list[str]) -> None:
    v = kp.get("spi_chips")
    if v is None:
        errors.append("keypad.spi_chips is required for spi_74hc165")
    elif not isinstance(v, int) or v < 1 or v > 4:
        errors.append("keypad.spi_chips must be between 1 and 4")


def _validate_button_map(kp: dict, errors: list[str]) -> None:
    bm = kp.get("button_map", {})
    if isinstance(bm, list):
        if not bm:
            errors.append("keypad.button_map must have at least 1 entry")
            return
        if len(bm) > 32:
            errors.append("keypad.button_map cannot exceed 32 entries")
            return
        seen: set[int] = set()
        for i, entry in enumerate(bm):
            if not isinstance(entry, list) or len(entry) < 2:
                errors.append(f"button_map[{i}] must be [index, button_name]")
                continue
            k, v = entry[0], entry[1]
            if not isinstance(k, int) or k < 0 or k > 31:
                errors.append(f"button_map[{i}] index {k} must be a number 0-31")
            if k in seen:
                errors.append(f"button_map[{i}] duplicate index {k}")
            seen.add(k)
            if not isinstance(v, str):
                errors.append(
                    f"button_map[{i}] button name must be a string, "
                    f"got {type(v).__name__}"
                )
            elif v.upper() not in BUTTON_SUFFIX_LIST:
                errors.append(
                    f"button_map[{i}] '{v}' is not a valid button. "
                    "Valid examples: B1, L1, S1, UP"
                )
    elif isinstance(bm, dict):
        if not bm:
            errors.append("keypad.button_map must have at least 1 entry")
            return
        if len(bm) > 32:
            errors.append("keypad.button_map cannot exceed 32 entries")
            return
        for k, v in bm.items():
            if not isinstance(k, int) or k < 0 or k > 31:
                errors.append(f"button_map key {k} must be a number 0-31")
            if not isinstance(v, str):
                errors.append(
                    f"button_map[{k}] must be a string, got {type(v).__name__}"
                )
            elif v.upper() not in BUTTON_SUFFIX_LIST:
                errors.append(
                    f"button_map[{k}] '{v}' is not a valid button. "
                    "Valid examples: B1, L1, S1, UP"
                )
    else:
        errors.append("keypad.button_map must be an array or table")


# ── USB ──────────────────────────────────────────────────────────────────────

def _validate_usb(usb: dict, errors: list[str]) -> None:
    if not usb:
        return
    if "hw_periph" in usb and usb["hw_periph"] not in VALID_USB_PERIPHS:
        errors.append(
            f"Invalid usb.hw_periph '{usb['hw_periph']}'. "
            f"Valid values: {', '.join(sorted(VALID_USB_PERIPHS))}"
        )
    if "speed" in usb and usb["speed"] not in VALID_USB_SPEEDS:
        errors.append(
            f"Invalid usb.speed '{usb['speed']}'. "
            f"Valid values: {', '.join(sorted(VALID_USB_SPEEDS))}"
        )
    if "wired_report_hz" in usb:
        _validate_wired_report_hz(usb, errors)


def _validate_wired_report_hz(usb: dict, errors: list[str]) -> None:
    """The optional report rate may not ask for more than the link polls.

    A board that omits the key builds with the firmware default of 1000.
    """
    rate = usb["wired_report_hz"]
    if not isinstance(rate, int) or isinstance(rate, bool):
        errors.append(f"usb.wired_report_hz must be a number (got {rate!r})")
        return
    if rate <= 0:
        errors.append(f"usb.wired_report_hz must be positive (got {rate})")
        return

    speed = usb.get("speed")
    if isinstance(speed, str):
        ceiling = USB_REPORT_RATE_CEILING_HZ.get(speed)
        if ceiling is None:
            errors.append(
                f"usb.speed '{speed}' has no report-rate ceiling in the "
                f"generator's table, so the rate cannot be checked against it"
            )
        elif rate > ceiling:
            limits = ", ".join(
                f"{name} polls at most {limit} times per second"
                for name, limit in sorted(USB_REPORT_RATE_CEILING_HZ.items())
            )
            errors.append(
                f"usb.wired_report_hz is {rate} but usb.speed '{speed}' polls "
                f"the interrupt endpoint at most {ceiling} times per second, so "
                f"the key must not exceed {ceiling}: {limits}"
            )
    if 1000000 % rate != 0:
        errors.append(
            f"usb.wired_report_hz is {rate}, which does not divide 1000000. "
            f"The task period is a whole number of microseconds "
            f"(1000000/{rate} truncated to {1000000 // rate} us), so the tick "
            f"cannot land on exactly {rate} times per second."
        )


# ── Bus (UART, SPI) ──────────────────────────────────────────────────────────

def _validate_bus(bus: dict, errors: list[str]) -> None:
    uart_list = bus.get("uart", [])
    for i, u in enumerate(uart_list):
        if "peripheral" not in u:
            errors.append(f"bus.uart[{i}].peripheral is required")
        elif u["peripheral"] not in VALID_UART_PERIPHERALS:
            errors.append(
                f"bus.uart[{i}].peripheral '{u['peripheral']}' is invalid. "
                f"Valid values: {', '.join(sorted(VALID_UART_PERIPHERALS))}"
            )
        if "tx" not in u:
            errors.append(f"bus.uart[{i}].tx pin is required")
        else:
            err = validate_pin_format(u["tx"])
            if err:
                errors.append(f"bus.uart[{i}].tx: {err}")
        if "rx" in u:
            err = validate_pin_format(u["rx"])
            if err:
                errors.append(f"bus.uart[{i}].rx: {err}")
        if "baud" in u and (not isinstance(u["baud"], int) or u["baud"] <= 0):
            errors.append(f"bus.uart[{i}].baud must be a positive number")

    spi_list = bus.get("spi", [])
    for i, s in enumerate(spi_list):
        if "peripheral" not in s:
            errors.append(f"bus.spi[{i}].peripheral is required")
        elif s["peripheral"] not in VALID_SPI_PERIPHERALS:
            errors.append(
                f"bus.spi[{i}].peripheral '{s['peripheral']}' is invalid. "
                f"Valid values: {', '.join(sorted(VALID_SPI_PERIPHERALS))}"
            )
        # Every SPI line carries its own pin: the driver passes all four to the
        # descriptor table, so none of them has a pin it could borrow.
        for pin_name in ("sclk", "mosi", "miso", "ncs"):
            if pin_name not in s:
                errors.append(f"bus.spi[{i}].{pin_name} pin is required")
            else:
                err = validate_pin_format(s[pin_name])
                if err:
                    errors.append(f"bus.spi[{i}].{pin_name}: {err}")


# ── Flash ────────────────────────────────────────────────────────────────────

def _validate_flash(flash: dict | None, bus: dict, errors: list[str]) -> None:
    """A declared flash chip needs an SPI bus to sit on."""
    chip = (flash or {}).get("chip", "none")

    if chip not in VALID_FLASH_CHIPS:
        errors.append(
            f"flash.chip '{chip}' is invalid. "
            f"Valid values: {', '.join(sorted(VALID_FLASH_CHIPS))}"
        )
        return

    if chip == "none":
        return

    if not any(s.get("bind") == "flash" for s in bus.get("spi", [])):
        errors.append(
            f"flash.chip is '{chip}' but no bus.spi entry binds it. "
            f"Add a [[bus.spi]] entry with bind = \"flash\", or declare "
            f"chip = \"none\" on a board without a flash chip"
        )
