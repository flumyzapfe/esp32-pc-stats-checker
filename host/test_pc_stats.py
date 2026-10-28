"""Unit tests for the host script. Run with:  python -m unittest -v"""
import os
import sys
import types
import unittest
from unittest import mock

import pc_stats
from pc_stats import GpuSample, Stats

SAMPLE = Stats(
    cpu=37, ram=61, down_kbs=12697, up_kbs=380, disk=48, clock="14:12", date="Wed 28 Oct",
    cpu_threads=16, ram_used_mb=9984, ram_total_mb=16310,
    gpu=GpuSample(util=72, temp=64, mem_used_mb=3120, mem_total_mb=8192,
                  power_w=145.5, power_limit_w=215.0, fan=45),
    gpu_name="RTX 2070 SUPER",
)


def parse_kv(line):
    """Splits a CYD line back into a dict, the way the firmware does."""
    assert line.endswith("\n")
    return dict(part.split("=", 1) for part in line.strip().split(","))


class CydProtocolTests(unittest.TestCase):
    def test_all_fields(self):
        self.assertEqual(parse_kv(pc_stats.format_cyd(SAMPLE)), {
            "cpu": "37", "cpun": "16", "ram": "61", "ramu": "9984", "ramt": "16310",
            "gpu": "72", "gput": "64", "vram": "3120", "vramt": "8192",
            "gpw": "145.5", "gpwl": "215", "fan": "45",
            "dn": "12697", "up": "380", "disk": "48",
            "t": "14:12", "d": "Wed 28 Oct", "gn": "RTX 2070 SUPER",
        })

    def test_unknown_values_are_left_out(self):
        s = Stats(cpu=5, ram=50, down_kbs=0, up_kbs=0, disk=10, clock="09:00", date="Mon 02 Nov")
        fields = parse_kv(pc_stats.format_cyd(s))
        for key in ("gpu", "gput", "vram", "fan", "cpun", "gn"):
            self.assertNotIn(key, fields)
        self.assertEqual(fields["cpu"], "5")

    def test_fits_firmware_buffer(self):
        # cyd_dashboard.ino reads lines into a 256-byte buffer
        worst = Stats(cpu=100, ram=100, down_kbs=9_999_999, up_kbs=9_999_999, disk=100,
                      clock="23:59", date="Wed 30 Sep", cpu_threads=256, ram_used_mb=9_999_999,
                      ram_total_mb=9_999_999,
                      gpu=GpuSample(100, 110, 999_999, 999_999, 9999.9, 9999.9, 100),
                      gpu_name="X" * 60)
        self.assertLess(len(pc_stats.format_cyd(worst)), 256)

    def test_text_cannot_break_the_format(self):
        s = Stats(cpu=1, ram=1, down_kbs=0, up_kbs=0, disk=1, clock="1,2", date="a=b",
                  gpu_name="Weird, GPU=Name")
        fields = parse_kv(pc_stats.format_cyd(s))  # would raise if a separator leaked in
        self.assertEqual(fields["gn"], "Weird  GPU Name")


class NanoProtocolTests(unittest.TestCase):
    def test_nano_line(self):
        self.assertEqual(pc_stats.format_nano(SAMPLE), "37,61,14:12\n")


class HelperTests(unittest.TestCase):
    def test_short_gpu_name(self):
        self.assertEqual(pc_stats.short_gpu_name("NVIDIA GeForce RTX 2070 SUPER"), "RTX 2070 SUPER")
        self.assertEqual(pc_stats.short_gpu_name("NVIDIA RTX A2000"), "RTX A2000")

    def test_smi_number_parsing(self):
        self.assertEqual(pc_stats._num("145.20"), 145.2)
        self.assertEqual(pc_stats._num("[N/A]"), -1)
        self.assertEqual(pc_stats._num("[Not Supported]"), -1)


class CollectorTests(unittest.TestCase):
    def test_reading_is_in_range(self):
        s = pc_stats.StatsCollector().read(interval=0.1)
        self.assertTrue(0 <= s.cpu <= 100)
        self.assertTrue(0 <= s.ram <= 100)
        self.assertGreater(s.ram_total_mb, 0)
        self.assertTrue(0 < s.ram_used_mb <= s.ram_total_mb)
        self.assertGreaterEqual(s.down_kbs, 0)
        self.assertRegex(s.clock, r"^\d\d:\d\d$")


def fake_pynvml(fail_init=False, fan_supported=True):
    """A stand-in for the pynvml module, so GPU code can be tested without a GPU."""
    m = types.ModuleType("pynvml")
    m.NVML_TEMPERATURE_GPU = 0

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
    return m


class GpuTests(unittest.TestCase):
    def test_reads_everything_through_nvml(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml()}):
            gpu = pc_stats.GpuReader()
        self.assertEqual((gpu.source, gpu.short_name), ("NVML", "RTX 2070 SUPER"))
        self.assertEqual(gpu.read(), GpuSample(util=42, temp=63, mem_used_mb=3120, mem_total_mb=8192,
                                               power_w=145.5, power_limit_w=215.0, fan=45))

    def test_unsupported_nvml_value_is_unknown(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fan_supported=False)}):
            sample = pc_stats.GpuReader().read()
        self.assertEqual((sample.util, sample.fan), (42, -1))

    def test_falls_back_to_nvidia_smi(self):
        smi_output = {"name": "NVIDIA GeForce RTX 2070 SUPER\n",
                      pc_stats.GpuReader.SMI_FIELDS: "17, 51, 2048, 8192, 95.30, 215.00, [N/A]\n"}

        def fake_run(cmd, **kw):
            return types.SimpleNamespace(stdout=smi_output[cmd[1].split("=", 1)[1]])

        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(pc_stats, "find_nvidia_smi", return_value="nvidia-smi"), \
                mock.patch.object(pc_stats.subprocess, "run", side_effect=fake_run):
            gpu = pc_stats.GpuReader()
            self.assertEqual(gpu.source, "nvidia-smi")
            self.assertEqual(gpu.read(), GpuSample(17, 51, 2048, 8192, 95.3, 215.0, -1))

    def test_no_gpu_reports_unknown(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(pc_stats, "find_nvidia_smi", return_value=None):
            gpu = pc_stats.GpuReader()
        self.assertFalse(gpu.available)
        self.assertEqual(gpu.read(), GpuSample())
        self.assertEqual(gpu.short_name, "")

    def test_finds_nvidia_smi_off_path_on_windows(self):
        smi = r"C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe"
        with mock.patch.object(pc_stats.os, "name", "nt"), \
                mock.patch.object(pc_stats.shutil, "which", return_value=None), \
                mock.patch.dict(os.environ, {"ProgramFiles": r"C:\Program Files"}), \
                mock.patch.object(pc_stats.os.path, "isfile", side_effect=lambda p: p == smi), \
                mock.patch.object(pc_stats.os.path, "join", side_effect=lambda *a: "\\".join(a)):
            self.assertEqual(pc_stats.find_nvidia_smi(), smi)


class ArgTests(unittest.TestCase):
    def test_defaults(self):
        a = pc_stats.parse_args([])
        self.assertEqual((a.board, a.port), ("cyd", None))

    def test_old_style_port_argument_still_works(self):
        self.assertEqual(pc_stats.parse_args(["COM5"]).legacy_port, "COM5")

    def test_nano(self):
        self.assertEqual(pc_stats.parse_args(["--board", "nano"]).board, "nano")


if __name__ == "__main__":
    unittest.main()
