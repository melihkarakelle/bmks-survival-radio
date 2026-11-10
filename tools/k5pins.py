#!/usr/bin/env python3
"""Watch the radio's pins and ADC channels for changes (UART 0x0601, diag builds only).

Build the firmware with SURVIVAL_DIAG=1 (tools/build-f4hwn.sh), then run this and plug / unplug
the charger: every pin or ADC channel that changes is printed with the time.

Each reply holds GPIOA/B/C as configured, then the free pins (PA0-2, PA15, PB0-5, PB12-13,
PC6-7) read floating, with pull-up and with pull-down, then all 16 ADC channels.
"""
import argparse
import struct
import time

from k5link import K5, xor


def recv_synced(k5, timeout=1.0):
    """Next AB CD frame; skips the screenshot frames (AA 55 ...) the radio may interleave."""
    ser, end, buf = k5.ser, time.time() + timeout, b""
    while time.time() < end:
        buf += ser.read(ser.in_waiting or 1)
        i = buf.find(b"\xAB\xCD")
        if i < 0 or len(buf) < i + 4:
            buf = buf[-1:] if i < 0 else buf
            continue
        n = struct.unpack("<H", buf[i + 2:i + 4])[0]
        while len(buf) < i + 4 + n + 4 and time.time() < end:
            buf += ser.read(i + 4 + n + 4 - len(buf))
        frame = buf[i + 4:i + 4 + n + 4]
        if len(frame) == n + 4 and frame[-2:] == b"\xDC\xBA":
            payload = xor(frame[:n + 2])[:n]
            cmd, dlen = struct.unpack("<HH", payload[:4])
            return cmd, payload[4:4 + dlen]
        buf = buf[i + 2:]
    raise IOError("no frame")

MODES = ("as-is", "float", "pullup", "pulldn")
PORTS = "ABC"
ADC_MIN_CHANGE = 30   # ADC counts; smaller moves are noise


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbserial-140")
    ap.add_argument("--seconds", type=float, default=180)
    a = ap.parse_args()

    k5 = K5(a.port)
    k5.send(0x0514, struct.pack("<I", 0x6457396A))
    try:
        print("firmware:", recv_synced(k5)[1].split(b"\0")[0].decode(errors="replace"), flush=True)
    except IOError:
        print("firmware: (no hello reply)", flush=True)
    prev = None
    end = time.time() + a.seconds
    while time.time() < end:
        k5.send(0x0601, b"")
        try:
            cmd, data = recv_synced(k5)
        except IOError as e:
            print("no reply:", e, flush=True)
            time.sleep(0.5)
            continue
        if cmd != 0x0602 or len(data) < 48 + 32:
            continue
        ports = struct.unpack("<12I", data[:48])
        adc = struct.unpack("<16H", data[48:80])
        t = time.strftime("%H:%M:%S")
        if prev is None:
            for m, name in enumerate(MODES):
                print(f"{t} {name:7s}", " ".join(f"P{PORTS[p]}={ports[m * 3 + p]:04x}" for p in range(3)), flush=True)
            print(f"{t} adc    ", " ".join(f"{c}:{v}" for c, v in enumerate(adc)), flush=True)
        else:
            pports, padc = prev
            for i, (old, new) in enumerate(zip(pports, ports)):
                diff = old ^ new
                if diff:
                    bits = [f"P{PORTS[i % 3]}{b}={'1' if new >> b & 1 else '0'}" for b in range(16) if diff >> b & 1]
                    print(f"{t} {MODES[i // 3]:7s} " + " ".join(bits), flush=True)
            moved = [f"CH{c} {o}->{n}" for c, (o, n) in enumerate(zip(padc, adc))
                     if abs(n - o) >= (ADC_MIN_CHANGE if c < 10 else 300)]   # CH10+ internal, noisy
            if moved:
                print(f"{t} adc     " + "  ".join(moved), flush=True)
        prev = (ports, adc)
        time.sleep(0.4)


if __name__ == "__main__":
    main()
