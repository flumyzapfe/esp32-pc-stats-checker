"""Data classes for one reading of the PC's state.

Every numeric field uses -1 for "unknown / not available", which the protocol
leaves out and the display shows as "--".
"""
from dataclasses import dataclass, field
from typing import List


@dataclass
class GpuSample:
    """One reading from the graphics card."""
    util: int = -1            # load %
    temp: int = -1            # core temperature, deg C
    hotspot: int = -1         # hottest point on the die, deg C (from LibreHardwareMonitor)
    mem_used_mb: int = -1
    mem_total_mb: int = -1
    power_w: float = -1
    power_limit_w: float = -1
    fan: int = -1             # %
    clock_mhz: int = -1       # core clock
    mem_clock_mhz: int = -1


@dataclass
class Fan:
    name: str
    rpm: int


@dataclass
class TempSensor:
    name: str
    celsius: float


@dataclass
class Process:
    name: str
    cpu: float                # % of the whole CPU, like Task Manager
    mem_mb: int


@dataclass
class Drive:
    name: str                 # "C:" on Windows, mount point name elsewhere
    used_pct: float
    total_gb: float


@dataclass
class DiskHealth:
    name: str                 # drive model, shortened
    temp: float = -1          # deg C
    life: float = -1          # % life remaining


# Alert levels
INFO, WARNING, CRITICAL = 0, 1, 2


@dataclass
class Alert:
    level: int                # INFO, WARNING or CRITICAL
    text: str


@dataclass
class Stats:
    # Always available (psutil)
    cpu: int                  # total load %
    ram: int                  # %
    down_kbs: int             # network, KB/s
    up_kbs: int
    disk: int                 # % used on the system drive
    clock: str                # "HH:MM"
    date: str                 # "Wed 28 Oct"
    cpu_threads: int = -1
    cpu_name: str = ""
    core_loads: List[int] = field(default_factory=list)   # per logical CPU, %
    ram_used_mb: int = -1
    ram_total_mb: int = -1
    disk_read_kbs: int = -1
    disk_write_kbs: int = -1
    drives: List[Drive] = field(default_factory=list)
    processes: List[Process] = field(default_factory=list)
    process_count: int = -1
    uptime_s: int = -1
    host_name: str = ""
    os_name: str = ""

    # GPU (NVML, or LibreHardwareMonitor for any brand)
    gpu: GpuSample = field(default_factory=GpuSample)
    gpu_name: str = ""

    # Needs LibreHardwareMonitor on Windows
    sensors_available: bool = False
    cpu_temp: float = -1
    cpu_power_w: float = -1
    cpu_clock_mhz: int = -1
    core_temps: List[float] = field(default_factory=list)
    fans: List[Fan] = field(default_factory=list)
    temps: List[TempSensor] = field(default_factory=list)  # motherboard etc.
    disks: List[DiskHealth] = field(default_factory=list)

    # Worked out by health.py
    health_score: int = -1
    alerts: List[Alert] = field(default_factory=list)
    checks_passed: List[str] = field(default_factory=list)   # e.g. "CPU temp normal (62C)"
