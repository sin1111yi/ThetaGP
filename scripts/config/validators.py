"""
BoardConfig.toml validators.

All public entry point: validate_config(cfg) → list[str] of errors (empty = ok).
"""

from .pin_utils import validate_pin_format

# ── Valid value sets ─────────────────────────────────────────────────────────

VALID_MCU_SERIES = {"STM32H7", "STM32F4", "STM32F1"}

VALID_DRIVE_MODES = {"scan_matrix", "io_direct", "spi_74hc165"}
VALID_ACTIVE_MODES = {"none", "low", "high"}

BUTTON_SUFFIX_LIST = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "B1", "B2", "B3", "B4",
    "L1", "R1", "L2", "R2",
    "S1", "S2", "L3", "R3",
    "A1", "A2", "A3", "A4",
    "DU", "DD", "DL", "DR",
    "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",
}

VALID_USB_PERIPHS = {"USB1", "USB2", "ULPI"}
VALID_USB_SPEEDS = {"high_speed", "full_speed"}

VALID_UART_PERIPHERALS = {
    "UART1", "UART2", "UART3", "UART4",
    "UART5", "UART6", "UART7", "UART8", "LPUART1",
}


# ── Public API ───────────────────────────────────────────────────────────────

def validate_config(cfg: dict) -> list[str]:
    """Validate a BoardConfig dict. Returns list of error strings (empty = ok)."""
    errors: list[str] = []

    _validate_board_info(cfg.get("board_info", {}), errors)
    _validate_keypad(cfg.get("keypad", {}), errors)
    _validate_usb(cfg.get("usb", {}), errors)
    _validate_bus(cfg.get("bus", {}), errors)

    return errors


# ── board_info ───────────────────────────────────────────────────────────────

def _validate_board_info(bi: dict, errors: list[str]) -> None:
    required = ["identifier", "name", "mcu", "mcu_series"]
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


# ── Bus (UART) ───────────────────────────────────────────────────────────────

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
