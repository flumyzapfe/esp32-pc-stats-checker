# PC Stats Display

[![Build](https://github.com/flumyzapfe/esp32-pc-stats-checker/actions/workflows/build.yml/badge.svg)](https://github.com/flumyzapfe/esp32-pc-stats-checker/actions/workflows/build.yml)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20Arduino-blue)
![Python](https://img.shields.io/badge/python-3.9%2B-blue)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A pocket-sized health monitor for your PC. A £10 ESP32 touchscreen sits on your desk and shows live CPU, GPU and memory load, temperatures, fan speeds, the programs using the most resources, drive space and wear, network traffic, and an overall **health score** with plain-English alerts like "CPU very hot (96C)" or "Drive C 97% full".

A Python script on the PC collects everything and streams it over USB. The display needs **no soldering and no wiring**.

![All twelve screens: overview, health, CPU, GPU, thermals, processes, storage, history, network, clock, page menu and the waiting screen](docs/screens.png)
<sub>Rendered from the actual firmware drawing code by [tools/ui_preview](tools/ui_preview), using sample data.</sub>

<p align="center">
  <img src="docs/photo.jpg" alt="The dashboard running on an ESP32 Cheap Yellow Display" width="440">
</p>

## Features

- **Ten pages:** overview, health, CPU, GPU, thermals, processes, storage, history, network and clock. Tap the right or left side of the screen to go forward or back. Tap the title bar, or press and hold anywhere, for a menu of every page. The display remembers your page after a restart.
- **Health score and alerts:** the PC checks temperatures, memory, drive space and wear, fans and sustained load. It gives a 0–100 score with a reason for every lost point, and lists the checks that passed. The score shows on every page, and the board's LED glows green, yellow or red to match.
- **Thermals:** CPU package and per-core temperatures, GPU core and hotspot, motherboard and VRM sensors, and drive temperatures. Fans are listed with their RPM and an icon that spins at a matching speed.
- **CPU and GPU detail:** clocks, power draw, load on each core, VRAM, GPU fan. NVIDIA cards are read directly, and AMD and Intel graphics through LibreHardwareMonitor.
- **Mini Task Manager:** the busiest programs by CPU and memory, with the many processes of a program like Chrome grouped together.
- **Storage:** space on each drive, read/write speed, and SSD wear (percentage of life left).
- **Polished UI:** full colour with anti-aliased fonts, and gauges that animate smoothly between readings, all drawn without flicker on a microcontroller with 320 KB of RAM.
- **Plug and play:** the PC script finds the board automatically, reconnects if it's unplugged, and installs its own dependencies on Windows.
- **Second, smaller build** for an Arduino Nano with a 0.91" OLED.

## Quick start

You need an **ESP32-2432S028** ("Cheap Yellow Display") board, a USB data cable, the [Arduino IDE](https://www.arduino.cc/en/software) and [Python 3.9+](https://www.python.org/downloads/).

**1. Download the project.** Click the green **Code** button above, then **Download ZIP**, and unzip it. Or use git:

```bash
git clone https://github.com/flumyzapfe/esp32-pc-stats-checker.git
```

**2. Put the dashboard on the board.**

1. In the Arduino IDE, open **Boards Manager**, search `esp32` and install **esp32 by Espressif Systems**.
2. Open **Library Manager**, search `LovyanGFX` and install it.
3. Open `firmware/cyd_dashboard/cyd_dashboard.ino`. Two more tabs open with it; they're part of the sketch.
4. Select board **ESP32 Dev Module** and your board's port, then click **Upload**. If it stalls on `Connecting...`, hold the **BOOT** button on the back of the board.

On first start the board asks you to **tap four arrows**, one in each corner, to calibrate the touchscreen (a fingernail works best). Then the screen says **Waiting for PC...**

**3. Turn on temperature and fan sensors (Windows, recommended).** Windows doesn't let normal programs read CPU temperatures or fan speeds, so the script gets them from [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor), a free, open-source monitoring tool.

1. Download the latest release from its [releases page](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases), unzip it and run `LibreHardwareMonitor.exe`. It asks for administrator permission, which it needs to read the sensors.
2. In its **Options** menu, tick **Remote Web Server → Run**. This lets the script read the sensors at `http://localhost:8085`.
3. Optionally tick **Run On Windows Startup**, **Start Minimized** and **Minimize To Tray**, so it's always running in the background.

Everything except temperatures, fans and drive wear works without it.

**4. Start the PC side.**

- **Windows:** double-click `host/run_dashboard.bat`. The first time, it installs the Python libraries it needs. To see everything your PC exposes, double-click `host/check_sensors.bat`.
- **macOS / Linux:**
  ```bash
  cd host
  pip install -r requirements.txt
  python3 pc_stats.py
  ```

The dashboard comes alive within a couple of seconds.

## The pages

| Page | Shows |
|---|---|
| **Overview** | CPU, GPU and RAM gauges with temperatures, network speed with a mini graph, system drive |
| **Health** | Health score, alerts (critical in red, warnings in orange, tips in blue), checks that passed, PC name, OS, uptime |
| **CPU** | Model, temperature gauge and 2.5-minute temperature graph, load, clock, power, a bar for every thread |
| **GPU** | Load gauge, model, core and memory clocks, temperature, VRAM, power draw, fan |
| **Thermals** | Every temperature sensor with a colour-coded bar, every fan with its RPM and a spinning icon |
| **Processes** | Top six programs by CPU, with CPU % and memory, and the total process count |
| **Storage** | Space on each drive, read/write speed, drive temperature and life remaining |
| **History** | 2.5-minute graphs of CPU, GPU and RAM load |
| **Network** | 2.5-minute download and upload graphs with peaks |
| **Clock** | Big clock, date, uptime, health score and four key stats |

### Health checks

| Check | Warning | Critical |
|---|---|---|
| CPU temperature | 80 °C | 90 °C |
| GPU temperature / hotspot | 80 °C / 95 °C | 88 °C / 102 °C |
| Memory used | 85% | 95% |
| Drive space used | 90% | 95% |
| Drive temperature | 60 °C | 70 °C |
| SSD life remaining | 25% | 10% |
| CPU fan | | stopped |
| CPU load | 90%+ for a minute | |

The score starts at 100 and loses 12 points per warning and 30 per critical alert, so it's always clear why it isn't 100. Tips (blue) such as "Restart soon (up 9d)" don't cost points. GPU fans stopping at idle is normal, so that never raises an alert. The thresholds are in [`host/pcstats/health.py`](host/pcstats/health.py).

## Hardware

| | **CYD Dashboard** (main build) | **Nano OLED Ticker** |
|---|---|---|
| Board | ESP32-2432S028 "Cheap Yellow Display" | Arduino Nano (ATmega328P) |
| Screen | 2.8" 320×240 colour touchscreen | 0.91" 128×32 monochrome OLED |
| Shows | Everything above | CPU, memory, time |
| Wiring | None, everything is on the board | 4 jumper wires |
| Approx. cost | ~£10–15 | ~£5 |

### Display settings (CYD)

CYD boards are sold in a few variants. The start screen shows red, green and blue test swatches, so you can tell straight away if anything needs changing. If it does, edit the settings at the top of `cyd_dashboard.ino` and re-upload:

| What you see | Setting to change |
|---|---|
| White background instead of black | `INVERT_COLORS` → `true` |
| The "R" swatch looks blue | `SWAP_RED_BLUE` → `true` |
| Blank screen (common on single-USB boards) | `PANEL_ST7789` → `0` (uses the ILI9341 driver) |
| Upside down | `SCREEN_ROTATION` → `3` |

The defaults work on the dual-USB (USB-C + micro-USB) board shown in the photo.

### Nano OLED Ticker

| OLED pin | Nano pin |
|---|---|
| GND | GND |
| VCC | 5V |
| SCL | A5 |
| SDA | A4 |

1. Install the **Adafruit SSD1306** library, and accept the prompt to also install its dependencies.
2. Upload `firmware/nano_oled_ticker/nano_oled_ticker.ino` with board **Arduino Nano**. Many clone boards need processor **ATmega328P (Old Bootloader)** and the CH340 USB driver.
3. Double-click `host/run_nano_ticker.bat`, or run `python pc_stats.py --board nano`.

## How it works

```
+---------------------------------+                              +--------------------------+
|  PC: host/pc_stats.py           |   USB serial, 115200 baud    |  ESP32 dashboard         |
|  psutil, NVML,                  | ---------------------------> |  parse line -> animate   |
|  LibreHardwareMonitor (HTTP)    |   one text line per second   |  -> draw in 2 strips     |
|  -> health checks -> 1 line     |                              |                          |
+---------------------------------+                              +--------------------------+
```

**PC side (`host/`)**

`pc_stats.py` is the command-line entry point and serial loop. The work is done by the `pcstats` package:

| Module | Job |
|---|---|
| `system.py` | psutil: per-core load, memory, drives, disk and network speed, top processes, uptime, CPU model, OS |
| `gpu.py` | NVIDIA GPU through NVML ([nvidia-ml-py](https://pypi.org/project/nvidia-ml-py/)), falling back to `nvidia-smi` |
| `lhm.py` | Reads LibreHardwareMonitor's `data.json`: flattens its sensor tree, picks the right CPU temperature for Intel or AMD, filters out unconnected sensors and empty fan headers, and supports older versions that send only formatted text |
| `health.py` | Health checks, score, alerts and passed checks |
| `protocol.py` | Builds each display's line format, guaranteeing it stays under the firmware's 1 KB buffer |
| `collector.py` | Combines all sources once a second. Slow sources (processes, drive space) refresh less often, and an unreachable LibreHardwareMonitor is only retried every 15 s so it never slows readings down. |

**Board side (`firmware/cyd_dashboard/`)**

| File | Job |
|---|---|
| `cyd_dashboard.ino` | Display, touch and backlight setup, serial input, touch navigation, the main loop |
| `dashboard_data.h` | Stats model, history ring buffers and the protocol parser. Plain C++, no hardware. |
| `dashboard_ui.h` | Everything that gets drawn: theme, widgets (ring gauges, bars, area graphs, fan icons, menu icons) and all the pages |

- **Rendering:** a full 320×240 frame at 16-bit colour needs 150 KB, more than the ESP32 has free in one block. So each frame is drawn twice into a 320×120 sprite (75 KB), once per half of the screen, and each half is pushed to the display in one go. Pages draw through a `Canvas` that shifts coordinates into the current strip, so page code never has to know about strips.
- **Animation:** displayed values move a fraction of the way to the latest reading on each frame (about 30 fps). The screen only redraws while something is changing, or while fans are spinning on the Thermals page.
- **Robustness:** serial input goes into a fixed 1 KB buffer, malformed lines and list items are dropped, and text is shortened to fit its space on screen. If no data arrives for 5 seconds, the display goes back to the "Waiting for PC" screen, and it dims after a minute.

Because the drawing code doesn't depend on the hardware, [tools/ui_preview](tools/ui_preview) builds it on a normal PC with LovyanGFX's desktop back end and renders each page to PNG. That includes awkward cases like a PC in trouble, a laptop without sensors, or an older PC script. The screenshots above come from it.

### Serial protocol

Plain text, so you can test a display without the PC script by typing a line into the Arduino Serial Monitor (115200 baud, newline line ending).

**CYD:** comma-separated `key=value` pairs. Lists use `;` between items and `:` within an item. The firmware ignores keys it doesn't know and treats missing keys as unknown, so new stats can be added on one side without breaking the other. Only `cpu` is required. A typical line is about 650 bytes:

```
cpu=37,cpun=16,cput=67.5,cpuw=88.2,cpuc=4450,ram=61,ramu=9984,ramt=16310,gpu=72,gput=64,gpuh=76,
vram=3120,vramt=8192,gpw=145.5,gpwl=215,fan=45,gclk=1905,gmclk=7000,dn=12697,up=380,disk=48,
dr=2048,dw=512,procn=287,upt=273600,hs=88,sens=1,t=14:12,d=Wed 28 Oct,gn=RTX 2070 SUPER,
cpunm=Ryzen 7 5800X,hn=jasperpc,os=Windows 11,cores=42;18;65;12;...,fans=CPU:1180;Sys 1:820,
temps=Motherboard:38;VRM MOS:52,procs=chrome:12.4:2310;Discord:3.1:412,drv=C:79:932;D:41:1863,
dsk=Samsung 970 EVO:41:98,al=1:Drive C 91% full,ok=CPU temp normal (68C);Memory OK (61% used)
```
<sub>(Shown wrapped; it's sent as one line.)</sub>

| Key | Meaning | Key | Meaning |
|---|---|---|---|
| `cpu` / `cput` | CPU load % / temperature °C | `dn` / `up` | Network down / up, KB/s |
| `cpuw` / `cpuc` | CPU power W / average clock MHz | `disk` | System drive used % |
| `cpun` / `cpunm` | CPU threads / model | `dr` / `dw` | Disk read / write, KB/s |
| `cores` / `ctemps` | Load % / temperature per core (lists) | `drv` | Drives: `name:used%:sizeGB` |
| `ram` / `ramu` / `ramt` | Memory used %, used MB, total MB | `dsk` | Drive health: `model:tempC:life%` |
| `gpu` / `gput` / `gpuh` | GPU load %, temperature, hotspot | `fans` | Fans: `name:rpm` |
| `vram` / `vramt` | VRAM used / total, MB | `temps` | Other sensors: `name:tempC` |
| `gpw` / `gpwl` | GPU power / limit, W | `procs` | Top programs: `name:cpu%:memMB` |
| `fan` | GPU fan % | `procn` | Number of processes |
| `gclk` / `gmclk` | GPU core / memory clock, MHz | `hs` | Health score 0–100 |
| `gn` | GPU name | `al` | Alerts: `level:text` (0 tip, 1 warning, 2 critical) |
| `t` / `d` | Time `HH:MM` / date text | `ok` | Checks that passed |
| `hn` / `os` | PC name / OS | `upt` | Uptime, seconds |
| `sens` | 1 if LibreHardwareMonitor is connected | | |

The original positional format (`cpu,ram,gpu,gpuTemp,down,up,disk,HH:MM,date`) is still accepted.

**Nano:** `cpu,ram,HH:MM`, e.g. `37,61,14:12`

## Host script options

```
python pc_stats.py [--board {cyd,nano}] [--port PORT] [--sensors] [--list-ports] [--lhm-url URL]

  --board        which display you're using (default: cyd)
  --port         serial port, e.g. COM5 or /dev/ttyUSB0 (default: auto-detect)
  --sensors      show everything detected on this PC (and the line sent) and exit
  --list-ports   list serial ports and exit
  --lhm-url      LibreHardwareMonitor data URL (default: http://127.0.0.1:8085/data.json)
```

The Windows launchers pass extra options through, e.g. `run_dashboard.bat --port COM5`.

## Adding your own stat

As an example, here's how to show the number of running services. Because the protocol uses named keys, each side is a small, separate change.

1. **PC side:** add a field to `Stats` in `host/pcstats/model.py`, fill it in `collector.py`, and add `("svc", s.services)` to the list in `protocol.py`.
2. **Board side:** add `float services;` to `Metrics` in `dashboard_data.h` (and add it to the list set to `-1` in `clear()`), then add `{"svc", &Metrics::services}` to the key table in `setField()`.
3. **Draw it** in one of the page functions in `dashboard_ui.h`, then check how it looks by running `tools/ui_preview`. You don't need to upload anything to do that.
4. Add a check to `host/tests/test_protocol.py`.

## Troubleshooting

| Problem | Fix |
|---|---|
| Window says "Python was not found" | Install Python from python.org and tick **Add python.exe to PATH** in the installer. |
| CPU temperature shows "--" and Thermals says "needs LibreHardwareMonitor" | Install and run LibreHardwareMonitor with **Options → Remote Web Server → Run** ticked (see Quick start step 3). `check_sensors.bat` tells you whether the script can see it. |
| Some fans or sensors are missing | Not every motherboard exposes every sensor. If LibreHardwareMonitor itself doesn't show them, the dashboard can't either. |
| Stuck on "Waiting for the board" | Check the board is plugged in and has the dashboard uploaded. If no ports are listed, try another cable, since some only charge. If your board is listed but not picked up, use `--port COMx`. |
| "In use by another program" | Close the Arduino IDE's Serial Monitor. Only one program can use the port at a time. |
| Upload fails with "Could not open COMx... Access is denied" | The PC script is still using the port. Close its window, upload, then start it again. |
| Upload stalls on `Connecting...` | Hold the **BOOT** button on the board until the upload starts. |
| Taps land in the wrong place, or the menu won't open | A ring briefly shows where each tap registered. To recalibrate, open the menu (press and hold anywhere) and tap **Touch setup**, or hold a finger on the screen while plugging the board in. |
| Colours or orientation look wrong | See [Display settings](#display-settings-cyd). |

## Project structure

```
firmware/
  cyd_dashboard/
    cyd_dashboard.ino                    ESP32 CYD: hardware setup, serial, touch, main loop
    dashboard_ui.h                       Drawing: theme, widgets, pages, menu
    dashboard_data.h                     Stats model, history, protocol parser
  nano_oled_ticker/nano_oled_ticker.ino  Arduino Nano + SSD1306 OLED ticker
host/
  pc_stats.py                            Entry point: CLI, serial connection, --sensors report
  pcstats/                               Collectors, health checks, protocol (see "How it works")
  tests/                                 Unit tests and realistic sensor fixtures
  run_dashboard.bat                      Windows launcher for the CYD
  run_nano_ticker.bat                    Windows launcher for the Nano
  check_sensors.bat                      Windows: show every detected sensor
  requirements.txt
tools/ui_preview/                        Builds the UI on a PC: screenshots, C++ protocol tests
docs/                                    Images for this README
.github/workflows/build.yml              CI: compiles, tests, renders screenshots
```

## Testing

Every push is checked by [GitHub Actions](.github/workflows/build.yml):
- **Firmware:** both sketches are compiled with `arduino-cli`, for the ESP32 and for the AVR Nano.
- **Host:** 44 Python tests run on Linux and Windows with Python 3.9 and 3.13. They cover LibreHardwareMonitor parsing using realistic Intel/NVIDIA, AMD/Radeon and older-format sensor trees, the health rules, line formats and buffer limits, GPU detection with simulated NVML and `nvidia-smi`, and the collector running on the CI machine itself.
- **Protocol:** C++ tests check the firmware's parser on valid, legacy, partial, malformed and over-long input, including lists. Lines produced by the Python script, from sample data and from a live reading, are piped into that same parser, so both sides are checked against each other.
- **UI:** every page is rendered to PNG. The images are attached to each run as a download.

To run them locally:

```bash
# Python tests
cd host && python -m unittest discover -s tests -t . -v

# C++ protocol tests and screenshots (needs CMake and a C++17 compiler)
cd tools/ui_preview
git clone --depth 1 --branch 1.2.35 https://github.com/lovyan03/LovyanGFX.git
cmake -S . -B build && cmake --build build -j
./build/test_protocol
./build/ui_preview .                  # writes screen_*.png
./build/ui_preview . --edge-cases     # awkward states
./build/ui_preview . --line "cpu=..." # render a line from pc_stats.py --sensors
```

## Roadmap

- [ ] Wi-Fi mode: send stats over the network so the display only needs power
- [ ] Stand-alone mode with clock and weather when the PC is off
- [ ] Settings page on the device (brightness, which pages to show, alert thresholds)
- [ ] 3D-printed stand

## Built with

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) for the CYD display and touch
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) for temperatures, fans and drive health on Windows
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) and [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) for the OLED
- [psutil](https://github.com/giampaolo/psutil), [pySerial](https://github.com/pyserial/pyserial) and [nvidia-ml-py](https://pypi.org/project/nvidia-ml-py/) on the PC

## License

[MIT](LICENSE)
