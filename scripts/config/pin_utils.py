"""
ThetaGP pin parsing and C macro generation utilities.
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


def generate_pin_macro(name: str, pin_str: str) -> str:
    """Generate a single pin #define macro."""
    port, pin = parse_pin(pin_str)
    return f"#define {name:<28} {{{port}, {pin}}}"


def generate_pin_struct(pin_str: str) -> str:
    """Generate pin struct initializer (no #define prefix)."""
    port, pin = parse_pin(pin_str)
    return f"{{{port}, {pin}}}"


def generate_pin_array_macro(macro_name: str, pins: list[dict]) -> str:
    """Generate multi-line pin array #define macro."""
    lines = [f"#define {macro_name} \\"]
    count = len(pins)
    for i, pin_entry in enumerate(pins):
        port, pin = parse_pin(pin_entry["pin"])
        line = f"    {{{port}, {pin}}}"
        if i < count - 1:
            line += ", \\"
        lines.append(line)
    return "\n".join(lines)
