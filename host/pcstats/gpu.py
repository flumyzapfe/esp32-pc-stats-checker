"""NVIDIA GPU stats through NVML, with nvidia-smi as a fallback."""
import os
import shutil
import subprocess

from .model import GpuSample

NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)  # hide nvidia-smi's console on Windows


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
    name = (name or "").strip()
    for prefix in ("NVIDIA ", "GeForce ", "AMD ", "Radeon(TM) ", "Intel(R) "):
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
    """Stats for the first NVIDIA GPU: load, temperature, VRAM, power, fan and clocks.

    Uses NVML (NVIDIA's driver library, via the nvidia-ml-py package) when available:
    it's fast and needs nothing on PATH. Falls back to calling nvidia-smi.
    Every value is -1 when there's no NVIDIA GPU, or the card doesn't report it.
    """

    SMI_FIELDS = ("utilization.gpu,temperature.gpu,memory.used,memory.total,"
                  "power.draw,power.limit,fan.speed,clocks.gr,clocks.mem")

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
                nv = self._nvml
                util = self._nvml_value("nvmlDeviceGetUtilizationRates")
                temp = self._nvml_value("nvmlDeviceGetTemperature", nv.NVML_TEMPERATURE_GPU)
                mem = self._nvml_value("nvmlDeviceGetMemoryInfo")
                power = self._nvml_value("nvmlDeviceGetPowerUsage")           # milliwatts
                limit = self._nvml_value("nvmlDeviceGetEnforcedPowerLimit")   # milliwatts
                fan = self._nvml_value("nvmlDeviceGetFanSpeed")
                clock = self._nvml_value("nvmlDeviceGetClockInfo", getattr(nv, "NVML_CLOCK_GRAPHICS", 0))
                mclock = self._nvml_value("nvmlDeviceGetClockInfo", getattr(nv, "NVML_CLOCK_MEM", 2))
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
                if clock is not None:
                    g.clock_mhz = int(clock)
                if mclock is not None:
                    g.mem_clock_mhz = int(mclock)
            elif self._smi:
                v = [_num(x) for x in self._query_smi(self.SMI_FIELDS, timeout=3)]
                v += [-1] * (9 - len(v))
                g = GpuSample(util=int(v[0]), temp=int(v[1]), mem_used_mb=int(v[2]),
                              mem_total_mb=int(v[3]), power_w=v[4], power_limit_w=v[5],
                              fan=int(v[6]), clock_mhz=int(v[7]), mem_clock_mhz=int(v[8]))
        except Exception:
            return GpuSample()
        return g
