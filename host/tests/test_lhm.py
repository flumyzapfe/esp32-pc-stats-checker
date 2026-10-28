"""LibreHardwareMonitor parsing, using JSON shaped like its real data.json."""
import json
import unittest
from unittest import mock

from pcstats import lhm
from tests import fixtures


def extract(tree):
    return lhm.extract(lhm.flatten(json.loads(json.dumps(tree))))  # round-trip like real JSON


class IntelNvidiaTests(unittest.TestCase):
    def setUp(self):
        self.d = extract(fixtures.intel_nvidia_tree())

    def test_cpu(self):
        d = self.d
        self.assertEqual(d.cpu_name, "Core i7-9700K")
        self.assertEqual(d.cpu_temp, 71.0)                 # "CPU Package" beats the others
        self.assertEqual(d.cpu_power_w, 95.4)
        self.assertEqual(d.cpu_clock_mhz, 4500)            # average of the core clocks, not bus
        self.assertEqual(d.core_temps, [66, 69])

    def test_fans_skip_unused_headers(self):
        names = [(f.name, f.rpm) for f in self.d.fans]
        self.assertEqual(names, [("CPU", 1180), ("Sys 1", 820), ("Fan 4", 0), ("GPU 1", 1450),
                                 ("GPU 2", 1440)])

    def test_board_temps_skip_disconnected_sensors(self):
        self.assertEqual([(t.name, t.celsius) for t in self.d.temps],
                         [("CPU", 45.0), ("Motherboard", 38.0), ("VRM MOS", 52.0)])

    def test_nvme_health(self):
        disk = self.d.disks[0]
        self.assertEqual((disk.name, disk.temp, disk.life), ("Samsung 970 EVO Plus", 41, 98))

    def test_gpu_hotspot(self):
        self.assertEqual((self.d.gpu.temp, self.d.gpu.hotspot, self.d.gpu.util), (64, 77, 72))


class AmdTests(unittest.TestCase):
    def setUp(self):
        self.d = extract(fixtures.amd_radeon_tree())

    def test_cpu(self):
        d = self.d
        self.assertEqual((d.cpu_name, d.cpu_temp, d.cpu_power_w, d.cpu_clock_mhz),
                         ("Ryzen 7 5800X", 67.5, 88.2, 4450))
        self.assertEqual(d.core_temps, [])                 # AMD doesn't report per-core temps

    def test_radeon_gpu_fallback(self):
        g = self.d.gpu
        self.assertEqual(self.d.gpu_name, "Radeon RX 6700 XT")
        self.assertEqual((g.util, g.temp, g.hotspot, g.power_w), (33, 58, 71, 120.0))
        self.assertEqual((g.clock_mhz, g.mem_clock_mhz, g.mem_used_mb, g.mem_total_mb, g.fan),
                         (2400, 2000, 4096, 12288, 38))

    def test_sata_remaining_life(self):
        self.assertEqual((self.d.disks[0].name, self.d.disks[0].life), ("Crucial MX500", 87))


class LegacyFormatTests(unittest.TestCase):
    def test_works_without_ids_types_or_raw_values(self):
        d = extract(fixtures.legacy_tree())
        self.assertEqual(d.cpu_temp, 58.0)                 # parsed from "58,0 °C"
        self.assertEqual(d.cpu_power_w, 41.5)
        self.assertEqual([(f.name, f.rpm) for f in d.fans], [("Fan 1", 950)])
        self.assertEqual([(t.name, t.celsius) for t in d.temps], [("System 1", 33.0)])


class NumberParsingTests(unittest.TestCase):
    def test_formats(self):
        p = lhm.parse_number
        self.assertEqual(p(45), 45.0)
        self.assertEqual(p("45.5 °C"), 45.5)
        self.assertEqual(p("45,5 °C"), 45.5)
        self.assertEqual(p("1,234 RPM"), 1234.0)
        self.assertEqual(p("-128.0 °C"), -128.0)
        self.assertIsNone(p(""))
        self.assertIsNone(p(None))


class NameTests(unittest.TestCase):
    def test_cpu_names(self):
        s = lhm.short_cpu_name
        self.assertEqual(s("AMD Ryzen 7 5800X 8-Core Processor"), "Ryzen 7 5800X")
        self.assertEqual(s("Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz"), "Core i7-9700K")
        self.assertEqual(s("AMD Ryzen 5 5600G with Radeon Graphics"), "Ryzen 5 5600G")
        self.assertEqual(s("13th Gen Intel(R) Core(TM) i5-13600K"), "Core i5-13600K")

    def test_fan_names(self):
        f = lhm.short_fan_name
        self.assertEqual(f("CPU Fan", "board"), "CPU")
        self.assertEqual(f("System Fan #2", "board"), "Sys 2")
        self.assertEqual(f("Chassis Fan 1", "board"), "Case 1")
        self.assertEqual(f("Fan #3", "board"), "Fan 3")
        self.assertEqual(f("GPU Fan", "gpu"), "GPU")
        self.assertEqual(f("GPU Fan 2", "gpu"), "GPU 2")


class ReaderTests(unittest.TestCase):
    def test_unreachable_server_is_not_retried_every_second(self):
        r = lhm.LhmReader("http://127.0.0.1:9/data.json", timeout=0.1)
        with mock.patch.object(r, "fetch_json", side_effect=OSError("refused")) as fetch:
            self.assertIsNone(r.read())
            self.assertIsNone(r.read())
            self.assertEqual(fetch.call_count, 1)
        self.assertFalse(r.available)

    def test_reads_when_running(self):
        r = lhm.LhmReader()
        with mock.patch.object(r, "fetch_json", return_value=fixtures.amd_radeon_tree()):
            d = r.read()
        self.assertTrue(r.available)
        self.assertEqual(d.cpu_temp, 67.5)


if __name__ == "__main__":
    unittest.main()
