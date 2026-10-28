"""PC Stats Display - host script.

Collects live stats from this computer (CPU, RAM, NVIDIA GPU, network, disk,
time) and streams them over USB serial to the desk display, once a second.

    python pc_stats.py                     # CYD touchscreen dashboard (default)
    python pc_stats.py --board nano        # Arduino Nano + OLED ticker
    python pc_stats.py --port COM5         # skip auto-detection
    python pc_stats.py --list-ports        # show serial ports and exit

On Windows you can just double-click run_dashboard.bat (or run_nano_ticker.bat).
The script waits for the board to be plugged in and reconnects if it's unplugged.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time
import traceback
from dataclasses import dataclass


def pause_and_exit(msg, code=1):
    """Print a message and wait for Enter, so a double-clicked window doesn't vanish."""
    print("\n" + msg)
    try:
        input("\nPress Enter to close...")
    except (EOFError, KeyboardInterrupt):
        pass
    sys.exit(code)


try:
    import psutil
    import serial
    from serial.tools import list_ports
except ImportError as e:
    pause_and_exit(
        f"Missing Python library: {e.name}\n\n"
        "Install the libraries this script needs by running:\n"
        f'    "{sys.executable}" -m pip install psutil pyserial\n\n'
        "Then start this script again."
    )


BAUD = 115200
INTERVAL_S = 1.0  # how often to send a line

# Text found in the descriptions of common USB-serial chips (CH340, CP210x, ...)
BOARD_PORT_KEYWORDS = ("ch340", "ch341", "ch910", "cp210", "usb-serial", "usb serial",
                       "usbserial", "wch", "arduino")

ROOT_DRIVE = os.path.abspath(os.sep)
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # hide nvidia-smi's console on Windows


# ---------------------------------------------------------------------------
# Collecting stats
# ---------------------------------------------------------------------------

@dataclass
class Stats:
    cpu: int        # %
    ram: int        # %
    gpu: int        # %, -1 if unavailable
    gpu_temp: int   # deg C, -1 if unavailable
    down_kbs: int   # KB/s
    up_kbs: int     # KB/s
    disk: int       # % used on the system drive
    clock: str      # "HH:MM"
    date: str       # "Wed 28 Oct"


def find_nvidia_smi():
    """nvidia-smi isn't always on PATH on Windows, so also check where drivers install it."""
    candidates = [shutil.which("nvidia-smi")]
    if os.name == "nt":
        windir = os.environ.get("SystemRoot", r"C:\Windows")
        progfiles = os.environ.get("ProgramFiles", r"C:\Program Files")
        candidates += [os.path.join(windir, "System32", "nvidia-smi.exe"),
                       os.path.join(progfiles, "NVIDIA Corporation", "NVSMI", "nvidia-smi.exe")]
    return next((c for c in candidates if c and os.path.isfile(c)), None)


class GpuReader:
    """Usage and temperature of the first NVIDIA GPU.

    Uses NVML (NVIDIA's driver library, via the nvidia-ml-py package) when available:
    it's fast and needs nothing on PATH. Falls back to calling nvidia-smi.
    Reports -1 for both values when there's no NVIDIA GPU.
    """

    def __init__(self):
        self.name = None      # e.g. "NVIDIA GeForce RTX 2070 SUPER"
        self.source = None    # "NVML" or "nvidia-smi"
        self._nvml = self._handle = self._smi = None
        self._init_nvml() or self._init_smi()

    def _init_nvml(self):
        try:
            import pynvml
            pynvml.nvmlInit()
            handle = pynvml.nvmlDeviceGetHandleByIndex(0)
            name = pynvml.nvmlDeviceGetName(handle)
        except Exception:
            return False
        self._nvml, self._handle, self.source = pynvml, handle, "NVML"
        self.name = name.decode() if isinstance(name, bytes) else name
        return True

    def _query_smi(self, fields, timeout):
        out = subprocess.run(
            [self._smi, f"--query-gpu={fields}", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=timeout, creationflags=NO_WINDOW,
        ).stdout
        return [v.strip() for v in out.strip().splitlines()[0].split(",")]

    def _init_smi(self):
        self._smi = find_nvidia_smi()
        if not self._smi:
            return False
        try:
            self.name = self._query_smi("name", timeout=10)[0]  # first call can be slow
            self.source = "nvidia-smi"
            return True
        except Exception:
            self._smi = None
            return False

    @property
    def available(self):
        return self.source is not None

    def read(self):
        """Returns (usage %, temperature C), or (-1, -1) if unavailable."""
        try:
            if self._handle is not None:
                util = self._nvml.nvmlDeviceGetUtilizationRates(self._handle).gpu
                temp = self._nvml.nvmlDeviceGetTemperature(self._handle,
                                                           self._nvml.NVML_TEMPERATURE_GPU)
                return int(util), int(temp)
            if self._smi:
                util, temp = self._query_smi("utilization.gpu,temperature.gpu", timeout=3)
                return int(util), int(temp)
        except Exception:
            pass
        return -1, -1


class StatsCollector:
    """Takes one reading per call. Network speed is the change since the last call."""

    def __init__(self):
        self.gpu = GpuReader()
        psutil.cpu_percent()  # the first reading is always 0, so take it now
        self._net = psutil.net_io_counters()
        self._t = time.monotonic()

    def read(self, interval=INTERVAL_S):
        cpu = round(psutil.cpu_percent(interval=interval))  # blocks for `interval`

        net, now = psutil.net_io_counters(), time.monotonic()
        dt = max(now - self._t, 1e-3)
        down = max(0, int((net.bytes_recv - self._net.bytes_recv) / 1024 / dt))
        up = max(0, int((net.bytes_sent - self._net.bytes_sent) / 1024 / dt))
        self._net, self._t = net, now

        try:
            disk = round(psutil.disk_usage(ROOT_DRIVE).percent)
        except OSError:
            disk = 0

        gpu, gpu_temp = self.gpu.read()
        return Stats(
            cpu=cpu,
            ram=round(psutil.virtual_memory().percent),
            gpu=gpu,
            gpu_temp=gpu_temp,
            down_kbs=down,
            up_kbs=up,
            disk=disk,
            clock=time.strftime("%H:%M"),
            date=time.strftime("%a %d %b"),
        )


# ---------------------------------------------------------------------------
# Serial protocol: one CSV line per reading. Each board has its own format.
# ---------------------------------------------------------------------------

def format_cyd(s: Stats) -> str:
    return (f"{s.cpu},{s.ram},{s.gpu},{s.gpu_temp},{s.down_kbs},{s.up_kbs},"
            f"{s.disk},{s.clock},{s.date}\n")


def format_nano(s: Stats) -> str:
    return f"{s.cpu},{s.ram},{s.clock}\n"


BOARDS = {
    "cyd": ("ESP32 CYD dashboard", format_cyd),
    "nano": ("Arduino Nano OLED ticker", format_nano),
}


# ---------------------------------------------------------------------------
# Serial connection
# ---------------------------------------------------------------------------

def find_board_port():
    """Returns the first port that looks like a USB-serial board, or None."""
    for p in list_ports.comports():
        info = f"{p.device} {p.description} {p.manufacturer}".lower()
        if any(k in info for k in BOARD_PORT_KEYWORDS):
            return p.device
    return None


def ports_text():
    ports = list(list_ports.comports())
    if not ports:
        return "  (none found - if the board is plugged in, try another USB cable; some only charge)"
    return "\n".join(f"  {p.device:<14} {p.description}" for p in ports)


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 1
    ser.write_timeout = 2
    # Keep DTR/RTS low so opening the port doesn't reset the ESP32 or hold it in boot mode
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def describe_open_error(port, err):
    text = str(err).lower()
    if any(k in text for k in ("access is denied", "permission", "busy", "in use")):
        return (f"{port} is in use by another program. Close the Arduino IDE's Serial Monitor "
                "(or anything else using the board).")
    if any(k in text for k in ("filenotfound", "cannot find", "no such file", "could not open")):
        return f"Couldn't open {port}. Is that the right port?"
    return f"Couldn't open {port}: {err}"


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def run(board, fixed_port=None):
    name, fmt = BOARDS[board]
    print(f"PC Stats Display -> {name}")
    collector = StatsCollector()
    gpu = collector.gpu
    if gpu.available:
        print(f"GPU: {gpu.name} (read via {gpu.source})")
    else:
        print("GPU: no NVIDIA GPU detected - the GPU tile will show n/a")
    print("Press Ctrl+C to stop.\n")
    waiting_shown = False

    while True:
        port = fixed_port or find_board_port()
        if port is None:
            if not waiting_shown:
                print("Waiting for the board to be plugged in... Serial ports found:")
                print(ports_text())
                print("If your board is listed but not picked up, pass it in, e.g.  --port COM5\n")
                waiting_shown = True
            time.sleep(2)
            continue

        try:
            ser = open_port(port)
        except serial.SerialException as e:
            print(describe_open_error(port, e), "Retrying in 3s...")
            time.sleep(3)
            continue

        print(f"Connected to {port}. Sending stats every {INTERVAL_S:g}s...")
        waiting_shown = False
        try:
            with ser:
                while True:
                    ser.write(fmt(collector.read()).encode())
        except (serial.SerialException, OSError) as e:
            print(f"Lost connection to the board ({e.__class__.__name__}). Reconnecting...\n")
            time.sleep(2)


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="Stream live PC stats to the desk display over USB serial.")
    p.add_argument("--board", choices=BOARDS, default="cyd",
                   help="which display you're using (default: cyd)")
    p.add_argument("--port", help="serial port, e.g. COM5 or /dev/ttyUSB0 (default: auto-detect)")
    p.add_argument("--list-ports", action="store_true", help="list serial ports and exit")
    p.add_argument("legacy_port", nargs="?", help=argparse.SUPPRESS)  # old style: pc_stats.py COM5
    return p.parse_args(argv)


def main():
    args = parse_args()
    if args.list_ports:
        print(ports_text())
        return
    run(args.board, args.port or args.legacy_port)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception:
        pause_and_exit("Something went wrong:\n\n" + traceback.format_exc()
                       + "\nPlease copy this error into an issue on GitHub.")
