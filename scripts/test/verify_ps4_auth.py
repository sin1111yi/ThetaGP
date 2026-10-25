#!/usr/bin/env python3
"""Exercises the PS4 authentication channel against a connected device.

Writes a challenge through feature report 0xF0 in five pages, reads the answer
back through 0xF1 in nineteen chunks, and checks both signatures it carries
against the public key inside it. Run it with the device in PS4 mode and with
read-write access to its hidraw node:

    sudo chmod 666 /dev/hidraw0
    python3 scripts/test/verify_ps4_auth.py [--device /dev/hidraw0]
"""

import argparse
import fcntl
import os
import struct
import sys
import time
import zlib

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

CHALLENGE_REPORT = 0xF0
ANSWER_REPORT = 0xF1
STATE_REPORT = 0xF2
RESET_REPORT = 0xF3

FRAME = 63
PAGE = 56
LAST_PAGE = 32
CHUNKS = 19
ANSWER_SIZE = 1064

IOC_READ = 2
IOC_WRITE = 1


def feature_request(kind: int, size: int) -> int:
    return ((IOC_READ | IOC_WRITE) << 30) | (size << 16) | (ord("H") << 8) | kind


def set_feature(fd: int, report: int, payload: bytes) -> None:
    buffer = bytearray([report]) + bytearray(payload)
    fcntl.ioctl(fd, feature_request(0x06, len(buffer)), buffer)


def get_feature(fd: int, report: int, size: int) -> bytes:
    buffer = bytearray([report]) + bytearray(size - 1)
    fcntl.ioctl(fd, feature_request(0x07, len(buffer)), buffer)
    return bytes(buffer[1:])


def build_page(nonce: int, page: int, data: bytes) -> bytes:
    payload = bytearray(FRAME)
    payload[0] = nonce
    payload[1] = page
    payload[2] = 0
    payload[3 : 3 + len(data)] = data
    crc = zlib.crc32(bytes([CHALLENGE_REPORT]) + bytes(payload[: FRAME - 4]))
    struct.pack_into("<I", payload, FRAME - 4, crc)
    return bytes(payload)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="/dev/hidraw0")
    args = parser.parse_args()

    challenge = bytes((i * 7 + 3) & 0xFF for i in range(256))
    nonce = 0x5A

    fd = os.open(args.device, os.O_RDWR)
    try:
        for page in range(5):
            start = page * PAGE
            length = LAST_PAGE if page == 4 else PAGE
            set_feature(fd, CHALLENGE_REPORT, build_page(nonce, page, challenge[start : start + length]))

        state = None
        for _ in range(50):
            payload = get_feature(fd, STATE_REPORT, 16)
            state = payload[1]
            if state == 0:
                break
            time.sleep(0.05)
        if state != 0:
            print(f"the device is still signing (state {state})")
            return 1

        answer = bytearray()
        for chunk in range(CHUNKS):
            payload = get_feature(fd, ANSWER_REPORT, 64)
            if payload[1] != chunk:
                print(f"chunk {chunk} arrived labelled {payload[1]}")
                return 1
            answer += payload[3 : 3 + PAGE]

        reset = get_feature(fd, RESET_REPORT, 8)
        print(f"reset answered {len(reset)} bytes")
    finally:
        os.close(fd)

    signature = bytes(answer[0:256])
    serial = bytes(answer[256:272])
    modulus = bytes(answer[272:528])
    exponent = bytes(answer[528:784])
    certificate = bytes(answer[784:1040])

    public = rsa.RSAPublicNumbers(
        int.from_bytes(exponent, "big"), int.from_bytes(modulus, "big")
    ).public_key()
    pss = padding.PSS(mgf=padding.MGF1(hashes.SHA256()), salt_length=padding.PSS.AUTO)

    public.verify(signature, challenge, pss, hashes.SHA256())
    print("signature over the challenge: valid")

    public.verify(certificate, serial + modulus + exponent, pss, hashes.SHA256())
    print("certificate over serial and public key: valid")

    if any(answer[1040:1064]):
        print("the trailing padding is not zero")
        return 1
    print(f"serial {serial.hex()}, key {public.key_size} bits")
    return 0


if __name__ == "__main__":
    sys.exit(main())
