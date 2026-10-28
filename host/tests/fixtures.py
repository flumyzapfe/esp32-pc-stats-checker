"""Shared test data: a full sample reading and LibreHardwareMonitor JSON trees
shaped like the real data.json (current and older formats)."""
from pcstats.model import (INFO, WARNING, Alert, DiskHealth, Drive, Fan, GpuSample,
                           Process, Stats, TempSensor)

SAMPLE = Stats(
    cpu=37, ram=61, down_kbs=12697, up_kbs=380, disk=48, clock="14:12", date="Wed 28 Oct",
    cpu_threads=16, cpu_name="Ryzen 7 5800X",
    core_loads=[42, 18, 65, 12, 30, 9, 77, 25, 14, 33, 51, 8, 20, 44, 16, 29],
    ram_used_mb=9984, ram_total_mb=16310, disk_read_kbs=2048, disk_write_kbs=512,
    drives=[Drive("C:", 79.2, 931.5), Drive("D:", 41.0, 1863.0)],
    processes=[Process("chrome", 12.4, 2310), Process("Discord", 3.1, 412),
               Process("explorer", 1.2, 156)],
    process_count=287, uptime_s=3 * 86400 + 4 * 3600, host_name="jasperpc", os_name="Windows 11",
    gpu=GpuSample(util=72, temp=64, hotspot=76, mem_used_mb=3120, mem_total_mb=8192,
                  power_w=145.5, power_limit_w=215.0, fan=45, clock_mhz=1905, mem_clock_mhz=7000),
    gpu_name="RTX 2070 SUPER",
    sensors_available=True, cpu_temp=67.5, cpu_power_w=88.2, cpu_clock_mhz=4450,
    core_temps=[],
    fans=[Fan("CPU", 1180), Fan("Sys 1", 820), Fan("GPU 1", 1450)],
    temps=[TempSensor("Motherboard", 38), TempSensor("VRM MOS", 52)],
    disks=[DiskHealth("Samsung 970 EVO", 41, 98)],
    health_score=88,
    alerts=[Alert(WARNING, "Drive C filling up (91%)"), Alert(INFO, "Up 3 days - restart soon")],
    checks_passed=["CPU temp normal (68C)", "GPU temp normal (64C)", "Memory OK (61% used)"],
)

_ids = iter(range(1, 10_000))


def sensor(name, stype, raw, unit, hw_id, idx, raw_max=None, legacy=False, decimal_comma=False):
    text = f"{raw:.1f}"
    if decimal_comma:
        text = text.replace(".", ",")
    node = {"id": next(_ids), "Text": name, "Min": "", "Value": f"{text} {unit}".strip(),
            "Max": f"{raw_max if raw_max is not None else raw:.1f} {unit}".strip(),
            "ImageURL": "images/transparent.png", "Children": []}
    if not legacy:
        node.update({"SensorId": f"{hw_id}/{stype.lower()}/{idx}", "Type": stype,
                     "RawMin": raw, "RawValue": raw, "RawMax": raw if raw_max is None else raw_max})
    return node


def group(text, children):
    return {"id": next(_ids), "Text": text, "Min": "", "Value": "", "Max": "",
            "ImageURL": "images_icon/group.png", "Children": children}


def hardware(text, hw_id, icon, children, legacy=False):
    node = {"id": next(_ids), "Text": text, "Min": "", "Value": "", "Max": "",
            "ImageURL": f"images_icon/{icon}", "Children": children}
    if not legacy:
        node["HardwareId"] = hw_id
    return node


def root(children):
    return {"id": 0, "Text": "Sensor", "Min": "Min", "Value": "Value", "Max": "Max",
            "ImageURL": "", "Children": [
                {"id": 1, "Text": "JASPERPC", "Min": "", "Value": "", "Max": "",
                 "ImageURL": "images_icon/computer.png", "Children": children}]}


def intel_nvidia_tree():
    """Intel CPU with per-core temps, Nuvoton Super I/O, NVIDIA GPU, NVMe drive."""
    cpu = "/intelcpu/0"
    lpc = "/lpc/nct6798d/0"
    gpu = "/gpu-nvidia/0"
    nvme = "/nvme/0"
    return root([
        hardware("ASUS ROG STRIX Z390-F GAMING", "/motherboard", "mainboard.png", [
            hardware("Nuvoton NCT6798D", lpc, "chip.png", [
                group("Temperatures", [
                    sensor("CPU", "Temperature", 45.0, "°C", lpc, 0),
                    sensor("Motherboard", "Temperature", 38.0, "°C", lpc, 1),
                    sensor("Temperature #3", "Temperature", -128.0, "°C", lpc, 2),   # not connected
                    sensor("VRM MOS", "Temperature", 52.0, "°C", lpc, 3),
                ]),
                group("Fans", [
                    sensor("CPU Fan", "Fan", 1180.0, "RPM", lpc, 0),
                    sensor("System Fan #1", "Fan", 820.0, "RPM", lpc, 1),
                    sensor("Fan #3", "Fan", 0.0, "RPM", lpc, 2, raw_max=0.0),       # empty header
                    sensor("Fan #4", "Fan", 0.0, "RPM", lpc, 3, raw_max=900.0),     # stopped
                ]),
            ]),
        ]),
        hardware("Intel Core i7-9700K", cpu, "cpu.png", [
            group("Clocks", [
                sensor("Bus Speed", "Clock", 100.0, "MHz", cpu, 0),
                sensor("CPU Core #1", "Clock", 4600.0, "MHz", cpu, 1),
                sensor("CPU Core #2", "Clock", 4400.0, "MHz", cpu, 2),
            ]),
            group("Temperatures", [
                sensor("CPU Core #1", "Temperature", 66.0, "°C", cpu, 0),
                sensor("CPU Core #2", "Temperature", 69.0, "°C", cpu, 1),
                sensor("CPU Package", "Temperature", 71.0, "°C", cpu, 2),
                sensor("Core Max", "Temperature", 69.0, "°C", cpu, 3),
            ]),
            group("Load", [sensor("CPU Total", "Load", 37.0, "%", cpu, 0)]),
            group("Powers", [
                sensor("CPU Package", "Power", 95.4, "W", cpu, 0),
                sensor("CPU Cores", "Power", 80.1, "W", cpu, 1),
            ]),
        ]),
        hardware("NVIDIA GeForce RTX 2070 SUPER", gpu, "nvidia.png", [
            group("Temperatures", [
                sensor("GPU Core", "Temperature", 64.0, "°C", gpu, 0),
                sensor("GPU Hot Spot", "Temperature", 77.0, "°C", gpu, 1),
            ]),
            group("Fans", [sensor("GPU Fan 1", "Fan", 1450.0, "RPM", gpu, 0),
                           sensor("GPU Fan 2", "Fan", 1440.0, "RPM", gpu, 1)]),
            group("Load", [sensor("GPU Core", "Load", 72.0, "%", gpu, 0)]),
        ]),
        hardware("Samsung SSD 970 EVO Plus 1TB", nvme, "hdd.png", [
            group("Temperatures", [sensor("Composite Temperature", "Temperature", 41.0, "°C", nvme, 0),
                                   sensor("Temperature #2", "Temperature", 48.0, "°C", nvme, 1)]),
            group("Levels", [sensor("Available Spare", "Level", 100.0, "%", nvme, 0),
                             sensor("Percentage Used", "Level", 2.0, "%", nvme, 1)]),
        ]),
    ])


def amd_radeon_tree():
    """AMD CPU (no per-core temps, Tctl/Tdie), AMD Radeon GPU, SATA SSD with Remaining Life."""
    cpu = "/amdcpu/0"
    gpu = "/gpu-amd/0"
    ssd = "/ssd/1"
    return root([
        hardware("AMD Ryzen 7 5800X", cpu, "cpu.png", [
            group("Clocks", [sensor("Cores (Average)", "Clock", 4450.2, "MHz", cpu, 0),
                             sensor("Core #1", "Clock", 4500.0, "MHz", cpu, 1)]),
            group("Temperatures", [sensor("Core (Tctl/Tdie)", "Temperature", 67.5, "°C", cpu, 0),
                                   sensor("CCD1 (Tdie)", "Temperature", 63.0, "°C", cpu, 1)]),
            group("Powers", [sensor("Package", "Power", 88.2, "W", cpu, 0)]),
        ]),
        hardware("AMD Radeon RX 6700 XT", gpu, "ati.png", [
            group("Temperatures", [sensor("GPU Core", "Temperature", 58.0, "°C", gpu, 0),
                                   sensor("GPU Hot Spot", "Temperature", 71.0, "°C", gpu, 1)]),
            group("Load", [sensor("GPU Core", "Load", 33.0, "%", gpu, 0)]),
            group("Powers", [sensor("GPU Package", "Power", 120.0, "W", gpu, 0)]),
            group("Clocks", [sensor("GPU Core", "Clock", 2400.0, "MHz", gpu, 0),
                             sensor("GPU Memory", "Clock", 2000.0, "MHz", gpu, 1)]),
            group("Data", [sensor("GPU Memory Used", "SmallData", 4096.0, "MB", gpu, 0),
                           sensor("GPU Memory Total", "SmallData", 12288.0, "MB", gpu, 1)]),
            group("Controls", [sensor("GPU Fan", "Control", 38.0, "%", gpu, 0)]),
        ]),
        hardware("Crucial MX500 500GB", ssd, "hdd.png", [
            group("Temperatures", [sensor("Temperature", "Temperature", 36.0, "°C", ssd, 0)]),
            group("Levels", [sensor("Remaining Life", "Level", 87.0, "%", ssd, 0)]),
        ]),
    ])


def legacy_tree():
    """Older LibreHardwareMonitor: no HardwareId/SensorId/Type/RawValue, and a PC set to a
    locale with decimal commas."""
    return root([
        hardware("Intel Core i5-8400", None, "cpu.png", [
            group("Temperatures", [
                sensor("CPU Package", "Temperature", 58.0, "°C", "", 0, legacy=True, decimal_comma=True),
            ]),
            group("Powers", [sensor("CPU Package", "Power", 41.5, "W", "", 0, legacy=True,
                                    decimal_comma=True)]),
        ], legacy=True),
        hardware("Gigabyte B360M", None, "mainboard.png", [
            hardware("ITE IT8686E", None, "chip.png", [
                group("Fans", [sensor("Fan #1", "Fan", 950.0, "RPM", "", 0, legacy=True)]),
                group("Temperatures", [sensor("System #1", "Temperature", 33.0, "°C", "", 0,
                                              legacy=True, decimal_comma=True)]),
            ], legacy=True),
        ], legacy=True),
    ])
