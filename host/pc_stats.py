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
from dataclasses import dataclass, field


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
class GpuSample:
    """One reading from the GPU. -1 means unknown or not supported by the card."""
    util: int = -1            # %
    temp: int = -1            # deg C
    mem_used_mb: int = -1
    mem_total_mb: int = -1
    power_w: float = -1
    power_limit_w: float = -1
    fan: int = -1             # %


@dataclass
class Stats:
    cpu: int                  # %
    ram: int                  # %
    down_kbs: int             # KB/s
    up_kbs: int               # KB/s
    disk: int                 # % used on the system drive
    clock: str                # "HH:MM"
    date: str                 # "Wed 28 Oct"
    cpu_threads: int = -1
    ram_used_mb: int = -1
    ram_total_mb: int = -1
    gpu: GpuSample = field(default_factory=GpuSample)
    gpu_name: str = ""        # short name, e.g. "RTX 2070 SUPER"


def find_nvidia_smi():
    """nvidia-smi isn't always on PATH on Windows, so also check where drivers install it."""
    candidates = [shutil.which("nvidia-smi")]
    if os.name == "nt":
        windir = os.environ.get("SystemRoot", r"C:\Windows")
        progfiles = os.environ.get("ProgramFiles", r"C:\Program Files")
        candidates += [os.path.join(windir, "System32", "nvidia-smi.exe"),
                       os.path.join(progfiles, "NVIDIA Corporation", "NVSMI", "nvidia-smi.exe")]
    return next((c for c in candidates if c and os.path.isfile(c)), None)


def short_gpu_name(name):
    """'NVIDIA GeForce RTX 2070 SUPER' -> 'RTX 2070 SUPER' (fits on the small screen)."""
    for prefix in ("NVIDIA ", "GeForce "):
        if name.startswith(prefix):
            name = name[len(prefix):]
    return name.strip()


def _num(text):
    """Parses an nvidia-smi value; '[N/A]' or '[Not Supported]' become -1."""
    try:
        return float(text)
    except (TypeError, ValueError):
        return -1


class GpuReader:
    """Stats for the first NVIDIA GPU: load, temperature, VRAM, power and fan.

    Uses NVML (NVIDIA's driver library, via the nvidia-ml-py package) when available:
    it's fast and needs nothing on PATH. Falls back to calling nvidia-smi.
    Every value is -1 when there's no NVIDIA GPU, or the card doesn't report it.
    """

    SMI_FIELDS = ("utilization.gpu,temperature.gpu,memory.used,memory.total,"
                  "power.draw,power.limit,fan.speed")

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

    @property
    def short_name(self):
        return short_gpu_name(self.name) if self.name else ""

    def _nvml_value(self, fn, *args):
        """Calls an NVML function, returning None if this card doesn't support it."""
        try:
            return getattr(self._nvml, fn)(self._handle, *args)
        except Exception:
            return None

    def read(self):
        """Returns a GpuSample. Fields the card doesn't report stay at -1."""
        g = GpuSample()
        try:
            if self._handle is not None:
                util = self._nvml_value("nvmlDeviceGetUtilizationRates")
                temp = self._nvml_value("nvmlDeviceGetTemperature", self._nvml.NVML_TEMPERATURE_GPU)
                mem = self._nvml_value("nvmlDeviceGetMemoryInfo")
                power = self._nvml_value("nvmlDeviceGetPowerUsage")           # milliwatts
                limit = self._nvml_value("nvmlDeviceGetEnforcedPowerLimit")   # milliwatts
                fan = self._nvml_value("nvmlDeviceGetFanSpeed")
                if util is not None:
                    g.util = int(util.gpu)
                if temp is not None:
                    g.temp = int(temp)
                if mem is not None:
                    g.mem_used_mb = int(mem.used // 2**20)
                    g.mem_total_mb = int(mem.total // 2**20)
                if power is not None:
                    g.power_w = round(power / 1000, 1)
                if limit is not None:
                    g.power_limit_w = round(limit / 1000, 1)
                if fan is not None:
                    g.fan = int(fan)
            elif self._smi:
                v = [_num(x) for x in self._query_smi(self.SMI_FIELDS, timeout=3)]
                g = GpuSample(util=int(v[0]), temp=int(v[1]), mem_used_mb=int(v[2]),
                              mem_total_mb=int(v[3]), power_w=v[4], power_limit_w=v[5],
                              fan=int(v[6]))
        except Exception:
            return GpuSample()
        return g


class StatsCollector:
    """Takes one reading per call. Network speed is the change since the last call."""

    def __init__(self):
        self.gpu = GpuReader()
        self.cpu_threads = psutil.cpu_count(logical=True) or -1
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

        mem = psutil.virtual_memory()
        return Stats(
            cpu=cpu,
            ram=round(mem.percent),
            down_kbs=down,
            up_kbs=up,
            disk=disk,
            clock=time.strftime("%H:%M"),
            date=time.strftime("%a %d %b"),
            cpu_threads=self.cpu_threads,
            ram_used_mb=int((mem.total - mem.available) // 2**20),
            ram_total_mb=int(mem.total // 2**20),
            gpu=self.gpu.read(),
            gpu_name=self.gpu.short_name,
        )


# ---------------------------------------------------------------------------
# Serial protocol. Each board has its own line format.
# ---------------------------------------------------------------------------

def _clean(text):
    """Text values can't contain the protocol's separators."""
    return str(text).replace(",", " ").replace("=", " ").replace("\n", " ").strip()


def _fmt_num(v):
    return str(int(v)) if float(v).is_integer() else f"{v:.1f}"


def format_cyd(s: Stats) -> str:
    """key=value pairs; the firmware ignores keys it doesn't know and treats missing ones
    as unknown, so either side can be updated without breaking the other."""
    g = s.gpu
    fields = [
        ("cpu", s.cpu), ("cpun", s.cpu_threads),
        ("ram", s.ram), ("ramu", s.ram_used_mb), ("ramt", s.ram_total_mb),
        ("gpu", g.util), ("gput", g.temp), ("vram", g.mem_used_mb), ("vramt", g.mem_total_mb),
        ("gpw", g.power_w), ("gpwl", g.power_limit_w), ("fan", g.fan),
        ("dn", s.down_kbs), ("up", s.up_kbs), ("disk", s.disk),
    ]
    parts = [f"{k}={_fmt_num(v)}" for k, v in fields if v is not None and v >= 0]
    parts += [f"t={_clean(s.clock)}", f"d={_clean(s.date)}"]
    if s.gpu_name:
        parts.append(f"gn={_clean(s.gpu_name)[:23]}")
    return ",".join(parts) + "\n"


def format_nano(s: Stats) -> str:
    """Positional: cpu,ram,HH:MM"""
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
        sample = gpu.read()
        extras = [name for name, v in (("VRAM", sample.mem_total_mb), ("power", sample.power_w),
                                      ("fan", sample.fan)) if v >= 0]
        if extras:
            print(f"     also reporting: {', '.join(extras)}")
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
