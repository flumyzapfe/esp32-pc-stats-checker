"""Health checks: turns a reading into a 0-100 score and a list of plain-English alerts.

Each check looks at one thing (temperatures, memory, drive space, drive wear, fans,
sustained load) and raises a warning or critical alert past a threshold. The score
starts at 100 and loses points per warning and per critical alert, so it's easy to
explain why it's not 100. INFO alerts are tips and don't affect the score.
"""
from collections import deque

from .model import CRITICAL, INFO, WARNING, Alert, Stats

WARNING_PENALTY = 12
CRITICAL_PENALTY = 30

# (warning, critical) thresholds
CPU_TEMP = (80, 90)       # deg C
GPU_TEMP = (80, 88)
GPU_HOTSPOT = (95, 102)
RAM = (85, 95)            # % used
DRIVE_FULL = (90, 95)     # % used
DRIVE_TEMP = (60, 70)     # deg C
DRIVE_LIFE = (25, 10)     # % remaining (lower is worse)
SUSTAINED_CPU = 90        # % average over the last minute
LONG_UPTIME_DAYS = 7


def _level(value, thresholds):
    warn, crit = thresholds
    if value >= crit:
        return CRITICAL
    if value >= warn:
        return WARNING
    return None


def status_text(score):
    if score >= 90:
        return "Healthy"
    if score >= 70:
        return "Fair"
    if score >= 40:
        return "Needs attention"
    return "Critical"


class HealthMonitor:
    def __init__(self):
        self._cpu_history = deque(maxlen=60)  # one entry per reading, ~1 minute

    def evaluate(self, s: Stats):
        """Returns (score, alerts, passed): alerts sorted most serious first, and short
        descriptions of the checks that passed (shown with a tick on the display)."""
        alerts, passed = [], []

        def add(level, text):
            if level is not None:
                alerts.append(Alert(level, text[:28]))  # the display fits ~22 characters
            return level is not None

        def ok(raised, text):
            if not raised:
                passed.append(text)

        if s.cpu_temp >= 0:
            lvl = _level(s.cpu_temp, CPU_TEMP)
            ok(add(lvl, f"CPU {'very hot' if lvl == CRITICAL else 'running hot'} ({s.cpu_temp:.0f}C)"),
               f"CPU temp normal ({s.cpu_temp:.0f}C)")
        g = s.gpu
        if g.temp >= 0:
            lvl = _level(g.temp, GPU_TEMP)
            raised = add(lvl, f"GPU {'very hot' if lvl == CRITICAL else 'running hot'} ({g.temp}C)")
            if g.hotspot >= 0:
                raised |= add(_level(g.hotspot, GPU_HOTSPOT), f"GPU hotspot {g.hotspot}C")
            ok(raised, f"GPU temp normal ({g.temp}C)")

        if s.ram >= 0:
            lvl = _level(s.ram, RAM)
            ok(add(lvl, f"Memory {s.ram}% {'full' if lvl == CRITICAL else 'used'}"),
               f"Memory OK ({s.ram}% used)")

        raised = False
        for d in s.drives:
            lvl = _level(d.used_pct, DRIVE_FULL)
            raised |= add(lvl, f"Drive {d.name.rstrip(':')} {d.used_pct:.0f}% full")
        if s.drives:
            ok(raised, "Drive space OK")
        raised = False
        for d in s.disks:
            brand = d.name.split()[0] if d.name else "Drive"   # keeps alerts short
            if d.temp >= 0:
                raised |= add(_level(d.temp, DRIVE_TEMP), f"{brand} drive hot ({d.temp:.0f}C)")
            if d.life >= 0:
                lvl = CRITICAL if d.life <= DRIVE_LIFE[1] else WARNING if d.life <= DRIVE_LIFE[0] else None
                raised |= add(lvl, f"{brand} worn ({d.life:.0f}% life)")
        if s.disks:
            ok(raised, "Drive health good")

        raised = False
        for f in s.fans:
            if f.rpm == 0 and "CPU" in f.name.upper():
                raised |= add(CRITICAL, f"{f.name} fan not spinning")
            elif f.rpm == 0 and not f.name.startswith("GPU"):  # GPU fans stop at idle on purpose
                add(INFO, f"{f.name} fan stopped")
        spinning = sum(f.rpm > 0 for f in s.fans)
        if spinning:
            ok(raised, f"{spinning} fan{'s' if spinning > 1 else ''} spinning")

        if s.cpu >= 0:
            self._cpu_history.append(s.cpu)
            if len(self._cpu_history) >= 30:
                avg = sum(self._cpu_history) / len(self._cpu_history)
                if avg >= SUSTAINED_CPU:
                    add(WARNING, f"CPU at {avg:.0f}% for 1 min")

        if s.uptime_s >= LONG_UPTIME_DAYS * 86400:
            add(INFO, f"Restart soon (up {s.uptime_s // 86400}d)")
        if not s.sensors_available:
            add(INFO, "Temps need LibreHWMon")

        alerts.sort(key=lambda a: a.level, reverse=True)
        warnings = sum(a.level == WARNING for a in alerts)
        criticals = sum(a.level == CRITICAL for a in alerts)
        score = max(0, 100 - WARNING_PENALTY * warnings - CRITICAL_PENALTY * criticals)
        return score, alerts, passed
