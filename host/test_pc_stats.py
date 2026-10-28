"""Unit tests for the host script's serial protocol. Run with:  python -m unittest"""
import unittest

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
