# PC Stats Display

[![Build](https://github.com/flumyzapfe/esp32-pc-stats-checker/actions/workflows/build.yml/badge.svg)](https://github.com/flumyzapfe/esp32-pc-stats-checker/actions/workflows/build.yml)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20Arduino-blue)
![Python](https://img.shields.io/badge/python-3.9%2B-blue)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A small desk display that shows live stats from your computer: CPU, memory, GPU, network, disk and the time.

A Python script on the PC collects the numbers and streams them over USB to a microcontroller, which draws them on its screen. The main build runs on a £10 ESP32 touchscreen board and needs **no soldering and no wiring**.

<p align="center">
  <img src="docs/photo.jpg" alt="The dashboard running on an ESP32 Cheap Yellow Display, showing the clock page" width="560">
</p>

## Features

- **Three pages, tap to switch:** an overview of all stats, rolling 2.5-minute CPU/memory graphs, and a big clock
- **NVIDIA GPU usage and temperature**, read through NVIDIA's driver library (NVML)
- **Status LED** on the board glows green, yellow or red with CPU load
- **Flicker-free drawing:** each frame is rendered off-screen and pushed in one go
- **Plug and play:** finds the board automatically, waits if it isn't plugged in, and reconnects if it's unplugged
- **Windows launcher** that installs the Python dependencies on first run
- **Second, smaller build** for an Arduino Nano with a 0.91" OLED

![The three dashboard pages: overview, history and clock](docs/preview.png)

## Quick start

You need an **ESP32-2432S028** ("Cheap Yellow Display") board, a USB data cable, the [Arduino IDE](https://www.arduino.cc/en/software) and [Python 3.9+](https://www.python.org/downloads/).

**1. Download the project.** Click the green **Code** button above, then **Download ZIP**, and unzip it. Or use git:

```bash
git clone https://github.com/flumyzapfe/esp32-pc-stats-checker.git
```

**2. Put the dashboard on the board.**

1. In the Arduino IDE, open **Boards Manager**, search `esp32` and install **esp32 by Espressif Systems**.
2. Open **Library Manager**, search `LovyanGFX` and install it.
3. Open `firmware/cyd_dashboard/cyd_dashboard.ino`.
4. Select board **ESP32 Dev Module** and your board's port, then click **Upload**. If it stalls on `Connecting...`, hold the **BOOT** button on the back of the board.

The screen should then say **Waiting for PC...**

**3. Start the PC side.**

- **Windows:** double-click `host/run_dashboard.bat`. The first time, it installs the Python libraries it needs.
- **macOS / Linux:**
  ```bash
  cd host
  pip install -r requirements.txt
  python3 pc_stats.py
  ```

The dashboard comes alive within a couple of seconds. Tap the screen to switch pages.

## Hardware

| | **CYD Dashboard** (main build) | **Nano OLED Ticker** |
|---|---|---|
| Board | ESP32-2432S028 "Cheap Yellow Display" | Arduino Nano (ATmega328P) |
| Screen | 2.8" 320×240 colour touchscreen | 0.91" 128×32 monochrome OLED |
| Shows | CPU, memory, disk, GPU + temp, network, history graphs, clock | CPU, memory, time |
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
+-------------------------+   USB serial, 115200 baud    +-------------------------+
|  PC: host/pc_stats.py   | ---------------------------> |  ESP32 / Arduino        |
|  psutil + NVML          |   one CSV line per second    |  parse line -> redraw   |
+-------------------------+                              +-------------------------+
```

**PC side (`host/pc_stats.py`)**
- `StatsCollector` reads CPU, memory and disk with [psutil](https://github.com/giampaolo/psutil). It works out network speed from the change in byte counters since the last reading.
- `GpuReader` reads NVIDIA GPU usage and temperature through NVML ([nvidia-ml-py](https://pypi.org/project/nvidia-ml-py/)). If that isn't available it falls back to `nvidia-smi`, and without an NVIDIA GPU it reports `-1` (shown as "n/a").
- Each board has a small formatter function that turns a reading into its line format.
- The main loop finds the board's USB-serial port by name (CH340, CP210x and so on), sends a line every second, and reconnects if the board disappears.

**Board side (`firmware/`)**
- Serial input is buffered into a fixed-size line buffer and parsed when a newline arrives. Lines that don't split into the expected number of fields are ignored.
- The CYD draws each frame into a 320×240 8-bit sprite in RAM, then pushes it to the screen in one go, so there's no flicker.
- If no data arrives for 5 seconds, the display goes back to the "Waiting for PC" screen.

### Serial protocol

Both boards use plain text, so you can test a display without the PC script by typing a line into the Arduino Serial Monitor (115200 baud, newline line ending).

| Board | Format | Example |
|---|---|---|
| CYD | `cpu,ram,gpu,gpuTemp,downKBs,upKBs,disk,HH:MM,date` | `37,61,72,64,12697,380,48,14:12,Wed 28 Oct` |
| Nano | `cpu,ram,HH:MM` | `37,61,14:12` |

`gpu` and `gpuTemp` are `-1` when no NVIDIA GPU is found.

## Host script options

```
python pc_stats.py [--board {cyd,nano}] [--port PORT] [--list-ports]

  --board        which display you're using (default: cyd)
  --port         serial port, e.g. COM5 or /dev/ttyUSB0 (default: auto-detect)
  --list-ports   list serial ports and exit
```

The Windows launchers pass extra options through, e.g. `run_dashboard.bat --port COM5`.

## Adding your own stat

As an example, here's how to show the number of running processes (`len(psutil.pids())`). You make one change on each side.

1. **PC side:** add a field to the `Stats` dataclass in `pc_stats.py`, fill it in `StatsCollector.read()`, and append it in `format_cyd()`.
2. **Board side:** in `cyd_dashboard.ino`, add the value to `struct Stats`, raise the field count in `parseLine()` (the `f[9]` array, the `n < 9` loop limit and the `n != 9` check), read the new field, and draw it in one of the `draw...()` page functions.
3. Update `test_cyd_line` in `host/test_pc_stats.py` to match the new format.

## Troubleshooting

| Problem | Fix |
|---|---|
| Window says "Python was not found" | Install Python from python.org and tick **Add python.exe to PATH** in the installer. |
| Stuck on "Waiting for the board" | Check the board is plugged in and has the dashboard uploaded. If no ports are listed, try another cable, since some only charge. If your board is listed but not picked up, use `--port COMx`. |
| "In use by another program" | Close the Arduino IDE's Serial Monitor. Only one program can use the port at a time. |
| GPU tile shows "n/a" | The startup message says whether a GPU was found. GPU stats need an NVIDIA card with its driver installed. |
| Upload stalls on `Connecting...` | Hold the **BOOT** button on the board until the upload starts. |
| Colours or orientation look wrong | See [Display settings](#display-settings-cyd). |

## Project structure

```
firmware/
  cyd_dashboard/cyd_dashboard.ino        ESP32 CYD touchscreen dashboard
  nano_oled_ticker/nano_oled_ticker.ino  Arduino Nano + SSD1306 OLED ticker
host/
  pc_stats.py                            PC script: collects stats, talks to the board
  test_pc_stats.py                       Unit tests (python -m unittest)
  run_dashboard.bat                      Windows launcher for the CYD
  run_nano_ticker.bat                    Windows launcher for the Nano
  requirements.txt
docs/                                    Images for this README
.github/workflows/build.yml              CI: compiles both sketches, runs tests
```

## Testing

Every push is checked by [GitHub Actions](.github/workflows/build.yml):
- Both sketches are compiled with `arduino-cli`, for the ESP32 and for the AVR Nano.
- The host unit tests run on Linux and Windows with Python 3.9 and 3.13. They cover the serial line formats, the firmware's buffer size limit, and GPU detection with a simulated NVIDIA card.

To run the tests locally:

```bash
cd host
python -m unittest -v
```

## Roadmap

- [ ] Wi-Fi mode: send stats over UDP so the display only needs power
- [ ] AMD/Intel GPU support (e.g. via LibreHardwareMonitor)
- [ ] Per-core CPU load and CPU temperature
- [ ] Touch-adjustable screen brightness
- [ ] 3D-printed stand

## Built with

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) for the CYD display and touch
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) and [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) for the OLED
- [psutil](https://github.com/giampaolo/psutil), [pySerial](https://github.com/pyserial/pyserial) and [nvidia-ml-py](https://pypi.org/project/nvidia-ml-py/) on the PC

## License

[MIT](LICENSE)
