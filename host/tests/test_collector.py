"""Collector and CLI, run against this machine (no display or GPU needed)."""
import unittest
from unittest import mock

import pc_stats
from pcstats.collector import StatsCollector
from pcstats.lhm import LhmData
from tests import fixtures


class CollectorTests(unittest.TestCase):
    def test_real_reading_is_sane(self):
        c = StatsCollector()
        s = c.read()
        self.assertTrue(0 <= s.cpu <= 100)
        self.assertTrue(0 <= s.ram <= 100)
        self.assertEqual(len(s.core_loads), c.cpu_threads)
        self.assertTrue(0 < s.ram_used_mb <= s.ram_total_mb)
        self.assertGreater(s.uptime_s, 0)
        self.assertGreater(s.process_count, 0)
        self.assertTrue(s.drives)
        self.assertTrue(0 <= s.health_score <= 100)
        self.assertRegex(s.clock, r"^\d\d:\d\d$")

    def test_sensor_data_is_merged_in(self):
        c = StatsCollector()
        from pcstats import lhm
        data = lhm.extract(lhm.flatten(fixtures.intel_nvidia_tree()))
        with mock.patch.object(c.lhm, "read", return_value=data):
            s = c.read()
        self.assertTrue(s.sensors_available)
        self.assertEqual((s.cpu_temp, s.cpu_power_w), (71.0, 95.4))
        self.assertEqual(len(s.fans), 5)
        self.assertEqual(s.disks[0].life, 98)

    def test_amd_gpu_used_when_no_nvidia(self):
        c = StatsCollector()
        c.gpu.source = None  # pretend NVML isn't available
        from pcstats import lhm
        data = lhm.extract(lhm.flatten(fixtures.amd_radeon_tree()))
        with mock.patch.object(c.lhm, "read", return_value=data):
            s = c.read()
        self.assertEqual((s.gpu_name, s.gpu.util, s.gpu.temp), ("Radeon RX 6700 XT", 33, 58))

    def test_without_sensors(self):
        c = StatsCollector()
        with mock.patch.object(c.lhm, "read", return_value=None):
            s = c.read()
        self.assertFalse(s.sensors_available)
        self.assertEqual(s.cpu_temp, -1)
        self.assertIsInstance(LhmData(), LhmData)


class ArgTests(unittest.TestCase):
    def test_defaults(self):
        a = pc_stats.parse_args([])
        self.assertEqual((a.board, a.port, a.sensors), ("cyd", None, False))

    def test_old_style_port_argument_still_works(self):
        self.assertEqual(pc_stats.parse_args(["COM5"]).legacy_port, "COM5")

    def test_options(self):
        a = pc_stats.parse_args(["--board", "nano", "--sensors", "--lhm-url", "http://x/data.json"])
        self.assertEqual((a.board, a.sensors, a.lhm_url), ("nano", True, "http://x/data.json"))


if __name__ == "__main__":
    unittest.main()
