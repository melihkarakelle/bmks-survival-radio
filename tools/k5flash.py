#!/usr/bin/env python3
"""Flash a raw firmware .bin to a UV-K5 (V1, DP32G030) in bootloader mode.

Protocol from sq5bpf/k5prog (k5_writeflash, k5_send_flash_version_message):
  radio in flash mode repeats 0x0518 (bootloader info, version at data+0x10)
  0x0530  16 bytes: version string; '*' first char is accepted by all bootloaders
  0x0519  per 0x100 block, data (0x10C bytes):
          8A 8D 9F 1D | addr_hi addr_lo | end_hi 00 | len_lo len_hi | 00 00 | 256 data
          end = total size rounded up to 0x100
  0x051A  ack: 8A 8D 9F 1D | addr_hi addr_lo | ...
Flash above 0xF000 holds the bootloader and is never written.

Usage:
  k5flash.py builds/f4hwn.bandscope.bin --dry-run
  k5flash.py builds/f4hwn.bandscope.bin --yes
"""
import argparse
import struct
import sys
import time

import serial

from k5link import K5

BLOCK = 0x100
MAX_FLASH = 0xF000
MAGIC = bytes([0x8A, 0x8D, 0x9F, 0x1D])


def block_payload(fw, addr, end):
    chunk = fw[addr:addr + BLOCK]
    hdr = MAGIC + bytes([addr >> 8, addr & 0xFF, end >> 8, 0]) + struct.pack("<H", len(chunk)) + b"\0\0"
    return hdr + chunk.ljust(BLOCK, b"\0")


def check_image(fw):
    if len(fw) < 50000:
        sys.exit(f"image too small ({len(fw)} bytes), wrong file?")
    if len(fw) > MAX_FLASH:
        sys.exit(f"image {len(fw):#x} bigger than {MAX_FLASH:#x}, would hit bootloader")
    sp, reset = struct.unpack("<II", fw[:8])
    if not (0x20000000 < sp <= 0x20004000) or not (reset & 1) or reset >= len(fw):
        sys.exit(f"bad vector table SP={sp:#x} reset={reset:#x} (packed file? use the raw .bin)")


def wait_bootloader(k5, seconds=10):
    """Wait for the bootloader announce; survives the USB cable being re-plugged."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            if not k5.ser.is_open:
                k5.ser.open()
            cmd, data = k5.recv()
        except IOError:
            try:
                k5.ser.reset_input_buffer()
            except (OSError, serial.SerialException):
                k5.ser.close()
                time.sleep(1)
            continue
        except (OSError, serial.SerialException):
            k5.ser.close()   # adapter unplugged, reopen when it is back
            time.sleep(1)
            continue
        if cmd == 0x0518:
            return data[0x10:0x20].split(b"\0")[0].decode(errors="replace")
    sys.exit("no bootloader message: power on the radio with PTT held")


def wait_ack(k5, addr, seconds=5.0):
    """True when the bootloader confirms addr, False on an error reply or timeout."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            cmd, data = k5.recv()
        except IOError:
            continue
        if cmd == 0x0518:
            continue
        if cmd == 0x051A and data[4] == addr >> 8 and data[5] == addr & 0xFF and data[6] == 0:
            return True
        print(f"\nblock {addr:#06x}: reply {cmd:#06x} {data.hex()}")
        return False
    print(f"\nblock {addr:#06x}: no ack")
    return False


def write_image(k5, fw, blocks, end, version):
    k5.send(0x0530, version.encode().ljust(16, b"\0")[:16])
    try:
        k5.recv()
    except IOError:
        pass

    for addr in blocks:
        k5.send(0x0519, block_payload(fw, addr, end))
        if not wait_ack(k5, addr):
            return False
        print(f"\r{addr + BLOCK:#06x}/{end:#06x}", end="", flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--port", default="/dev/cu.usbserial-140")
    ap.add_argument("--version", default="*.01.23")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--yes", action="store_true", help="actually write")
    ap.add_argument("--wait", type=int, default=10, help="seconds to wait for bootloader")
    a = ap.parse_args()

    fw = open(a.image, "rb").read()
    check_image(fw)
    end = (len(fw) + BLOCK - 1) & ~(BLOCK - 1)
    blocks = list(range(0, len(fw), BLOCK))
    print(f"image {len(fw)} bytes ({len(fw):#x}), {len(blocks)} blocks, end {end:#x}")

    if a.dry_run:
        for addr in (blocks[0], blocks[-1]):
            p = block_payload(fw, addr, end)
            print(f"block {addr:#06x}: payload {len(p):#x} bytes, header {p[:12].hex(' ')}")
        return
    if not a.yes:
        sys.exit("refusing to write without --yes")

    k5 = K5(a.port, timeout=0.5)
    print(f"bootloader: {wait_bootloader(k5, a.wait)}")

    # The bootloader accepts blocks only in order. A repeated or missed block
    # makes it answer with an error for address 0, so on any failure the whole
    # image is written again from block 0 (never a single block re-send).
    t0 = time.time()
    for attempt in range(1, 4):
        if write_image(k5, fw, blocks, end, a.version):
            break
        print(f"\nattempt {attempt} failed, restarting from block 0")
        time.sleep(1)
        k5.ser.reset_input_buffer()
    else:
        sys.exit("\nFAILED. Radio stays in bootloader: power it off, hold PTT, power on, run again.")
    print(f"\ndone in {time.time() - t0:.1f}s. Power-cycle the radio (without PTT).")


if __name__ == "__main__":
    main()
