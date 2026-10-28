"""The line formats sent to each display."""
import copy
import unittest

from pcstats import protocol
from pcstats.model import Alert, DiskHealth, Drive, Fan, Process, Stats, TempSensor
from tests.fixtures import SAMPLE


def parse_kv(line):
    """Splits a CYD line back into a dict, the way the firmware does."""
    assert line.endswith("\n")
    return dict(part.split("=", 1) for part in line.strip().split(","))


class CydLineTests(unittest.TestCase):
    def setUp(self):
        self.f = parse_kv(protocol.format_cyd(SAMPLE))

    def test_scalar_fields(self):
        expected = {
            "cpu": "37", "cpun": "16", "cput": "67.5", "cpuw": "88.2", "cpuc": "4450",
            "ram": "61", "ramu": "9984", "ramt": "16310",
            "gpu": "72", "gput": "64", "gpuh": "76", "vram": "3120", "vramt": "8192",
            "gpw": "145.5", "gpwl": "215", "fan": "45", "gclk": "1905", "gmclk": "7000",
            "dn": "12697", "up": "380", "disk": "48", "dr": "2048", "dw": "512",
            "procn": "287", "upt": str(3 * 86400 + 4 * 3600), "hs": "88", "sens": "1",
            "t": "14:12", "d": "Wed 28 Oct", "gn": "RTX 2070 SUPER", "cpunm": "Ryzen 7 5800X",
            "hn": "jasperpc", "os": "Windows 11",
        }
        for k, v in expected.items():
            self.assertEqual(self.f.get(k), v, k)

    def test_lists(self):
        f = self.f
        self.assertEqual(f["cores"], "42;18;65;12;30;9;77;25;14;33;51;8;20;44;16;29")
        self.assertEqual(f["fans"], "CPU:1180;Sys 1:820;GPU 1:1450")
        self.assertEqual(f["temps"], "Motherboard:38;VRM MOS:52")
        self.assertEqual(f["procs"], "chrome:12.4:2310;Discord:3.1:412;explorer:1.2:156")
        self.assertEqual(f["drv"], "C:79:932;D:41:1863")   # "C:" loses its colon inside a list
        self.assertEqual(f["dsk"], "Samsung 970 EVO:41:98")
        self.assertEqual(f["al"], "1:Drive C filling up (91%);0:Up 3 days - restart soon")
        self.assertEqual(f["ok"], "CPU temp normal (68C);GPU temp normal (64C);Memory OK (61% used)")
        self.assertNotIn("ctemps", f)  # empty lists are left out

    def test_unknown_values_are_left_out(self):
        s = Stats(cpu=5, ram=50, down_kbs=0, up_kbs=0, disk=10, clock="09:00", date="Mon 02 Nov")
        f = parse_kv(protocol.format_cyd(s))
        for key in ("gpu", "gput", "cput", "vram", "fan", "cpun", "gn", "fans", "procs", "hs"):
            self.assertNotIn(key, f)
        self.assertEqual((f["cpu"], f["sens"]), ("5", "0"))

    def test_text_cannot_break_the_format(self):
        s = copy.deepcopy(SAMPLE)
        s.gpu_name = "Weird, GPU=Name"
        s.fans = [Fan("A;B:C", 100)]
        s.processes = [Process("a,b=c;d:e", 1.0, 2)]
        s.alerts = [Alert(1, "Drive C: full, 99%")]
        f = parse_kv(protocol.format_cyd(s))       # would raise if a ',' or '=' leaked
        self.assertEqual(f["gn"], "Weird GPU Name")
        self.assertEqual(f["fans"], "A B C:100")
        self.assertEqual(f["procs"], "a b c d e:1:2")
        self.assertEqual(f["al"], "1:Drive C full 99%")

    def test_worst_case_fits_firmware_buffer(self):
        s = copy.deepcopy(SAMPLE)
        s.core_loads = [100] * 64
        s.core_temps = [100] * 32
        s.fans = [Fan("X" * 30, 99999)] * 12
        s.temps = [TempSensor("X" * 30, 100)] * 12
        s.processes = [Process("X" * 40, 100.0, 999999)] * 10
        s.drives = [Drive("XXXXXXXX", 100, 99999)] * 8
        s.disks = [DiskHealth("X" * 40, 100, 100)] * 8
        s.alerts = [Alert(2, "X" * 60)] * 10
        s.gpu_name = s.cpu_name = s.host_name = s.os_name = "X" * 60
        line = protocol.format_cyd(s)
        self.assertLessEqual(len(line), protocol.MAX_LINE + 1)
        self.assertTrue(line.endswith("\n"))
        parse_kv(line)                              # still well-formed after trimming

    def test_old_firmware_still_gets_basic_fields_first(self):
        # Firmware before the health upgrade only read 256 bytes; the basics come first
        head = protocol.format_cyd(SAMPLE)[:255]
        for key in ("cpu=", "ram=", "gpu=", "gput=", "dn=", "disk="):
            self.assertIn(key, head)


class NanoLineTests(unittest.TestCase):
    def test_nano_line(self):
        self.assertEqual(protocol.format_nano(SAMPLE), "37,61,14:12\n")


if __name__ == "__main__":
    unittest.main()
