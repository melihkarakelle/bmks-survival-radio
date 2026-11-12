#!/usr/bin/env python3
"""Flash a firmware .bin to a UV-K1 / UV-K5 V3 (PY32F071) in DFU mode.

Protocol ported 1:1 from UVTools2 (armel.github.io/uvtools2, js/flash.js):
  radio in DFU mode repeats 0x0518 (dev info: 16 bytes UID, bootloader version at data+16)
  1. wait for 5 dev-info messages 5..1000 ms apart
  2. handshake: 3 x 0x0530 with the first 4 chars of the bootloader version
  3. per 256-byte page 0x0519 (268 bytes):
       timestamp32 | page16 | page_count16 | 4 x 00 | 256 data
     answer 0x051A: data[4:6] page, data[6:8] error (0 = ok)
Framing (AB CD | len | XOR(payload + CRC16) | DC BA) is the same as k5link.py.

Usage:
  k5v3flash.py --port /dev/ttyUSB0 image.bin --dry-run
  k5v3flash.py --port /dev/ttyUSB0 image.bin --yes
"""
import argparse
import struct
import sys
import time

import serial

from k5link import build_frame, xor

MSG_NOTIFY_DEV_INFO = 0x0518
MSG_NOTIFY_BL_VER = 0x0530
MSG_PROG_FW = 0x0519
MSG_PROG_FW_RESP = 0x051A
PAGE = 256
MAX_IMAGE = 118 * 1024   # FLASH region of the V3 linker script
MIN_BL = (7, 0, 7)


class Link:
    def __init__(self, port):
        self.ser = serial.Serial(port, 38400, timeout=0.01)
        self.buf = bytearray()

    def pump(self):
        self.buf += self.ser.read(512)

    def fetch(self):
        """One decoded message (cmd, data) from the buffer, or None."""
        self.pump()
        while True:
            i = self.buf.find(b"\xab\xcd")
            if i < 0:
                del self.buf[:-1 if self.buf.endswith(b"\xab") else len(self.buf)]
                return None
            if len(self.buf) - i < 8:
                return None
            n = struct.unpack_from("<H", self.buf, i + 2)[0]
            end = i + 4 + n
            if len(self.buf) < end + 4:
                return None
            if self.buf[end + 2:end + 4] != b"\xdc\xba":
                del self.buf[:i + 2]
                continue
            msg = xor(bytes(self.buf[i + 4:end + 2]))
            del self.buf[:end + 4]
            return struct.unpack_from("<H", msg)[0], msg[4:n]

    def send(self, cmd, data):
        if len(data) % 2:
            data += b"\0"
        self.ser.write(build_frame(cmd, data))


def wait_dev_info(link, seconds=8):
    last, acc = 0.0, 0
    deadline = time.time() + seconds
    while time.time() < deadline:
        m = link.fetch()
        if not m:
            time.sleep(0.01)
            continue
        cmd, data = m
        if cmd != MSG_NOTIFY_DEV_INFO:
            continue
        now = time.time()
        dt, last = now - last, now
        acc = acc + 1 if 0.005 <= dt <= 1.0 else 0
        if acc >= 5:
            uid = data[:16]
            ver = data[16:32].split(b"\0")[0].decode(errors="replace")
            return uid, ver
    sys.exit("no device: power on the radio with PTT held (blank screen, flashlight on)")


def handshake(link, ver):
    acc = 0
    while acc < 3:
        time.sleep(0.05)
        m = link.fetch()
        if m and m[0] == MSG_NOTIFY_DEV_INFO:
            link.send(MSG_NOTIFY_BL_VER, ver[:4].encode().ljust(4, b"\0"))
            acc += 1
            time.sleep(0.05)
    time.sleep(0.2)
    while link.fetch():
        pass


def program(link, fw):
    count = (len(fw) + PAGE - 1) // PAGE
    ts = int(time.time() * 1000) & 0xFFFFFFFF
    page, retries = 0, 0
    while page < count:
        chunk = fw[page * PAGE:(page + 1) * PAGE].ljust(PAGE, b"\0")
        link.send(MSG_PROG_FW, struct.pack("<IHH", ts, page, count) + b"\0" * 4 + chunk)
        ok = False
        deadline = time.time() + 3.0
        while time.time() < deadline:
            m = link.fetch()
            if not m:
                time.sleep(0.005)
                continue
            cmd, data = m
            if cmd != MSG_PROG_FW_RESP:
                continue
            rpage, err = struct.unpack_from("<HH", data, 4)
            if rpage != page:
                continue
            if err:
                print(f"\npage {page + 1}/{count}: error {err}")
            else:
                ok = True
            break
        if ok:
            page, retries = page + 1, 0
            print(f"\r{page}/{count} pages", end="", flush=True)
        else:
            retries += 1
            if retries > 3:
                sys.exit(f"\nFAILED at page {page}. Radio stays in DFU: power-cycle with PTT held and run again.")
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--yes", action="store_true", help="actually write")
    a = ap.parse_args()

    fw = open(a.image, "rb").read()
    if not 20000 < len(fw) <= MAX_IMAGE:
        sys.exit(f"image size {len(fw)} outside 20000..{MAX_IMAGE}, wrong file?")
    sp, reset = struct.unpack("<II", fw[:8])
    if not (0x20000000 < sp <= 0x20004000) or not (reset & 1) or (reset & ~1) >= 0x08000000 + len(fw):
        sys.exit(f"bad vector table SP={sp:#x} reset={reset:#x}")
    print(f"image {len(fw)} bytes, {(len(fw) + PAGE - 1) // PAGE} pages, SP={sp:#x} reset={reset:#x}")

    link = Link(a.port)
    uid, ver = wait_dev_info(link)
    print(f"bootloader {ver}, UID {uid.hex()}")
    try:
        if tuple(int(x) for x in ver.split(".")) < MIN_BL:
            sys.exit(f"bootloader {ver} older than 7.00.07: not a supported V3 / K1")
    except ValueError:
        sys.exit(f"unexpected bootloader version {ver!r}")
    if a.dry_run:
        print("dry run: nothing written")
        return
    if not a.yes:
        sys.exit("refusing to write without --yes")

    handshake(link, ver)
    t0 = time.time()
    program(link, fw)
    print(f"done in {time.time() - t0:.1f}s. Power-cycle the radio without PTT.")


if __name__ == "__main__":
    main()
