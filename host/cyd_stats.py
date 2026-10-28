"""PC Stats Dashboard - PC side, for the ESP32 Cheap Yellow Display.
Sends CPU, RAM, GPU (NVIDIA), network, disk and the time once a second.

Setup:  pip install -r requirements.txt
Run:    python cyd_stats.py            (auto-detects the board, or just double-click this file)
        python cyd_stats.py COM5       (or give the port yourself)

If the board isn't plugged in yet, or gets unplugged, the script waits and reconnects.
"""
import os
import shutil
import subprocess
import sys
import time
import traceback


def pause_and_exit(msg, code=1):
    """Show a message and wait for Enter, so the window doesn't vanish when double-clicked."""
    print()
    print(msg)
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
ROOT_DRIVE = os.path.abspath(os.sep)
HAS_NVIDIA = shutil.which("nvidia-smi") is not None
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # stops a console popping up on Windows
KEYWORDS = ("ch340", "ch341", "ch910", "cp210", "usb-serial", "usb serial", "usbserial", "wch")


def find_port():
    """Returns the board's port name, or None if it isn't plugged in."""
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in list_ports.comports():
        info = f"{p.device} {p.description} {p.manufacturer}".lower()
        if any(k in info for k in KEYWORDS):
            return p.device
    return None


def list_ports_text():
    ports = list(list_ports.comports())
    if not ports:
        return "  (no serial ports found - is the USB cable a data cable, not charge-only?)"
    return "\n".join(f"  {p.device}  -  {p.description}" for p in ports)


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUD
    ser.timeout = 1
    ser.write_timeout = 2
    ser.dtr = False   # stops the ESP32 resetting or getting stuck in boot mode
    ser.rts = False
    ser.open()
    return ser


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


def stream(ser):
    """Sends one line of stats per second until the connection drops."""
    psutil.cpu_percent()  # first reading is always 0, so prime it
    last_net = psutil.net_io_counters()
    last_t = time.time()
    while True:
        cpu = round(psutil.cpu_percent(interval=1))  # also acts as the 1s delay

        net = psutil.net_io_counters()
        now = time.time()
        dt = max(now - last_t, 0.001)
        down = max(0, int((net.bytes_recv - last_net.bytes_recv) / 1024 / dt))
        up = max(0, int((net.bytes_sent - last_net.bytes_sent) / 1024 / dt))
        last_net, last_t = net, now

        ram = round(psutil.virtual_memory().percent)
        try:
            disk = round(psutil.disk_usage(ROOT_DRIVE).percent)
        except OSError:
            disk = 0
        gpu, gpu_temp = gpu_stats()
        clock = time.strftime("%H:%M")
        date = time.strftime("%a %d %b")

        line = f"{cpu},{ram},{gpu},{gpu_temp},{down},{up},{disk},{clock},{date}\n"
        ser.write(line.encode())


def main():
    print("PC Stats Dashboard - press Ctrl+C to stop.\n")
    waiting_shown = False
    while True:
        port = find_port()
        if port is None:
            if not waiting_shown:
                print("Waiting for the board to be plugged in... Serial ports I can see:")
                print(list_ports_text())
                print("If your board is listed but not picked up, run:  python cyd_stats.py COMx\n")
                waiting_shown = True
            time.sleep(2)
            continue

        try:
            ser = open_port(port)
        except serial.SerialException as e:
            text = str(e).lower()
            if "access is denied" in text or "permission" in text or "busy" in text:
                print(f"{port} is in use by another program. Close the Arduino IDE's Serial Monitor "
                      "(or anything else using the board). Retrying in 3s...")
            elif "filenotfound" in text or "cannot find" in text or "could not open" in text:
                print(f"Couldn't open {port}. Is that the right port? Retrying in 3s...")
            else:
                print(f"Couldn't open {port}: {e}\nRetrying in 3s...")
            time.sleep(3)
            continue

        print(f"Connected to {port}. Sending stats...")
        waiting_shown = False
        try:
            with ser:
                stream(ser)
        except (serial.SerialException, OSError) as e:
            print(f"Lost connection to the board ({e.__class__.__name__}). Reconnecting...\n")
            time.sleep(2)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception:
        pause_and_exit("Something went wrong:\n\n" + traceback.format_exc()
                       + "\nCopy this error and send it over for help.")
