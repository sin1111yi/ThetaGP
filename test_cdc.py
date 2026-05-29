#!/usr/bin/env python3
"""CDC JSON protocol test for ThetaGP — no pyserial needed."""

import os, termios, tty, time, json, select

PORT = "/dev/ttyACM1"
BAUD_MAP = {115200: termios.B115200}


def open_serial(port, baud=115200):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = attrs[0] & ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK
                            | termios.ISTRIP | termios.INLCR | termios.IGNCR
                            | termios.ICRNL | termios.IXON)
    attrs[1] = attrs[1] & ~termios.OPOST
    attrs[2] = attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
    attrs[2] = attrs[2] | termios.CS8
    attrs[3] = attrs[3] & ~(termios.ECHO | termios.ECHONL | termios.ICANON
                            | termios.ISIG | termios.IEXTEN)
    attrs[4] = BAUD_MAP.get(baud, termios.B115200)
    attrs[5] = BAUD_MAP.get(baud, termios.B115200)
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def readline(fd, timeout=3):
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


queued = 0


def send_cmd(fd, cmd, **kwargs):
    global queued
    req = {"cmd": cmd, "queued": queued, **kwargs}
    line = json.dumps(req, separators=(",", ":")) + "\r\n"
    print(f">>> {line.strip()}")
    os.write(fd, line.encode("utf-8"))
    queued += 1

    while True:
        resp = readline(fd)
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
        if obj.get("cmd") != cmd:
            print(f"[unexpected cmd={obj.get('cmd')}] {resp}")
            continue
        print(f"<<< {resp}")
        return obj


def main():
    global queued
    fd = open_serial(PORT)
    time.sleep(0.3)
    queued = 0
    passed = 0
    failed = 0

    # Test 1: sys.ping
    print("\n--- Test 1: sys.ping ---")
    r = send_cmd(fd, "sys.ping")
    if r and r.get("status") == "ok":
        print("  PASS"); passed += 1
    else:
        print("  FAIL"); failed += 1

    # Test 2: sys.get_fw_version
    print("\n--- Test 2: sys.get_fw_version ---")
    r = send_cmd(fd, "sys.get_fw_version")
    if r and r.get("status") == "ok" and "version" in r:
        print(f"  PASS (version={r['version']})"); passed += 1
    else:
        print("  FAIL"); failed += 1

    # Test 3: unknown command
    print("\n--- Test 3: unknown command ---")
    r = send_cmd(fd, "test.not_implemented_yet")
    if r and r.get("status") == "error" and r.get("error_code") == 1:
        print(f"  PASS (reason={r['reason']})"); passed += 1
    else:
        print("  FAIL"); failed += 1

    # Test 4: queued sequence (1→2→3→4)
    print("\n--- Test 4: queued sequence ---")
    ok = True
    expected = 1
    queued = 0
    for i in range(3):
        r = send_cmd(fd, "sys.ping")
        if r and r.get("queued") == expected:
            expected += 1
        else:
            ok = False
            break
    if ok:
        print(f"  PASS"); passed += 1
    else:
        print(f"  FAIL"); failed += 1

    # Summary
    print(f"\n{'='*40}")
    print(f"Passed: {passed}/{passed+failed}")
    print(f"Failed: {failed}/{passed+failed}")

    os.close(fd)
    return failed == 0


if __name__ == "__main__":
    exit(0 if main() else 1)
