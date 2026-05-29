#!/usr/bin/env python3
"""Debug CDC: try to send and receive bytes."""

import os, termios, time, select, json

PORT = "/dev/ttyACM1"

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY)
attrs = termios.tcgetattr(fd)
attrs[0] = attrs[0] & ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK
                        | termios.ISTRIP | termios.INLCR | termios.IGNCR
                        | termios.ICRNL | termios.IXON)
attrs[1] = attrs[1] & ~termios.OPOST
attrs[2] = attrs[2] & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)
attrs[2] = attrs[2] | termios.CS8
attrs[3] = attrs[3] & ~(termios.ECHO | termios.ECHONL | termios.ICANON
                        | termios.ISIG | termios.IEXTEN)
attrs[4] = termios.B115200
attrs[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, attrs)
termios.tcflush(fd, termios.TCIOFLUSH)

time.sleep(2)  # wait for device init

print(f"Opened {PORT}")
print(f"Baud rate after open: {termios.tcgetattr(fd)[4]}")

# Send PING
msg = '{"cmd":"sys.ping","queued":0}\r\n'
os.write(fd, msg.encode())
print(f">>> {msg.strip()}")

# Read for 5 seconds
t0 = time.time()
buf = b""
while time.time() - t0 < 5:
    r, _, _ = select.select([fd], [], [], 0.1)
    if r:
        chunk = os.read(fd, 256)
        if chunk:
            buf += chunk
            print(f"[read {len(chunk)} bytes] {chunk!r}")
    else:
        elapsed = int(time.time() - t0)
        if elapsed > 0 and elapsed % 1 == 0:
            print(f"  waiting... ({elapsed}s)")

if buf:
    print(f"\nTotal received: {buf!r}")
else:
    print("\nNo data received in 5 seconds")

os.close(fd)
