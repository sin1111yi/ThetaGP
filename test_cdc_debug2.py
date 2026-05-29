#!/usr/bin/env python3
"""Debug: test each sys command individually with longer timeout."""

import os, termios, time, select, json

PORT = "/dev/ttyACM1"

def open_serial(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    for attr in (0, 1, 2, 3):
        if attr == 0:
            attrs[attr] = attrs[attr] & ~(termios.IGNBRK|termios.BRKINT|termios.PARMRK|termios.ISTRIP|termios.INLCR|termios.IGNCR|termios.ICRNL|termios.IXON)
        elif attr == 1:
            attrs[attr] = attrs[attr] & ~termios.OPOST
        elif attr == 2:
            attrs[attr] = attrs[attr] & ~(termios.CSIZE|termios.PARENB|termios.CSTOPB) | termios.CS8
        elif attr == 3:
            attrs[attr] = attrs[attr] & ~(termios.ECHO|termios.ECHONL|termios.ICANON|termios.ISIG|termios.IEXTEN)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd

def send(fd, cmd, queued=0):
    req = {"cmd": cmd, "queued": queued}
    line = json.dumps(req, separators=(",",":")) + "\r\n"
    print(f">>> {line.strip()}")
    os.write(fd, line.encode())
    
    t0 = time.time()
    buf = b""
    while time.time() - t0 < 5:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            ch = os.read(fd, 1)
            if not ch:
                break
            if ch == b"\n":
                line = (buf + b"").decode("utf-8").strip()
                print(f"<<< {line}")
                return line
            if ch != b"\r":
                buf += ch
    print(f"!!! TIMEOUT (buffered: {buf!r})")
    return None

fd = open_serial(PORT)
time.sleep(1)

for i, cmd in enumerate(["sys.ping", "sys.get_fw_version", "test.nope"]):
    print(f"\n--- Test {i+1}: {cmd} (queued=0) ---")
    send(fd, cmd, queued=0)
    time.sleep(0.5)
    # Flush any stale input
    termios.tcflush(fd, termios.TCIOFLUSH)

os.close(fd)
