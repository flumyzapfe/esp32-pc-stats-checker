"""PC Stats Dashboard - PC side, for the ESP32 Cheap Yellow Display.
Sends CPU, RAM, GPU (NVIDIA), network, disk and the time once a second.

Setup:  pip install psutil pyserial
Run:    python cyd_stats.py            (auto-detects the board)
        python cyd_stats.py COM5       (or give the port yourself)
"""
import os
import shutil
import subprocess
import sys
import time

import psutil
import serial
from serial.tools import list_ports

ROOT_DRIVE = os.path.abspath(os.sep)
HAS_NVIDIA = shutil.which("nvidia-smi") is not None
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # stops a console popping up on Windows


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    ports = list(list_ports.comports())
    keywords = ("ch340", "ch341", "ch910", "cp210", "usb-serial", "usb serial", "usbserial", "wch")
    for p in ports:
        info = f"{p.device} {p.description} {p.manufacturer}".lower()
        if any(k in info for k in keywords):
            return p.device
    print("Couldn't find the board automatically. Ports I can see:")
    for p in ports:
        print(f"  {p.device}  -  {p.description}")
    sys.exit("Run again with the port, e.g.  python cyd_stats.py COM5")


def gpu_stats():
    """Returns (usage %, temp C), or (-1, -1) if there's no NVIDIA GPU."""
    if not HAS_NVIDIA:
        return -1, -1
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=2, creationflags=NO_WINDOW,
        ).stdout
        util, temp = out.strip().splitlines()[0].split(",")
        return int(util), int(temp)
    except Exception:
        return -1, -1


def main():
    port = find_port()
    print(f"Connecting to {port}...")

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 1
    ser.dtr = False   # stops the ESP32 resetting or getting stuck in boot mode
    ser.rts = False
    ser.open()

    psutil.cpu_percent()  # first reading is always 0, so prime it
    last_net = psutil.net_io_counters()
    last_t = time.time()
    print("Sending stats. Press Ctrl+C to stop.")

    with ser:
        while True:
            cpu = round(psutil.cpu_percent(interval=1))  # also acts as the 1s delay

            net = psutil.net_io_counters()
            now = time.time()
            dt = max(now - last_t, 0.001)
            down = int((net.bytes_recv - last_net.bytes_recv) / 1024 / dt)
            up = int((net.bytes_sent - last_net.bytes_sent) / 1024 / dt)
            last_net, last_t = net, now

            ram = round(psutil.virtual_memory().percent)
            disk = round(psutil.disk_usage(ROOT_DRIVE).percent)
            gpu, gpu_temp = gpu_stats()
            clock = time.strftime("%H:%M")
            date = time.strftime("%a %d %b")

            line = f"{cpu},{ram},{gpu},{gpu_temp},{down},{up},{disk},{clock},{date}\n"
            ser.write(line.encode())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
    except serial.SerialException as e:
        sys.exit(f"Lost connection to the board: {e}")
