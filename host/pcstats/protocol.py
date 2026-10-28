"""Turns a reading into the text line each display expects.

CYD dashboard: comma-separated key=value pairs. Lists use ';' between items and ':'
between an item's parts, e.g. fans=CPU:1180;Sys 1:820. The firmware ignores keys it
doesn't know and treats missing keys as unknown, so either side can be updated
without breaking the other. Lines stay under MAX_LINE (the firmware's buffer).

Nano ticker: positional cpu,ram,HH:MM.
"""
from .model import Stats

MAX_LINE = 1000  # firmware buffer is 1024 bytes


def clean(text, limit=40, in_list=False):
    """Removes the protocol's separators from text. Inside lists ';' and ':' are
    separators too; in plain values (like the time "14:12") they're fine."""
    text = str(text)
    for ch in ",=\n\r" + (";:" if in_list else ""):
        text = text.replace(ch, " ")
    return " ".join(text.split())[:limit]


def num(v):
    v = float(v)
    return str(int(v)) if v.is_integer() else f"{v:.1f}"


def _list(items):
    return ";".join(":".join(str(p) for p in item) for item in items)


def format_cyd(s: Stats) -> str:
    g = s.gpu
    fields = [
        ("cpu", s.cpu), ("cpun", s.cpu_threads), ("cput", s.cpu_temp), ("cpuw", s.cpu_power_w),
        ("cpuc", s.cpu_clock_mhz),
        ("ram", s.ram), ("ramu", s.ram_used_mb), ("ramt", s.ram_total_mb),
        ("gpu", g.util), ("gput", g.temp), ("gpuh", g.hotspot),
        ("vram", g.mem_used_mb), ("vramt", g.mem_total_mb),
        ("gpw", g.power_w), ("gpwl", g.power_limit_w), ("fan", g.fan),
        ("gclk", g.clock_mhz), ("gmclk", g.mem_clock_mhz),
        ("dn", s.down_kbs), ("up", s.up_kbs), ("disk", s.disk),
        ("dr", s.disk_read_kbs), ("dw", s.disk_write_kbs),
        ("procn", s.process_count), ("upt", s.uptime_s), ("hs", s.health_score),
    ]
    parts = [f"{k}={num(v)}" for k, v in fields if v is not None and v >= 0]
    parts.append(f"sens={1 if s.sensors_available else 0}")
    parts += [f"t={clean(s.clock, 5)}", f"d={clean(s.date, 15)}"]

    text_fields = [("gn", s.gpu_name, 23), ("cpunm", s.cpu_name, 23),
                   ("hn", s.host_name, 15), ("os", s.os_name, 15)]
    parts += [f"{k}={clean(v, n)}" for k, v, n in text_fields if v]

    lists = [
        ("cores", [(round(x),) for x in s.core_loads[:32]]),
        ("ctemps", [(round(x),) for x in s.core_temps[:16]]),
        ("fans", [(clean(f.name, 12, True), int(f.rpm)) for f in s.fans[:8]]),
        ("temps", [(clean(t.name, 12, True), num(round(t.celsius))) for t in s.temps[:8]]),
        ("procs", [(clean(p.name, 16, True), num(p.cpu), int(p.mem_mb)) for p in s.processes[:6]]),
        ("drv", [(clean(d.name, 6, True), num(round(d.used_pct)), num(round(d.total_gb)))
                 for d in s.drives[:4]]),
        ("dsk", [(clean(d.name, 20, True), num(d.temp), num(d.life)) for d in s.disks[:4]]),
        ("al", [(a.level, clean(a.text, 34, True)) for a in s.alerts[:6]]),
        ("ok", [(clean(t, 24, True),) for t in s.checks_passed[:6]]),
    ]
    for key, items in lists:
        if items:
            parts.append(f"{key}={_list(items)}")

    line = ",".join(parts)
    # Safety net: drop the least important lists if something made the line too long
    for key in ("ok", "ctemps", "temps", "dsk", "procs", "cores"):
        if len(line) < MAX_LINE:
            break
        parts = [p for p in parts if not p.startswith(key + "=")]
        line = ",".join(parts)
    return line[:MAX_LINE] + "\n"


def format_nano(s: Stats) -> str:
    return f"{s.cpu},{s.ram},{s.clock}\n"


BOARDS = {
    "cyd": ("ESP32 CYD dashboard", format_cyd),
    "nano": ("Arduino Nano OLED ticker", format_nano),
}
