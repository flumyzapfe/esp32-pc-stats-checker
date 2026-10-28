"""Health score and alerts."""
import copy
import unittest

from pcstats.health import HealthMonitor, status_text
from pcstats.model import CRITICAL, INFO, WARNING, DiskHealth, Drive, Fan, Stats


def healthy():
    return Stats(cpu=20, ram=40, down_kbs=0, up_kbs=0, disk=30, clock="12:00", date="Mon",
                 sensors_available=True, cpu_temp=55, drives=[Drive("C:", 50, 500)],
                 fans=[Fan("CPU", 1100), Fan("GPU 1", 0)], uptime_s=3600)


class HealthTests(unittest.TestCase):
    def evaluate(self, s):
        score, alerts, _ = HealthMonitor().evaluate(s)
        return score, alerts

    def test_healthy_pc_scores_100(self):
        score, alerts = self.evaluate(healthy())
        self.assertEqual((score, alerts), (100, []))
        self.assertEqual(status_text(score), "Healthy")

    def test_cpu_temperature_levels(self):
        s = healthy()
        s.cpu_temp = 84
        score, alerts = self.evaluate(s)
        self.assertEqual((alerts[0].level, alerts[0].text, score), (WARNING, "CPU running hot (84C)", 88))
        s.cpu_temp = 93
        score, alerts = self.evaluate(s)
        self.assertEqual((alerts[0].level, alerts[0].text, score), (CRITICAL, "CPU very hot (93C)", 70))

    def test_drive_space_and_wear(self):
        s = healthy()
        s.drives = [Drive("C:", 96, 500), Drive("D:", 91, 2000)]
        s.disks = [DiskHealth("Old SSD", 45, 8)]
        score, alerts = self.evaluate(s)
        texts = [(a.level, a.text) for a in alerts]
        self.assertIn((CRITICAL, "Drive C 96% full"), texts)
        self.assertIn((WARNING, "Drive D 91% full"), texts)
        self.assertIn((CRITICAL, "Old worn (8% life)"), texts)
        self.assertEqual(score, 100 - 30 - 12 - 30)

    def test_stopped_cpu_fan_is_critical_but_idle_gpu_fan_is_fine(self):
        s = healthy()
        s.fans = [Fan("CPU", 0), Fan("GPU 1", 0), Fan("Sys 2", 0)]
        _, alerts = self.evaluate(s)
        self.assertEqual([(a.level, a.text) for a in alerts],
                         [(CRITICAL, "CPU fan not spinning"), (INFO, "Sys 2 fan stopped")])

    def test_sustained_cpu_load_needs_a_minute_of_history(self):
        m = HealthMonitor()
        s = healthy()
        s.cpu = 99
        for _ in range(29):
            _, alerts, _ = m.evaluate(s)
            self.assertEqual(alerts, [])
        _, alerts, _ = m.evaluate(s)
        self.assertEqual(alerts[0].text, "CPU at 99% for 1 min")

    def test_info_alerts_do_not_lower_the_score(self):
        s = healthy()
        s.sensors_available = False
        s.cpu_temp = -1
        s.uptime_s = 9 * 86400
        score, alerts = self.evaluate(s)
        self.assertEqual(score, 100)
        self.assertEqual({a.level for a in alerts}, {INFO})

    def test_alerts_sorted_most_serious_first(self):
        s = copy.deepcopy(healthy())
        s.uptime_s, s.ram, s.cpu_temp = 9 * 86400, 97, 82
        levels = [a.level for a in self.evaluate(s)[1]]
        self.assertEqual(levels, sorted(levels, reverse=True))

    def test_passed_checks_are_listed(self):
        s = healthy()
        s.gpu.temp = 60
        _, _, passed = HealthMonitor().evaluate(s)
        self.assertEqual(passed, ["CPU temp normal (55C)", "GPU temp normal (60C)",
                                  "Memory OK (40% used)", "Drive space OK",
                                  "1 fan spinning"])

    def test_failed_check_is_not_listed_as_passed(self):
        s = healthy()
        s.cpu_temp, s.ram = 91, 97
        _, _, passed = HealthMonitor().evaluate(s)
        self.assertFalse(any(p.startswith(("CPU", "Memory")) for p in passed))

    def test_score_never_negative(self):
        s = healthy()
        s.drives = [Drive(f"{c}:", 99, 100) for c in "CDEFG"]
        self.assertEqual(self.evaluate(s)[0], 0)


if __name__ == "__main__":
    unittest.main()
