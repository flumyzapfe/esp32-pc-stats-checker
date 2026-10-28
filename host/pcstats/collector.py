"""Combines every source into one Stats reading."""
import time

import psutil

from . import system
from .gpu import GpuReader
from .health import HealthMonitor
from .lhm import DEFAULT_URL, LhmReader
from .model import Stats


class StatsCollector:
    """Call read() about once a second. Slow sources are refreshed less often:
    processes every 2 s, drive space every 10 s."""

    PROCESS_EVERY_S = 2
    DRIVES_EVERY_S = 10

    def __init__(self, lhm_url=DEFAULT_URL):
        self.gpu = GpuReader()
        self.lhm = LhmReader(lhm_url)
        self.health = HealthMonitor()
        self.processes = system.ProcessTracker()
        self.cpu_threads = psutil.cpu_count(logical=True) or -1
        self.cpu_name = system.cpu_model_name()
        self.os_name = system.os_name()
        self.host_name = system.host_name()
        self._net = system.RateCounter(system.net_bytes)
        self._disk_io = system.RateCounter(system.disk_bytes)
        self._procs, self._procs_t = [], 0.0
        self._drives, self._drives_t = [], 0.0
        psutil.cpu_percent()              # the first reading is always 0, so take it now
        psutil.cpu_percent(percpu=True)

    def read(self) -> Stats:
        now = time.monotonic()
        cpu = round(psutil.cpu_percent())          # since the previous call
        cores = [round(x) for x in psutil.cpu_percent(percpu=True)]
        down, up = self._net.rates()
        disk_r, disk_w = self._disk_io.rates()
        mem = psutil.virtual_memory()
        try:
            disk = round(psutil.disk_usage(system.ROOT_DRIVE).percent)
        except OSError:
            disk = -1

        if now - self._procs_t >= self.PROCESS_EVERY_S:
            self._procs, self._procs_t = self.processes.top(), now
        if now - self._drives_t >= self.DRIVES_EVERY_S:
            self._drives, self._drives_t = system.list_drives(), now

        s = Stats(
            cpu=cpu, ram=round(mem.percent), down_kbs=down, up_kbs=up, disk=disk,
            clock=time.strftime("%H:%M"), date=time.strftime("%a %d %b"),
            cpu_threads=self.cpu_threads, cpu_name=self.cpu_name, core_loads=cores,
            ram_used_mb=int((mem.total - mem.available) // 2**20),
            ram_total_mb=int(mem.total // 2**20),
            disk_read_kbs=disk_r, disk_write_kbs=disk_w,
            drives=self._drives, processes=self._procs,
            process_count=self.processes.count,
            uptime_s=int(time.time() - psutil.boot_time()),
            host_name=self.host_name, os_name=self.os_name,
            gpu=self.gpu.read(), gpu_name=self.gpu.short_name,
        )

        lhm = self.lhm.read()
        if lhm is not None:
            s.sensors_available = True
            s.cpu_temp, s.cpu_power_w, s.cpu_clock_mhz = lhm.cpu_temp, lhm.cpu_power_w, lhm.cpu_clock_mhz
            s.core_temps, s.fans, s.temps, s.disks = lhm.core_temps, lhm.fans, lhm.temps, lhm.disks
            if not s.cpu_name and lhm.cpu_name:
                s.cpu_name = lhm.cpu_name
            if self.gpu.available:
                # NVML is the main GPU source; LibreHardwareMonitor adds the hotspot
                s.gpu.hotspot = lhm.gpu.hotspot
            elif lhm.gpu_name:
                s.gpu, s.gpu_name = lhm.gpu, lhm.gpu_name   # AMD / Intel graphics

        s.health_score, s.alerts, s.checks_passed = self.health.evaluate(s)
        return s
