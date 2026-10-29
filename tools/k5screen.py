#!/usr/bin/env python3
"""Capture the UV-K5 LCD over the programming cable (F4HWN screenshot feature).

The radio streams the 128x64 screen while it receives 0x55 keepalive bytes:
  AA 55 02 len_hi len_lo | blocks (index + 8 bytes) | 0A
The 1024 byte frame is 64 rows x 128 pixels, row-major, LSB first.

Usage: k5screen.py out.png [--seconds 3] [--scale 4]
"""
import argparse
import time

import serial
from PIL import Image

KEEPALIVE = b"\x55\xAA\x00\x00"


def read_frames(ser, frame, seconds, on_frame=None):
    buf = bytearray()
    deadline = time.time() + seconds
    next_ping = 0
    updates = 0
    while time.time() < deadline:
        if time.time() >= next_ping:
            ser.write(KEEPALIVE)
            next_ping = time.time() + 0.3
        buf += ser.read(ser.in_waiting or 1)
        while True:
            start = buf.find(b"\xAA\x55\x02")
            if start < 0 or len(buf) < start + 5:
                break
            n = (buf[start + 3] << 8) | buf[start + 4]
            end = start + 5 + n
            if len(buf) < end + 1:
                break
            payload = buf[start + 5:end]
            for i in range(0, n - 8, 9):
                block = payload[i]
                if block < 128:
                    frame[block * 8:block * 8 + 8] = payload[i + 1:i + 9]
            updates += 1
            del buf[:end + 1]
            if on_frame:
                on_frame(frame)
    return updates


def to_image(frame, scale):
    img = Image.new("RGB", (128, 64), (170, 190, 150))
    ink = (20, 30, 20)
    for y in range(64):
        for x in range(128):
            idx = y * 128 + x
            if frame[idx // 8] >> (idx % 8) & 1:
                img.putpixel((x, y), ink)
    return img.resize((128 * scale, 64 * scale), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--port", default="/dev/cu.usbserial-140")
    ap.add_argument("--seconds", type=float, default=3)
    ap.add_argument("--scale", type=int, default=4)
    ap.add_argument("--sequence", help="also save every distinct screen to this folder")
    a = ap.parse_args()

    frame = bytearray(1024)
    saved = []

    def save_distinct(f):
        if a.sequence and (not saved or saved[-1] != bytes(f)):
            saved.append(bytes(f))
            to_image(f, 2).save(f"{a.sequence}/{len(saved):03d}_{time.strftime('%H%M%S')}.png")

    with serial.Serial(a.port, 38400, timeout=0.1) as ser:
        updates = read_frames(ser, frame, a.seconds, save_distinct)
    to_image(frame, a.scale).save(a.out)
    print(f"{updates} updates, {len(saved)} distinct screens, saved {a.out}")


if __name__ == "__main__":
    main()
