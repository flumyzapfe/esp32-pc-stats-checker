// PC Stats Dashboard for the ESP32-2432S028 "Cheap Yellow Display" (CYD)
// https://github.com/flumyzapfe/esp32-pc-stats-checker
//
// Receives one line of stats per second from host/pc_stats.py over USB serial and
// shows them on the 320x240 touchscreen as a "health tablet" for the PC:
//   Overview  - CPU / GPU / RAM gauges, network and disk
//   Health    - 0-100 health score and alerts (hot parts, full drives, stopped fans...)
//   CPU       - temperature, clock, power, per-core load
//   GPU       - load, temperature, hotspot, VRAM, power, fan, clocks
//   Thermals  - every temperature sensor and fan speed
//   Processes - top programs by CPU and memory
//   Storage   - drive space, read/write speed, drive temperature and wear
//   History   - 2.5-minute CPU, GPU and RAM graphs
//   Network   - download and upload graphs
//   Clock     - big clock with key stats
// Touch: right half = next page, left half = previous, the title bar = page menu.
// The page is remembered across restarts. The onboard RGB LED shows overall health.
//
// Files in this sketch:
//   cyd_dashboard.ino  - hardware: display/touch setup, serial input, main loop (this file)
//   dashboard_ui.h     - all drawing code (also used by tools/ui_preview on a PC)
//   dashboard_data.h   - stats model, history and the serial protocol parser
//
// Arduino IDE setup: board package "esp32" by Espressif, library "LovyanGFX",
// board "ESP32 Dev Module". Tested on the dual-USB (USB-C + micro-USB) ST7789 board.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Preferences.h>

#include "dashboard_data.h"
#include "dashboard_ui.h"

// ---------- Settings: change these if the screen looks wrong ----------
#define PANEL_ST7789    1      // 1 = ST7789 (dual-USB boards like the one in the photo), 0 = ILI9341
#define INVERT_COLORS   false  // flip to true if the background is white instead of black
#define SWAP_RED_BLUE   false  // flip to true if the "R" swatch on the start screen looks blue
#define SCREEN_ROTATION 1      // 1 or 3 = landscape. Try 3 if upside down
#define TOUCH_FLIP_X    false  // flip to true if tapping right goes back instead of forward

#define DATA_TIMEOUT_MS 5000   // show "Waiting for PC" if no data for this long
#define DIM_AFTER_MS    60000  // dim the backlight after this long without data
#define FRAME_MS        33     // ~30 fps while animating

// RGB LED pins (the LED lights when a pin is LOW). If "green" shows as blue, swap G and B.
const int LED_R = 4, LED_G = 16, LED_B = 17;

// ---------- Display + touch hardware for the CYD ----------
class LGFX : public lgfx::LGFX_Device {
#if PANEL_ST7789
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto c = _bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 40000000;
      c.freq_read = 16000000;
      c.spi_3wire = false;
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = 14;
      c.pin_mosi = 13;
      c.pin_miso = 12;
      c.pin_dc = 2;
      _bus.config(c);
      _panel.setBus(&_bus);
    }
    {
      auto c = _panel.config();
      c.pin_cs = 15;
      c.pin_rst = -1;
      c.pin_busy = -1;
      c.panel_width = 240;
      c.panel_height = 320;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.readable = false;
      c.invert = INVERT_COLORS;
      c.rgb_order = SWAP_RED_BLUE;
      c.dlen_16bit = false;
      c.bus_shared = false;
      _panel.config(c);
    }
    {
      auto c = _light.config();  // backlight on GPIO 21, dimmable with PWM
      c.pin_bl = 21;
      c.invert = false;
      c.freq = 12000;
      c.pwm_channel = 7;
      _light.config(c);
      _panel.setLight(&_light);
    }
    {
      auto c = _touch.config();
      c.x_min = 300;
      c.x_max = 3900;
      c.y_min = 300;
      c.y_max = 3900;
      c.pin_int = 36;
      c.bus_shared = false;
      c.offset_rotation = 0;
      c.spi_host = SPI3_HOST;
      c.freq = 1000000;
      c.pin_sclk = 25;
      c.pin_mosi = 32;
      c.pin_miso = 39;
      c.pin_cs = 33;
      _touch.config(c);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

LGFX lcd;
LGFX_Sprite strip(&lcd);  // one horizontal band of the screen, reused for each band
int stripH = 0;           // 0 = no sprite memory, draw straight to the screen
ui::State st;
Preferences prefs;

char lineBuf[LINE_MAX_LEN];
size_t lineLen = 0;
uint32_t lastData = 0, lastFrame = 0, lastTap = 0;
bool wasTouched = false, dirty = true;
int savedPage = -1;
uint8_t brightness = 0;

void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// LED: health score if the PC sends one, otherwise CPU load
void updateLed(const Metrics& m) {
  bool green, yellow;
  if (m.hasHealth()) { green = m.healthScore >= 90; yellow = m.healthScore >= 70; }
  else { green = m.cpu < 50; yellow = m.cpu < 80; }
  if (green) setLed(false, true, false);
  else if (yellow) setLed(true, true, false);
  else setLed(true, false, false);
}

void setBacklight(uint8_t level) {
  if (level != brightness) {
    brightness = level;
    lcd.setBrightness(level);
  }
}

void render() {
  if (stripH == 0) {  // fallback: draw directly (may flicker)
    ui::Canvas c{&lcd, 0, ui::H};
    ui::drawFrame(c, st);
    return;
  }
  for (int oy = 0; oy < ui::H; oy += stripH) {
    ui::Canvas c{&strip, oy, stripH};
    ui::drawFrame(c, st);
    strip.pushSprite(0, oy);
  }
}

void handleLine(char* line) {
  static Metrics m;  // ~1.3 KB, kept off the stack
  if (!proto::parseLine(line, m)) return;
  st.m = m;
  st.hist.add(m);
  if (!st.connected) {
    st.s = ui::Smoothed();  // gauges sweep up from zero on (re)connect
    st.connected = true;
  }
  lastData = millis();
  updateLed(m);
  dirty = true;
}

void readSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n') {
      lineBuf[lineLen] = '\0';
      handleLine(lineBuf);
      lineLen = 0;
    } else if (ch != '\r' && lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = ch;
    }
  }
}

void readTouch() {
  int32_t x, y;
  bool touched = lcd.getTouch(&x, &y);
  if (touched && !wasTouched && millis() - lastTap > 250) {
    if (TOUCH_FLIP_X) x = ui::W - 1 - x;
    if (st.menuOpen) {                       // pick a page, or tap outside to close
      int hit = ui::menuHit(x, y);
      if (hit >= 0) st.page = hit;
      st.menuOpen = false;
    } else if (y < ui::HEADER_H) {           // title bar opens the page menu
      st.menuOpen = st.connected;
    } else {
      int dir = (x >= ui::W / 2) ? 1 : -1;
      st.page = (st.page + dir + ui::PAGE_COUNT) % ui::PAGE_COUNT;
    }
    if (st.page != savedPage) {              // save to flash only when it changes
      savedPage = st.page;
      prefs.putUChar("page", (uint8_t)savedPage);
    }
    lastTap = millis();
    dirty = true;
  }
  wasTouched = touched;
}

void setup() {
  Serial.setRxBufferSize(2048);  // lines are ~600 bytes and arrive while a frame is drawn
  Serial.begin(115200);

  pinMode(27, OUTPUT);  // backlight on some board revisions
  digitalWrite(27, HIGH);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, false);

  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);
  setBacklight(255);

  // Largest band that fits in RAM: 320x120 at 16-bit colour is 75 KB
  strip.setColorDepth(16);
  const int heights[] = {120, 80, 60, 40, 30};
  for (int h : heights) {
    if (strip.createSprite(ui::W, h)) { stripH = h; break; }
  }

  prefs.begin("dashboard", false);
  st.page = savedPage = prefs.getUChar("page", 0) % ui::PAGE_COUNT;

  Serial.printf("PC Stats Dashboard ready (strip height %d)\n", stripH);
}

void loop() {
  readSerial();
  readTouch();

  uint32_t now = millis();
  if (st.connected && now - lastData > DATA_TIMEOUT_MS) {
    st.connected = false;
    setLed(false, false, false);
    dirty = true;
  }
  uint32_t lastActive = lastData > lastTap ? lastData : lastTap;  // a tap wakes the screen
  setBacklight(!st.connected && now - lastActive > DIM_AFTER_MS ? 40 : 255);

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    bool animating = st.connected && st.s.step(st.m);
    if (dirty || animating || ui::needsAnimation(st)) {  // spinner, spinning fans
      st.ms = now;
      render();
      dirty = false;
    }
  }
  delay(2);
}
