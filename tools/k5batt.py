#!/usr/bin/env python3
"""Watch the battery ADC of a running radio (UART command 0x0529, needs ENABLE_EXTRA_UART_CMD).

Prints the raw voltage ADC and the charge current ADC twice a second. The firmware treats a
current reading above 500 as "charging over USB-C" (helper/battery.c).
"""
import argparse
import struct
import time

from k5link import K5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-140")
    ap.add_argument("--seconds", type=float, default=60)
    a = ap.parse_args()

    k5 = K5(a.port)
    print("firmware:", k5.hello().split(b"\0")[0].decode(errors="replace"))
    end = time.time() + a.seconds
    while time.time() < end:
        k5.send(0x0529, b"")
        try:
            cmd, data = k5.recv()
        except IOError as e:
            print("no reply:", e)
            time.sleep(0.5)
            continue
        if cmd == 0x052A and len(data) >= 4:
            voltage, current = struct.unpack("<HH", data[:4])
            print(f"{time.strftime('%H:%M:%S')}  voltage ADC {voltage:5d}  current ADC {current:5d}"
                  f"{'  CHARGING' if current > 500 else ''}", flush=True)
        time.sleep(0.5)


if __name__ == "__main__":
    main()
