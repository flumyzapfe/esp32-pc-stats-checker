"""Desk Stats Ticker - PC side.
Sends CPU %, RAM % and the time to the Nano once a second.

Setup:  pip install psutil pyserial
Run:    python nano_stats.py       (auto-detects the Nano)
        python nano_stats.py COM5  (or give the port yourself)
"""
import sys
import time

import psutil
import serial
from serial.tools import list_ports


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    ports = list(list_ports.comports())
    keywords = ("ch340", "ch341", "arduino", "usb-serial", "usb serial", "usbserial")
    for p in ports:
        info = f"{p.device} {p.description} {p.manufacturer}".lower()
        if any(k in info for k in keywords):
            return p.device
    print("Couldn't find the Nano automatically. Ports I can see:")
    for p in ports:
        print(f"  {p.device}  -  {p.description}")
    sys.exit("Run again with the port, e.g.  python nano_stats.py COM5")


def main():
    port = find_port()
    print(f"Connecting to {port}...")
    with serial.Serial(port, 115200, timeout=1) as ser:
        time.sleep(2)            # the Nano resets when the port opens
        psutil.cpu_percent()     # first reading is always 0, so prime it
        print("Sending stats. Press Ctrl+C to stop.")
        while True:
            cpu = round(psutil.cpu_percent(interval=1))  # also acts as the 1s delay
            ram = round(psutil.virtual_memory().percent)
            clock = time.strftime("%H:%M")
            ser.write(f"{cpu},{ram},{clock}\n".encode())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
