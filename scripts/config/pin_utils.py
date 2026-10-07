"""
ThetaGP pin parsing and C spelling.

A pin is written in a board's declaration as a string — PA9, PE14 — and is
spelled in the header as the port and pin constants a driver takes.
"""

# Port map for pin strings (PA0 → Port::PortA, Pin::Pin0)
PORT_MAP: dict[str, str] = {c: f"Port::Port{c}" for c in "ABCDEFGHI"}


def validate_pin_format(pin_str: str) -> str | None:
    """Validate pin string format (e.g. PA0). Returns error or None."""
    if not isinstance(pin_str, str):
        return f"Pin must be a string, got {type(pin_str).__name__}"
    if len(pin_str) < 3 or pin_str[0] != "P":
        return f"Invalid pin format '{pin_str}' (expected 'PA0' format)"
    port_char = pin_str[1]
    if port_char not in "ABCDEFGHI":
        return f"Invalid port '{port_char}' (expected A-I)"
    if not pin_str[2:].isdigit():
        return f"Invalid pin number in '{pin_str}'"
    return None


def parse_pin(pin_str: str) -> tuple[str, str]:
    """Parse pin string to (port, pin) tuple. Raises ValueError on invalid."""
    err = validate_pin_format(pin_str)
    if err:
        raise ValueError(err)
    port_char = pin_str[1]
    pin_num = pin_str[2:]
    return PORT_MAP[port_char], f"Pin::Pin{pin_num}"


def pin_of(entry) -> str | None:
    """The pin a list entry names, written as a string or as a table."""
    if isinstance(entry, dict):
        return entry.get("pin")
    return entry


def pin_struct(pin_str: str) -> str:
    """A pin as the port and pin constants it names."""
    port, pin = parse_pin(pin_str)
    return f"{{{port}, {pin}}}"


def pin_array_lines(macro_name: str, pins: list) -> str:
    """A list of pins as one macro spanning its entries."""
    lines = [f"#define {macro_name} \\"]
    last = len(pins) - 1
    for i, entry in enumerate(pins):
        line = f"    {pin_struct(pin_of(entry))}"
        if i < last:
            line += ", \\"
        lines.append(line)
    return "\n".join(lines)
