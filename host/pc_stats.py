"""PC Stats Display - host script.

Collects live stats from this computer and streams them over USB serial to the desk
display, once a second: CPU (load per core, temperature, clock, power), memory, GPU,
fans, motherboard temperatures, drives (space, speed, temperature, wear), network,
top processes, and a health score with alerts.

    python pc_stats.py                     # CYD touchscreen dashboard (default)
    python pc_stats.py --board nano        # Arduino Nano + OLED ticker
    python pc_stats.py --port COM5         # skip auto-detection
    python pc_stats.py --sensors           # show everything detected on this PC, then exit
    python pc_stats.py --list-ports        # show serial ports and exit

On Windows you can just double-click run_dashboard.bat (or run_nano_ticker.bat).
CPU temperatures and fan speeds on Windows need LibreHardwareMonitor running with its
web server enabled (see the README). Everything else works without it.
The script waits for the board to be plugged in and reconnects if it's unplugged.
"""
import argparse
import sys
import time
import traceback


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
    del psutil  # only imported here so a missing library gives a clear message
except ImportError as e:
    pause_and_exit(
        f"Missing Python library: {e.name}\n\n"
        "Install the libraries this script needs by running:\n"
        f'    "{sys.executable}" -m pip install -r requirements.txt\n\n'
        "Then start this script again."
    )

from pcstats.collector import StatsCollector
from pcstats.health import status_text
from pcstats.lhm import DEFAULT_URL, LhmReader
from pcstats.model import CRITICAL, WARNING
from pcstats.protocol import BOARDS, format_cyd

BAUD = 115200
INTERVAL_S = 1.0  # how often to send a line

# Text found in the descriptions of common USB-serial chips (CH340, CP210x, ...)
BOARD_PORT_KEYWORDS = ("ch340", "ch341", "ch910", "cp210", "usb-serial", "usb serial",
                       "usbserial", "wch", "arduino")


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
# Startup summary and the --sensors report
# ---------------------------------------------------------------------------

def print_summary(collector):
    gpu = collector.gpu
    print(f"CPU: {collector.cpu_name or 'unknown'} ({collector.cpu_threads} threads)")
    if gpu.available:
        print(f"GPU: {gpu.name} (read via {gpu.source})")
    else:
        print("GPU: no NVIDIA GPU via NVML (other brands are read through LibreHardwareMonitor)")
    lhm = collector.lhm.read()
    if lhm is not None:
        print(f"Sensors: LibreHardwareMonitor connected ({lhm.sensor_count} sensors, "
              f"{len(lhm.fans)} fans, {len(lhm.temps)} board temps, {len(lhm.disks)} drives)")
    else:
        print("Sensors: LibreHardwareMonitor not found - CPU temperature and fan speeds will show")
        print("         as '--'. To enable them, run it with Options > Remote Web Server on")
        print(f"         (expected at {collector.lhm.url}). Run with --sensors for details.")


def sensors_report(lhm_url):
    """Everything this PC exposes, to help set things up and debug."""
    print("Reading this PC... (takes about 3 seconds)\n")
    c = StatsCollector(lhm_url)
    time.sleep(1.5)
    c.read()
    time.sleep(1.5)
    s = c.read()

    print(f"System     {s.host_name} - {s.os_name}, up {s.uptime_s // 3600}h {s.uptime_s % 3600 // 60}m")
    print(f"CPU        {s.cpu_name} - {s.cpu}% load, {s.cpu_threads} threads")
    print(f"           per core: {' '.join(str(x) for x in s.core_loads)}")
    print(f"           temp {fmt(s.cpu_temp, 'C')}, clock {fmt(s.cpu_clock_mhz, ' MHz')}, "
          f"power {fmt(s.cpu_power_w, ' W')}")
    print(f"Memory     {s.ram}% ({s.ram_used_mb / 1024:.1f} of {s.ram_total_mb / 1024:.1f} GB)")
    g = s.gpu
    print(f"GPU        {s.gpu_name or 'none'} - load {fmt(g.util, '%')}, temp {fmt(g.temp, 'C')}, "
          f"hotspot {fmt(g.hotspot, 'C')}")
    print(f"           VRAM {fmt(g.mem_used_mb, ' MB')} / {fmt(g.mem_total_mb, ' MB')}, "
          f"power {fmt(g.power_w, ' W')} / {fmt(g.power_limit_w, ' W')}, fan {fmt(g.fan, '%')}, "
          f"clocks {fmt(g.clock_mhz, '')}/{fmt(g.mem_clock_mhz, ' MHz')}")
    print(f"Fans       {', '.join(f'{f.name} {f.rpm} RPM' for f in s.fans) or '--'}")
    print(f"Board      {', '.join(f'{t.name} {t.celsius:.0f}C' for t in s.temps) or '--'}")
    print(f"Drives     {', '.join(f'{d.name} {d.used_pct:.0f}% of {d.total_gb:.0f} GB' for d in s.drives)}")
    print(f"           read {s.disk_read_kbs} KB/s, write {s.disk_write_kbs} KB/s")
    disks = [f"{d.name}: {fmt(d.temp, 'C')}, life {fmt(d.life, '%')}" for d in s.disks]
    print(f"Disk health {', '.join(disks) or '--'}")
    print(f"Network    down {s.down_kbs} KB/s, up {s.up_kbs} KB/s")
    print(f"Processes  {s.process_count} running; top: "
          + ", ".join(f"{p.name} {p.cpu:.1f}%" for p in s.processes[:4]))
    print(f"\nHealth     {s.health_score}/100 - {status_text(s.health_score)}")
    for a in s.alerts:
        tag = {CRITICAL: "CRITICAL", WARNING: "warning "}.get(a.level, "info    ")
        print(f"  {tag}  {a.text}")
    for text in s.checks_passed:
        print(f"  ok        {text}")

    reader = LhmReader(lhm_url)
    try:
        sensors = reader.sensors()
        print(f"\nLibreHardwareMonitor: {len(sensors)} sensors at {lhm_url}")
        for sen in sensors:
            if sen.type in ("Temperature", "Fan", "Power", "Clock", "Load", "Level"):
                print(f"  [{sen.hw_kind:<7}] {sen.type:<11} {sen.name:<28} {sen.value:g}")
    except Exception as e:
        print(f"\nLibreHardwareMonitor: not reachable at {lhm_url} ({e.__class__.__name__})")
        print("  Install it from https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases,")
        print("  run it as administrator, and tick Options > Remote Web Server > Run.")

    line = format_cyd(s)
    print(f"\nLine sent to the dashboard ({len(line)} bytes):\n{line}")


def fmt(v, unit):
    return "--" if v is None or v < 0 else f"{v:g}{unit}"


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def run(board, fixed_port=None, lhm_url=DEFAULT_URL):
    name, fmt_line = BOARDS[board]
    print(f"PC Stats Display -> {name}\n")
    collector = StatsCollector(lhm_url)
    print_summary(collector)
    print("\nPress Ctrl+C to stop.\n")
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
        next_t = time.monotonic()
        try:
            with ser:
                while True:
                    ser.write(fmt_line(collector.read()).encode())
                    next_t += INTERVAL_S
                    time.sleep(max(0.0, next_t - time.monotonic()))
                    if time.monotonic() - next_t > 5:  # fell far behind (e.g. sleep): resync
                        next_t = time.monotonic()
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
    p.add_argument("--sensors", action="store_true",
                   help="show everything detected on this PC and exit")
    p.add_argument("--lhm-url", default=DEFAULT_URL,
                   help=f"LibreHardwareMonitor web server data URL (default: {DEFAULT_URL})")
    p.add_argument("legacy_port", nargs="?", help=argparse.SUPPRESS)  # old style: pc_stats.py COM5
    return p.parse_args(argv)


def main():
    args = parse_args()
    if args.list_ports:
        print(ports_text())
    elif args.sensors:
        sensors_report(args.lhm_url)  # the .bat launchers pause afterwards
    else:
        run(args.board, args.port or args.legacy_port, args.lhm_url)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception:
        pause_and_exit("Something went wrong:\n\n" + traceback.format_exc()
                       + "\nPlease copy this error into an issue on GitHub.")
