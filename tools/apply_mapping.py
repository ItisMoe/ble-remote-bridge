#!/usr/bin/env python3
"""Send a file of bridge console commands over the serial port, one line at a time.

    pip install pyserial
    python3 tools/apply_mapping.py <port> examples/one-screen-simple.txt

Close the Arduino Serial Monitor first (only one program can use the port).
Blank lines and lines starting with '#' are skipped. The bridge's answer to each line is
printed, and the exit code is 1 if the bridge reported an error for any line.

EXPERIMENTAL: written and tested against a simulated serial device, not yet on real hardware.
Opening the port can restart some ESP32 boards, so the script waits 2.5 s before sending.
"""
import argparse
import sys
import time

ERROR_MARKERS = ("Unknown command", "Bad ", "Table is full", "Not in the table", "Use ", "That is an arrow",
                 "already the")


def read_commands(path):
    with open(path, encoding="utf-8") as f:
        return [ln.strip() for ln in f if ln.strip() and not ln.lstrip().startswith("#")]


def main():
    ap = argparse.ArgumentParser(description="Send a mapping file to the BLE Remote Bridge over serial.")
    ap.add_argument("port", help="serial port, e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("file", help="text file with one console command per line")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--delay", type=float, default=0.5, help="seconds to wait for the answer after each line")
    ap.add_argument("--dry-run", action="store_true", help="only print the commands, do not open the port")
    args = ap.parse_args()

    commands = read_commands(args.file)
    if not commands:
        sys.exit("No commands found in " + args.file)
    if args.dry_run:
        for c in commands:
            print(c)
        return 0

    try:
        import serial
    except ImportError:
        sys.exit("pyserial is required:  pip install pyserial")

    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = args.port, args.baud, 0.1
    ser.dtr = False  # do not trigger a reset through the control lines where it can be avoided
    ser.rts = False
    ser.open()
    time.sleep(2.5)
    ser.reset_input_buffer()

    errors = 0
    for command in commands:
        print("> " + command)
        ser.write((command + "\n").encode("ascii"))
        ser.flush()
        deadline = time.time() + args.delay
        raw = b""
        while time.time() < deadline:
            raw += ser.read(256)
        for line in raw.decode("utf-8", errors="replace").splitlines():
            if line.strip():
                print("  " + line)
                if any(marker in line for marker in ERROR_MARKERS):
                    errors += 1
    ser.close()
    print(f"\n{len(commands)} command(s) sent, {errors} error message(s) from the bridge.")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
