"""NVIDIA GPU reading through NVML and nvidia-smi, using stand-ins for both."""
import os
import sys
import types
import unittest
from unittest import mock

from pcstats import gpu as gpu_mod
from pcstats.gpu import GpuReader, short_gpu_name
from pcstats.model import GpuSample


def fake_pynvml(fail_init=False, fan_supported=True):
    m = types.ModuleType("pynvml")
    m.NVML_TEMPERATURE_GPU, m.NVML_CLOCK_GRAPHICS, m.NVML_CLOCK_MEM = 0, 0, 2

    def init():
        if fail_init:
            raise RuntimeError("NVML Shared Library Not Found")

    def fan(handle):
        if not fan_supported:
            raise RuntimeError("Not Supported")  # e.g. laptops
        return 45

    m.nvmlInit = init
    m.nvmlDeviceGetHandleByIndex = lambda i: "handle0"
    m.nvmlDeviceGetName = lambda h: b"NVIDIA GeForce RTX 2070 SUPER"
    m.nvmlDeviceGetUtilizationRates = lambda h: types.SimpleNamespace(gpu=42, memory=10)
    m.nvmlDeviceGetTemperature = lambda h, sensor: 63
    m.nvmlDeviceGetMemoryInfo = lambda h: types.SimpleNamespace(used=3120 * 2**20, total=8192 * 2**20)
    m.nvmlDeviceGetPowerUsage = lambda h: 145_500
    m.nvmlDeviceGetEnforcedPowerLimit = lambda h: 215_000
    m.nvmlDeviceGetFanSpeed = fan
    m.nvmlDeviceGetClockInfo = lambda h, kind: {0: 1905, 2: 7000}[kind]
    return m


class GpuTests(unittest.TestCase):
    def test_reads_everything_through_nvml(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml()}):
            g = GpuReader()
        self.assertEqual((g.source, g.short_name), ("NVML", "RTX 2070 SUPER"))
        self.assertEqual(g.read(), GpuSample(util=42, temp=63, mem_used_mb=3120, mem_total_mb=8192,
                                             power_w=145.5, power_limit_w=215.0, fan=45,
                                             clock_mhz=1905, mem_clock_mhz=7000))

    def test_unsupported_value_is_unknown(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fan_supported=False)}):
            sample = GpuReader().read()
        self.assertEqual((sample.util, sample.fan), (42, -1))

    def test_falls_back_to_nvidia_smi(self):
        out = {"name": "NVIDIA GeForce RTX 2070 SUPER\n",
               GpuReader.SMI_FIELDS: "17, 51, 2048, 8192, 95.30, 215.00, [N/A], 1500, 7000\n"}

        def fake_run(cmd, **kw):
            return types.SimpleNamespace(stdout=out[cmd[1].split("=", 1)[1]])

        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(gpu_mod, "find_nvidia_smi", return_value="nvidia-smi"), \
                mock.patch.object(gpu_mod.subprocess, "run", side_effect=fake_run):
            g = GpuReader()
            self.assertEqual(g.source, "nvidia-smi")
            self.assertEqual(g.read(), GpuSample(util=17, temp=51, mem_used_mb=2048, mem_total_mb=8192,
                                                 power_w=95.3, power_limit_w=215.0, fan=-1,
                                                 clock_mhz=1500, mem_clock_mhz=7000))

    def test_no_gpu(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(gpu_mod, "find_nvidia_smi", return_value=None):
            g = GpuReader()
        self.assertFalse(g.available)
        self.assertEqual((g.read(), g.short_name), (GpuSample(), ""))

    def test_finds_nvidia_smi_off_path_on_windows(self):
        smi = r"C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe"
        with mock.patch.object(gpu_mod.os, "name", "nt"), \
                mock.patch.object(gpu_mod.shutil, "which", return_value=None), \
                mock.patch.dict(os.environ, {"ProgramFiles": r"C:\Program Files"}), \
                mock.patch.object(gpu_mod.os.path, "isfile", side_effect=lambda p: p == smi), \
                mock.patch.object(gpu_mod.os.path, "join", side_effect=lambda *a: "\\".join(a)):
            self.assertEqual(gpu_mod.find_nvidia_smi(), smi)

    def test_short_names(self):
        self.assertEqual(short_gpu_name("NVIDIA GeForce RTX 2070 SUPER"), "RTX 2070 SUPER")
        self.assertEqual(short_gpu_name("NVIDIA RTX A2000"), "RTX A2000")
        self.assertEqual(short_gpu_name("AMD Radeon RX 6700 XT"), "Radeon RX 6700 XT")


if __name__ == "__main__":
    unittest.main()
