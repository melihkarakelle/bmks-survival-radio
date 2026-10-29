#!/usr/bin/env python3
"""Read-only UV-K5 serial link: firmware version + EEPROM dump.

Protocol (from sq5bpf/k5prog, DualTachyon firmware app/uart.c):
  frame  = AB CD | len16 LE | XOR(payload + crc16 LE) | DC BA
  payload= cmd16 LE | datalen16 LE | data
  crc    = CRC16-XMODEM over payload (radio replies with 0xFFFF instead)
  38400 8N1, normal (powered-on) mode.

Commands used:
  0x0514 hello(timestamp)        -> 0x0515 version string
  0x051B read(addr,len,ts)       -> 0x051C addr,len,data

Nothing here writes to the radio.
"""
import argparse
import struct
import sys
import time

import serial

XOR_KEY = bytes([0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
                 0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80])
TIMESTAMP = 0x6457396A


def crc16_xmodem(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def xor(data):
    return bytes(b ^ XOR_KEY[i % 16] for i, b in enumerate(data))


def build_frame(cmd, data):
    payload = struct.pack("<HH", cmd, len(data)) + data
    body = payload + struct.pack("<H", crc16_xmodem(payload))
    return b"\xAB\xCD" + struct.pack("<H", len(payload)) + xor(body) + b"\xDC\xBA"


class K5:
    def __init__(self, port, timeout=1.0):
        self.ser = serial.Serial(port, 38400, timeout=timeout)
        self.ser.reset_input_buffer()

    def send(self, cmd, data):
        self.ser.write(build_frame(cmd, data))

    def recv(self):
        head = self.ser.read(4)
        if len(head) < 4 or head[:2] != b"\xAB\xCD":
            raise IOError(f"bad/no header: {head.hex()}")
        n = struct.unpack("<H", head[2:])[0]
        rest = self.ser.read(n + 4)
        if len(rest) != n + 4 or rest[-2:] != b"\xDC\xBA":
            raise IOError("short frame")
        payload = xor(rest[:n + 2])[:n]
        cmd, dlen = struct.unpack("<HH", payload[:4])
        return cmd, payload[4:4 + dlen]

    def hello(self):
        self.send(0x0514, struct.pack("<I", TIMESTAMP))
        cmd, data = self.recv()
        if cmd != 0x0515:
            raise IOError(f"unexpected reply 0x{cmd:04x}")
        return data

    def read(self, addr, length):
        self.send(0x051B, struct.pack("<HBBI", addr, length, 0, TIMESTAMP))
        cmd, data = self.recv()
        if cmd != 0x051C:
            raise IOError(f"unexpected reply 0x{cmd:04x}")
        raddr, rlen = struct.unpack("<HB", data[:3])
        if raddr != addr:
            raise IOError(f"addr mismatch {raddr:04x}!={addr:04x}")
        return data[4:4 + rlen]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-140")
    ap.add_argument("--dump", help="write EEPROM dump to this file")
    ap.add_argument("--size", type=lambda s: int(s, 0), default=0x2000)
    a = ap.parse_args()

    k5 = K5(a.port)
    info = k5.hello()
    ver = info[:16].split(b"\x00")[0].decode(errors="replace")
    print(f"firmware: {ver}")
    print(f"hello raw: {info.hex()}")

    if a.dump:
        out = bytearray()
        for addr in range(0, a.size, 0x80):
            out += k5.read(addr, 0x80)
            print(f"\r{addr + 0x80:#06x}/{a.size:#06x}", end="", file=sys.stderr)
        print(file=sys.stderr)
        with open(a.dump, "wb") as f:
            f.write(out)
        print(f"saved {len(out)} bytes to {a.dump}")


if __name__ == "__main__":
    main()
