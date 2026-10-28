"""Stats every OS provides through psutil: CPU, memory, network, drives, processes."""
import os
import platform
import socket
import time

import psutil

from .lhm import short_cpu_name
from .model import Drive, Process

ROOT_DRIVE = os.path.abspath(os.sep)

# Processes that aren't real programs
_IGNORED_PROCESSES = {"system idle process", "idle", "system", "memory compression", "registry"}


def cpu_model_name():
    """The CPU's marketing name, e.g. 'Ryzen 7 5800X'."""
    name = ""
    try:
        if os.name == "nt":
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                                 r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            name = winreg.QueryValueEx(key, "ProcessorNameString")[0]
        elif os.path.exists("/proc/cpuinfo"):
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.lower().startswith("model name"):
                        name = line.split(":", 1)[1]
                        break
    except Exception:
        pass
    return short_cpu_name(name or platform.processor())


def os_name():
    system, release = platform.system(), platform.release()
    if system == "Windows":
        try:  # Windows 11 still reports release "10" on older Pythons
            if int(platform.version().split(".")[2]) >= 22000:
                release = "11"
        except (IndexError, ValueError):
            pass
    elif system == "Darwin":
        system, release = "macOS", platform.mac_ver()[0]
    return f"{system} {release}".strip()[:16]


def list_drives(limit=4):
    """Fixed drives with their usage, e.g. [Drive('C:', 79.2, 931.5)]."""
    drives, seen = [], set()
    for p in psutil.disk_partitions(all=False):
        opts = p.opts.lower()
        if "cdrom" in opts or "removable" in opts or not p.fstype:
            continue
        if p.mountpoint.startswith(("/boot", "/snap", "/var", "/run", "/sys", "/proc", "/dev")):
            continue
        try:
            u = psutil.disk_usage(p.mountpoint)
        except (OSError, PermissionError):
            continue
        if u.total < 1 * 2**30 or p.device in seen:  # skip tiny system partitions
            continue
        seen.add(p.device)
        if os.name == "nt":
            name = p.mountpoint.rstrip("\\")[:2]                    # "C:"
        else:
            name = "/" if p.mountpoint == "/" else os.path.basename(p.mountpoint.rstrip("/"))[:6]
        drives.append(Drive(name, round(u.percent, 1), round(u.total / 2**30, 1)))
    return drives[:limit]


class ProcessTracker:
    """Top programs by CPU and memory, grouped by name (e.g. all chrome.exe together),
    with CPU as a share of the whole processor like Task Manager shows."""

    def __init__(self):
        self._procs = {}
        self.threads = psutil.cpu_count(logical=True) or 1
        self.count = -1

    def top(self, n=6):
        totals = {}
        alive = set()
        count = 0
        for p in psutil.process_iter(["name", "memory_info"]):
            count += 1
            alive.add(p.pid)
            proc = self._procs.setdefault(p.pid, p)
            try:
                cpu = proc.cpu_percent(None) / self.threads  # first call per process returns 0
                name = (p.info["name"] or "").strip()
                mem = p.info["memory_info"].rss if p.info["memory_info"] else 0
            except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                continue
            if not name or name.lower() in _IGNORED_PROCESSES:
                continue
            if name.lower().endswith(".exe"):
                name = name[:-4]
            t = totals.setdefault(name, [0.0, 0])
            t[0] += cpu
            t[1] += mem
        for pid in list(self._procs):
            if pid not in alive:
                del self._procs[pid]
        self.count = count
        ranked = sorted(totals.items(), key=lambda kv: (kv[1][0], kv[1][1]), reverse=True)
        return [Process(name[:18], round(cpu, 1), int(mem // 2**20)) for name, (cpu, mem) in ranked[:n]]


class RateCounter:
    """Turns ever-increasing byte counters into KB/s."""

    def __init__(self, read):
        self._read = read
        self._last = read()
        self._t = time.monotonic()

    def rates(self):
        now, cur = time.monotonic(), self._read()
        dt = max(now - self._t, 1e-3)
        if cur is None or self._last is None:
            self._last, self._t = cur, now
            return (-1, -1)
        out = tuple(max(0, int((c - p) / 1024 / dt)) for c, p in zip(cur, self._last))
        self._last, self._t = cur, now
        return out


def net_bytes():
    n = psutil.net_io_counters()
    return (n.bytes_recv, n.bytes_sent)


def disk_bytes():
    d = psutil.disk_io_counters()
    return (d.read_bytes, d.write_bytes) if d else None


def host_name():
    return socket.gethostname().split(".")[0][:16]

