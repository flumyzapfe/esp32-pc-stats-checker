"""Reads hardware sensors from LibreHardwareMonitor.

Windows doesn't give normal programs access to CPU temperatures or fan speeds; they
need a kernel driver. LibreHardwareMonitor (free, open source) has one, and its
"Remote Web Server" option serves every sensor as JSON at
http://localhost:8085/data.json. This module reads that and picks out the useful
parts: CPU temperature/power/clock, fans, motherboard temperatures, drive health and
GPU details for any brand.

The JSON is a tree: computer -> hardware (has "HardwareId") -> sensor group
("Temperatures", "Fans", ...) -> sensors (have "SensorId", "Type", "RawValue").
Older LibreHardwareMonitor versions don't send Type/RawValue/HardwareId, so those
are worked out from the group name, the formatted value and the hardware icon.
"""
import json
import re
import time
import urllib.request
from dataclasses import dataclass, field
from typing import List, Optional

from .model import DiskHealth, Fan, GpuSample, TempSensor

DEFAULT_URL = "http://127.0.0.1:8085/data.json"


@dataclass
class Sensor:
    hw_kind: str    # cpu, gpu, board, storage, ram, other
    hw_name: str    # e.g. "AMD Ryzen 7 5800X"
    type: str       # Temperature, Fan, Load, Power, Clock, Data, SmallData, Level, ...
    name: str       # e.g. "Core (Tctl/Tdie)"
    value: float
    max: Optional[float] = None


@dataclass
class LhmData:
    """The parts of LibreHardwareMonitor's data that the dashboard uses."""
    cpu_name: str = ""
    cpu_temp: float = -1
    cpu_power_w: float = -1
    cpu_clock_mhz: int = -1
    core_temps: List[float] = field(default_factory=list)
    fans: List[Fan] = field(default_factory=list)
    temps: List[TempSensor] = field(default_factory=list)
    disks: List[DiskHealth] = field(default_factory=list)
    gpu: GpuSample = field(default_factory=GpuSample)  # any GPU brand; NVML takes priority
    gpu_name: str = ""
    sensor_count: int = 0


# ---------------------------------------------------------------------------
# Parsing the JSON tree into a flat list of sensors
# ---------------------------------------------------------------------------

_ID_KINDS = [
    ("cpu", ("/intelcpu", "/amdcpu", "/cpu")),
    ("gpu", ("/gpu-", "/nvidiagpu", "/atigpu", "/gpu")),
    ("board", ("/lpc", "/motherboard", "/mainboard", "/ec/", "/superio")),
    ("storage", ("/nvme", "/hdd", "/ssd", "/storage", "/ata")),
    ("ram", ("/ram", "/memory")),
]
_ICON_KINDS = {"cpu.png": "cpu", "nvidia.png": "gpu", "ati.png": "gpu", "intel.png": "gpu",
               "hdd.png": "storage", "mainboard.png": "board", "chip.png": "board",
               "ram.png": "ram"}
_GROUP_TYPES = {"Voltages": "Voltage", "Currents": "Current", "Clocks": "Clock", "Load": "Load",
                "Temperatures": "Temperature", "Fans": "Fan", "Controls": "Control",
                "Levels": "Level", "Powers": "Power", "Data": "Data", "Throughput": "Throughput",
                "Frequencies": "Frequency", "Factors": "Factor"}
_NUMBER = re.compile(r"-?\d+(?:[.,]\d+)?")


def hardware_kind(hw_id, icon):
    hw_id = (hw_id or "").lower()
    for kind, prefixes in _ID_KINDS:
        if hw_id.startswith(prefixes):
            return kind
    return _ICON_KINDS.get((icon or "").rsplit("/", 1)[-1], "other")


def parse_number(value):
    """RawValue (a number) or a formatted value like '45.0 °C' / '1,234 RPM' / '45,0 °C'."""
    if isinstance(value, (int, float)):
        return float(value)
    if not isinstance(value, str):
        return None
    text = value.strip()
    m = _NUMBER.search(text.replace(" ", ""))
    if not m:
        return None
    num = m.group(0)
    # "1,234" is a thousands separator, "45,0" is a decimal comma
    if "," in num and len(num.split(",")[1]) == 3 and "." not in num:
        num = num.replace(",", "")
    num = num.replace(",", ".")
    try:
        return float(num)
    except ValueError:
        return None


def flatten(root):
    """Walks the tree and returns every sensor with the hardware it belongs to."""
    out = []

    def walk(node, kind, hw_name, group):
        children = node.get("Children") or []
        if "HardwareId" in node or (node.get("ImageURL", "").startswith("images_icon/")
                                    and node.get("ImageURL", "").rsplit("/", 1)[-1] in _ICON_KINDS):
            kind = hardware_kind(node.get("HardwareId"), node.get("ImageURL"))
            hw_name = node.get("Text", "")
        is_sensor = "SensorId" in node or (not children and node.get("Value") not in (None, "")
                                           and group is not None)
        if is_sensor:
            raw = node.get("RawValue")
            value = parse_number(raw) if raw is not None else parse_number(node.get("Value"))
            if value is not None:
                stype = node.get("Type") or _GROUP_TYPES.get(group or "", "Unknown")
                # Older versions put MB-sized values in the "Data" group as "SmallData"
                if stype == "Data" and raw is None and str(node.get("Value", "")).endswith("MB"):
                    stype = "SmallData"
                raw_max = node.get("RawMax")
                vmax = parse_number(raw_max) if raw_max is not None else parse_number(node.get("Max"))
                out.append(Sensor(kind, hw_name, stype, node.get("Text", ""), value, vmax))
            return
        if not children:
            return
        new_group = node.get("Text") if node.get("Text") in _GROUP_TYPES else group
        for child in children:
            walk(child, kind, hw_name, new_group)

    walk(root, "other", "", None)
    return out


# ---------------------------------------------------------------------------
# Picking out the useful sensors
# ---------------------------------------------------------------------------

# Best CPU temperature reading, in order of preference (Intel and AMD names)
CPU_TEMP_NAMES = ["CPU Package", "Core (Tctl/Tdie)", "Core (Tdie)", "Core (Tctl)", "Package",
                  "Core Max", "CCDs Max (Tdie)", "Core Average", "CPU Cores"]
CPU_POWER_NAMES = ["CPU Package", "Package"]


def _valid_temp(c):
    return c is not None and 0 < c < 125  # unconnected sensors read -128, 0 or 127


def _first(sensors, names):
    by_name = {s.name: s for s in sensors}
    for n in names:
        if n in by_name:
            return by_name[n]
    return None


def short_fan_name(name, kind):
    n = name.strip()
    n = re.sub(r"(?i)^system fan", "Sys", n)
    n = re.sub(r"(?i)^chassis fan", "Case", n)
    n = re.sub(r"(?i)\s*fan\s*", " ", n).strip()
    n = n.replace("#", "").replace("  ", " ").strip()
    if n.isdigit():              # "Fan #2" -> "Fan 2"
        n = "Fan " + n
    if kind == "gpu":
        n = "GPU " + n.replace("GPU", "").strip() if n.upper() != "GPU" else "GPU"
    return (n or "Fan")[:12].strip()


def short_disk_name(name):
    n = re.sub(r"(?i)\b(SSD|NVMe|HDD|series|with heatsink)\b", "", name)
    n = re.sub(r"\b\d+(\.\d+)?\s?[GT]B\b", "", n)  # capacity is shown elsewhere
    return re.sub(r"\s+", " ", n).strip()[:20]


def short_cpu_name(name):
    """'AMD Ryzen 7 5800X 8-Core Processor' -> 'Ryzen 7 5800X',
    'Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz' -> 'Core i7-9700K'."""
    n = re.sub(r"\((R|TM|tm|r)\)", "", name or "")
    n = re.sub(r"(?i)@.*$", "", n)
    n = re.sub(r"(?i)\b(\d+|Six|Eight|Quad|Dual)-Core\b", "", n)
    n = re.sub(r"(?i)\bwith Radeon.*$", "", n)
    n = re.sub(r"(?i)\b(Processor|CPU)\b", "", n)
    n = re.sub(r"(?i)^\d+th Gen\s+", "", n.strip())          # "13th Gen Intel Core..."
    n = re.sub(r"^(AMD|Intel)\s+", "", n.strip())
    return re.sub(r"\s+", " ", n).strip()[:22]


def extract(sensors) -> LhmData:
    d = LhmData(sensor_count=len(sensors))
    by_kind = {}
    for s in sensors:
        by_kind.setdefault(s.hw_kind, []).append(s)

    # CPU
    cpu = by_kind.get("cpu", [])
    if cpu:
        d.cpu_name = short_cpu_name(cpu[0].hw_name)
        temps = [s for s in cpu if s.type == "Temperature" and _valid_temp(s.value)]
        best = _first(temps, CPU_TEMP_NAMES)
        if best is None and temps:
            best = max(temps, key=lambda s: s.value)
        if best is not None:
            d.cpu_temp = round(best.value, 1)
        d.core_temps = [round(s.value) for s in temps
                        if re.match(r"(CPU )?Core #\d+$", s.name)][:16]
        powers = [s for s in cpu if s.type == "Power"]
        p = _first(powers, CPU_POWER_NAMES)
        if p is not None:
            d.cpu_power_w = round(p.value, 1)
        clocks = [s for s in cpu if s.type == "Clock" and s.value > 0]
        avg = _first(clocks, ["Cores (Average)", "Cores (Average Effective)"])
        core_clocks = [s.value for s in clocks if re.search(r"Core #\d+", s.name)]
        if avg is not None:
            d.cpu_clock_mhz = int(round(avg.value))
        elif core_clocks:
            d.cpu_clock_mhz = int(round(sum(core_clocks) / len(core_clocks)))

    # Fans: motherboard headers and GPU fans. Headers that have never spun are unused.
    for kind in ("board", "gpu"):
        for s in by_kind.get(kind, []):
            if s.type == "Fan" and (s.value > 0 or (s.max or 0) > 0):
                d.fans.append(Fan(short_fan_name(s.name, kind), int(round(s.value))))
    d.fans = d.fans[:8]

    # Motherboard temperatures (CPU and GPU have their own pages)
    seen = set()
    for s in by_kind.get("board", []):
        if s.type == "Temperature" and _valid_temp(s.value) and s.name not in seen:
            seen.add(s.name)
            d.temps.append(TempSensor(s.name.replace("#", "")[:12].strip(), round(s.value, 1)))
    d.temps = d.temps[:8]

    # Drives: temperature and remaining life, one entry per physical drive
    disks = {}
    for s in by_kind.get("storage", []):
        disk = disks.setdefault(s.hw_name, DiskHealth(short_disk_name(s.hw_name)))
        if s.type == "Temperature" and _valid_temp(s.value):
            if disk.temp < 0 or s.name in ("Temperature", "Composite Temperature"):
                disk.temp = round(s.value)
        elif s.type == "Level":
            if s.name == "Remaining Life":
                disk.life = round(s.value)
            elif s.name == "Percentage Used" and disk.life < 0:
                disk.life = max(0, 100 - round(s.value))
    d.disks = [x for x in disks.values() if x.temp >= 0 or x.life >= 0][:4]

    # GPU (used for the hotspot, and as a fallback for AMD/Intel cards)
    gpu = by_kind.get("gpu", [])
    if gpu:
        from .gpu import short_gpu_name
        d.gpu_name = short_gpu_name(gpu[0].hw_name)
        g = d.gpu

        def find(stype, *names):
            return _first([s for s in gpu if s.type == stype], names)

        s = find("Load", "GPU Core", "D3D 3D")
        if s:
            g.util = int(round(s.value))
        s = find("Temperature", "GPU Core", "GPU Temperature")
        if s and _valid_temp(s.value):
            g.temp = int(round(s.value))
        s = find("Temperature", "GPU Hot Spot", "GPU Hotspot", "GPU Junction")
        if s and _valid_temp(s.value):
            g.hotspot = int(round(s.value))
        s = find("Power", "GPU Package", "GPU Power", "GPU Core")
        if s:
            g.power_w = round(s.value, 1)
        s = find("Clock", "GPU Core")
        if s:
            g.clock_mhz = int(round(s.value))
        s = find("Clock", "GPU Memory")
        if s:
            g.mem_clock_mhz = int(round(s.value))
        for stype, scale in (("SmallData", 1), ("Data", 1024)):
            used = find(stype, "GPU Memory Used", "D3D Dedicated Memory Used")
            total = find(stype, "GPU Memory Total")
            if used and g.mem_used_mb < 0:
                g.mem_used_mb = int(used.value * scale)
            if total and g.mem_total_mb < 0:
                g.mem_total_mb = int(total.value * scale)
        s = find("Control", "GPU Fan")
        if s:
            g.fan = int(round(s.value))
    return d


# ---------------------------------------------------------------------------
# Reader
# ---------------------------------------------------------------------------

class LhmReader:
    """Fetches LibreHardwareMonitor's data. If it isn't running, retries every 15 s
    so a missing app doesn't slow every reading down."""

    RETRY_S = 15

    def __init__(self, url=DEFAULT_URL, timeout=0.6):
        self.url = url
        self.timeout = timeout
        self.available = False
        self.last_error = None
        self._next_try = 0.0

    def fetch_json(self):
        req = urllib.request.Request(self.url, headers={"Accept": "application/json"})
        with urllib.request.urlopen(req, timeout=self.timeout) as r:
            return json.loads(r.read().decode("utf-8", "replace"))

    def read(self) -> Optional[LhmData]:
        now = time.monotonic()
        if not self.available and now < self._next_try:
            return None
        try:
            data = extract(flatten(self.fetch_json()))
            self.available = True
            return data
        except Exception as e:
            self.available = False
            self.last_error = e
            self._next_try = now + self.RETRY_S
            return None

    def sensors(self):
        """All sensors, for the --sensors diagnostic listing."""
        return flatten(self.fetch_json())
