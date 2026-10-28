"""Unit tests for the host script's serial protocol. Run with:  python -m unittest"""
import os
import sys
import types
import unittest
from unittest import mock

import pc_stats
from pc_stats import Stats

SAMPLE = Stats(cpu=37, ram=61, gpu=72, gpu_temp=64, down_kbs=12697, up_kbs=380,
               disk=48, clock="14:12", date="Wed 28 Oct")


class ProtocolTests(unittest.TestCase):
    def test_cyd_line(self):
        self.assertEqual(pc_stats.format_cyd(SAMPLE),
                         "37,61,72,64,12697,380,48,14:12,Wed 28 Oct\n")

    def test_cyd_line_has_nine_fields(self):
        # The firmware rejects any line that doesn't split into exactly 9 fields
        self.assertEqual(len(pc_stats.format_cyd(SAMPLE).strip().split(",")), 9)

    def test_cyd_line_fits_firmware_buffer(self):
        # cyd_dashboard.ino reads lines into a 96-byte buffer
        worst = Stats(100, 100, 100, 110, 9_999_999, 9_999_999, 100, "23:59", "Wed 30 Sep")
        self.assertLess(len(pc_stats.format_cyd(worst)), 96)

    def test_nano_line(self):
        self.assertEqual(pc_stats.format_nano(SAMPLE), "37,61,14:12\n")

    def test_no_gpu_sends_minus_one(self):
        s = Stats(5, 50, -1, -1, 0, 0, 10, "09:00", "Mon 02 Nov")
        self.assertTrue(pc_stats.format_cyd(s).startswith("5,50,-1,-1,"))


class CollectorTests(unittest.TestCase):
    def test_reading_is_in_range(self):
        s = pc_stats.StatsCollector().read(interval=0.1)
        self.assertTrue(0 <= s.cpu <= 100)
        self.assertTrue(0 <= s.ram <= 100)
        self.assertGreaterEqual(s.down_kbs, 0)
        self.assertRegex(s.clock, r"^\d\d:\d\d$")


def fake_pynvml(util=42, temp=63, name=b"NVIDIA GeForce RTX 2070 SUPER", fail_init=False):
    """A stand-in for the pynvml module, so GPU code can be tested without a GPU."""
    m = types.ModuleType("pynvml")
    m.NVML_TEMPERATURE_GPU = 0

    def init():
        if fail_init:
            raise RuntimeError("NVML Shared Library Not Found")
    m.nvmlInit = init
    m.nvmlDeviceGetHandleByIndex = lambda i: "handle0"
    m.nvmlDeviceGetName = lambda h: name
    m.nvmlDeviceGetUtilizationRates = lambda h: types.SimpleNamespace(gpu=util, memory=10)
    m.nvmlDeviceGetTemperature = lambda h, sensor: temp
    return m


class GpuTests(unittest.TestCase):
    def test_reads_through_nvml(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml()}):
            gpu = pc_stats.GpuReader()
        self.assertEqual((gpu.source, gpu.name), ("NVML", "NVIDIA GeForce RTX 2070 SUPER"))
        self.assertEqual(gpu.read(), (42, 63))

    def test_falls_back_to_nvidia_smi(self):
        smi_output = {"name": "NVIDIA GeForce RTX 2070 SUPER\n",
                      "utilization.gpu,temperature.gpu": "17, 51\n"}

        def fake_run(cmd, **kw):
            field = cmd[1].split("=", 1)[1]
            return types.SimpleNamespace(stdout=smi_output[field])

        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(pc_stats, "find_nvidia_smi", return_value="nvidia-smi"), \
                mock.patch.object(pc_stats.subprocess, "run", side_effect=fake_run):
            gpu = pc_stats.GpuReader()
            self.assertEqual(gpu.source, "nvidia-smi")
            self.assertEqual(gpu.read(), (17, 51))

    def test_no_gpu_reports_minus_one(self):
        with mock.patch.dict(sys.modules, {"pynvml": fake_pynvml(fail_init=True)}), \
                mock.patch.object(pc_stats, "find_nvidia_smi", return_value=None):
            gpu = pc_stats.GpuReader()
        self.assertFalse(gpu.available)
        self.assertEqual(gpu.read(), (-1, -1))

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
