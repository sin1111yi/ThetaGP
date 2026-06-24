"""
Shared CDC ACM serial I/O for ThetaGP test scripts.

Provides open_serial(), readline(), send(), check(), and summary helpers.
"""

import os
import time
import select
import json
import glob
import termios
import fcntl


def _find_port():
    """Try stable symlink first, fall back to /dev/ttyACM1."""
    candidates = glob.glob("/dev/serial/by-id/usb-ThetaGamepad*if01*")
    if candidates:
        return candidates[0]
    return "/dev/ttyACM1"


PORT = _find_port()


def open_serial(port=None, timeout=10):
    """Open CDC ACM port with raw binary mode.

    Uses O_NONBLOCK initially to avoid hanging when DCD is not asserted
    (STM32 CDC ACM quirk). Clears O_NONBLOCK after open.
    """
    if port is None:
        port = PORT
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            flags = fcntl.fcntl(fd, fcntl.F_GETFL)
            fcntl.fcntl(fd, fcntl.F_SETFL, flags & ~os.O_NONBLOCK)
            break
        except BlockingIOError:
            time.sleep(0.2)
    else:
        raise TimeoutError(f"Could not open {port} within {timeout}s")

    attrs = termios.tcgetattr(fd)
    for a in range(4):
        if a == 0:
            attrs[a] &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                          termios.ISTRIP | termios.INLCR | termios.IGNCR |
                          termios.ICRNL | termios.IXON)
        elif a == 1:
            attrs[a] &= ~termios.OPOST
        elif a == 2:
            attrs[a] = (attrs[a] & ~(termios.CSIZE | termios.PARENB |
                                     termios.CSTOPB)) | termios.CS8
        elif a == 3:
            attrs[a] &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                          termios.ISIG | termios.IEXTEN)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def readline(fd, timeout=3):
    """Read one line (up to \\n) from CDC serial. Returns str or None on timeout."""
    buf = b""
    while True:
        r, _, _ = select.select([fd], [], [], timeout)
        if not r:
            return None
        ch = os.read(fd, 1)
        if not ch:
            return None
        if ch == b"\n":
            return buf.decode("utf-8").strip()
        if ch != b"\r":
            buf += ch


def read_bytes(fd, count, timeout=5):
    """Read exactly `count` raw bytes. Returns bytes or None on timeout."""
    buf = b""
    deadline = time.time() + timeout
    while len(buf) < count and time.time() < deadline:
        remaining = count - len(buf)
        r, _, _ = select.select([fd], [], [], max(0.1, deadline - time.time()))
        if r:
            try:
                chunk = os.read(fd, min(remaining, 1024))
                if chunk:
                    buf += chunk
            except BlockingIOError:
                time.sleep(0.005)
    if len(buf) < count:
        return None
    return buf


def flush(fd):
    """Discard any pending input."""
    termios.tcflush(fd, termios.TCIOFLUSH)


class TestContext:
    """Per-test-script state: queued counter, pass/fail/skip counters."""

    def __init__(self, fd):
        self.fd = fd
        self.queued = 0
        self.passed = 0
        self.failed = 0
        self.skipped = 0

    def send(self, cmd, expect_cmd=None, **kw):
        """Send a JSON command and wait for a matching response.

        Skips async messages, garbage, and mismatched cmd fields.
        Returns parsed JSON dict or None on timeout.
        """
        req = {"cmd": cmd, "queued": self.queued, **kw}
        line = json.dumps(req, separators=(",", ":")) + "\r\n"
        print(f">>> {line.strip()}")
        os.write(self.fd, line.encode())
        self.queued += 1
        ec = expect_cmd or cmd
        while True:
            resp = readline(self.fd)
            if resp is None:
                print("!!! TIMEOUT")
                return None
            try:
                obj = json.loads(resp)
            except json.JSONDecodeError:
                print(f"[garbage] {resp}")
                continue
            if obj.get("status") == "async":
                print(f"[async] {resp}")
                continue
            if obj.get("cmd") != ec:
                print(f"[unexpected cmd={obj.get('cmd')}] {resp}")
                continue
            print(f"<<< {resp}")
            return obj

    def check(self, name, ok, detail=""):
        """Record a test result."""
        if ok is None:
            self.skipped += 1
            status = "SKIP"
        elif ok:
            self.passed += 1
            status = "PASS"
        else:
            self.failed += 1
            status = "FAIL"
        msg = f"  [{status}] {name}"
        if detail:
            msg += f" — {detail}"
        print(msg)

    def skip(self, name, reason=""):
        """Record a skipped test."""
        self.skipped += 1
        msg = f"  [SKIP] {name}"
        if reason:
            msg += f" — {reason}"
        print(msg)

    def summary(self):
        """Print test summary. Returns True if all passed."""
        total = self.passed + self.failed + self.skipped
        print(f"\n{'=' * 50}")
        print(f"  Passed:  {self.passed}/{total}")
        print(f"  Failed:  {self.failed}/{total}")
        print(f"  Skipped: {self.skipped}/{total}")
        print(f"{'=' * 50}")
        return self.failed == 0


def hex_encode(data: bytes) -> str:
    """Bytes to uppercase hex string."""
    return data.hex().upper()


def hex_decode(s: str) -> bytes:
    """Hex string to bytes."""
    return bytes.fromhex(s)


def make_pattern(offset: int, length: int) -> bytes:
    """Generate deterministic test pattern."""
    return bytes((i + offset) & 0xFF for i in range(length))
