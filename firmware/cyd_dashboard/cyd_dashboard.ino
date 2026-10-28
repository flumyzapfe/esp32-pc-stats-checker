// PC Stats Dashboard for the ESP32-2432S028 "Cheap Yellow Display"
// Receives stats from cyd_stats.py over USB. Tap the screen to switch pages.
// Needs: ESP32 board package (Espressif) + LovyanGFX library.

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ---------- Settings: change these if the screen looks wrong ----------
#define PANEL_ST7789    1      // 1 = ST7789 (dual-USB boards like yours), 0 = ILI9341 (original CYD)
#define INVERT_COLORS   false  // flip to true if the background is white instead of black
#define SWAP_RED_BLUE   false  // flip to true if the "R" swatch on the start screen looks blue
#define SCREEN_ROTATION 1      // 1 or 3 = landscape. Try 3 if upside down, 5 or 7 if mirrored

// RGB LED pins (the LED lights when a pin is LOW). If "green" shows as blue, swap G and B.
const int LED_R = 4, LED_G = 16, LED_B = 17;

// ---------- Display + touch setup for the CYD ----------
class LGFX : public lgfx::LGFX_Device {
#if PANEL_ST7789
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
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
LGFX_Sprite spr(&lcd);
LovyanGFX* g = &lcd;  // what we draw on: a full-screen sprite if it fits in memory (no flicker)
bool useSprite = false;

// Colours picked to look right in the 8-bit sprite
#define MKRGB(r, gr, b) ((uint32_t)(((r) << 16) | ((gr) << 8) | (b)))
const uint32_t C_BG    = MKRGB(0, 0, 0);
const uint32_t C_TILE  = MKRGB(36, 36, 85);
const uint32_t C_TRACK = MKRGB(0, 0, 0);
const uint32_t C_TEXT  = MKRGB(255, 255, 255);
const uint32_t C_DIM   = MKRGB(146, 146, 170);
const uint32_t C_CPU   = MKRGB(0, 219, 255);
const uint32_t C_RAM   = MKRGB(182, 109, 255);
const uint32_t C_GPU   = MKRGB(0, 219, 85);
const uint32_t C_NET   = MKRGB(255, 182, 0);
const uint32_t C_HOT   = MKRGB(255, 36, 0);

// ---------- Data ----------
struct Stats {
  int cpu, ram, gpu, gpuTemp, disk;
  long down, up;          // KB/s
  char clock[6];
  char date[16];
} s;

const int HIST = 150;     // 150 seconds of history
uint8_t cpuHist[HIST], ramHist[HIST];
int histCount = 0;

const int PAGES = 3;
int page = 0;
bool haveData = false;
unsigned long lastData = 0, lastTap = 0;
bool wasTouched = false;

char buf[96];
size_t len = 0;

void setLed(bool r, bool gr, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, gr ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

void pushHistory(uint8_t* h, int v) {
  if (histCount == HIST) memmove(h, h + 1, HIST - 1);
  h[histCount == HIST ? HIST - 1 : histCount] = v;
}

// Line format: cpu,ram,gpu,gpuTemp,downKBs,upKBs,disk,HH:MM,date
bool parseLine(char* line) {
  char* f[9];
  int n = 0;
  for (char* t = strtok(line, ","); t && n < 9; t = strtok(nullptr, ",")) f[n++] = t;
  if (n != 9) return false;

  s.cpu = constrain(atoi(f[0]), 0, 100);
  s.ram = constrain(atoi(f[1]), 0, 100);
  s.gpu = atoi(f[2]);       // -1 = no GPU data
  s.gpuTemp = atoi(f[3]);
  s.down = atol(f[4]);
  s.up = atol(f[5]);
  s.disk = constrain(atoi(f[6]), 0, 100);
  strlcpy(s.clock, f[7], sizeof(s.clock));
  strlcpy(s.date, f[8], sizeof(s.date));

  pushHistory(cpuHist, s.cpu);
  pushHistory(ramHist, s.ram);
  if (histCount < HIST) histCount++;

  // LED shows CPU load: green, yellow, red
  if (s.cpu < 50) setLed(false, true, false);
  else if (s.cpu < 80) setLed(true, true, false);
  else setLed(true, false, false);
  return true;
}

void fmtRate(long kbs, char* out, size_t n) {
  if (kbs >= 1024) snprintf(out, n, "%.1f MB/s", kbs / 1024.0);
  else snprintf(out, n, "%ld KB/s", kbs);
}

// ---------- Drawing ----------
void drawHeader(const char* title) {
  g->fillRect(0, 0, 320, 24, C_TILE);
  g->setFont(&fonts::Font2);
  g->setTextColor(C_TEXT);
  g->setTextDatum(textdatum_t::middle_left);
  g->drawString(title, 8, 12);
  g->setTextDatum(textdatum_t::middle_right);
  g->drawString(s.clock, 312, 12);
  for (int i = 0; i < PAGES; i++) g->fillCircle(150 + i * 10, 12, 3, i == page ? C_TEXT : C_DIM);
}

void drawTile(int x, int y, const char* label, const char* value, const char* sub, int pct, uint32_t col) {
  const int w = 154, h = 102;
  g->fillRoundRect(x, y, w, h, 8, C_TILE);
  g->setTextDatum(textdatum_t::top_left);
  g->setFont(&fonts::Font2);
  g->setTextColor(col);
  g->drawString(label, x + 10, y + 8);
  g->setFont(&fonts::Font4);
  g->setTextColor(C_TEXT);
  g->drawString(value, x + 10, y + 28);
  if (sub) {
    g->setFont(&fonts::Font2);
    g->setTextColor(C_DIM);
    g->drawString(sub, x + 10, y + 58);
  }
  if (pct >= 0) {
    g->fillRoundRect(x + 10, y + h - 18, w - 20, 8, 4, C_TRACK);
    int fw = (w - 20) * pct / 100;
    if (fw > 0) g->fillRoundRect(x + 10, y + h - 18, fw, 8, 4, pct >= 85 ? C_HOT : col);
  }
}

void drawOverview() {
  char v[24], sub[24];
  g->fillScreen(C_BG);
  drawHeader("PC STATS");

  snprintf(v, sizeof(v), "%d%%", s.cpu);
  drawTile(4, 30, "CPU", v, nullptr, s.cpu, C_CPU);

  snprintf(v, sizeof(v), "%d%%", s.ram);
  snprintf(sub, sizeof(sub), "Disk %d%% used", s.disk);
  drawTile(162, 30, "MEMORY", v, sub, s.ram, C_RAM);

  if (s.gpu >= 0) {
    snprintf(v, sizeof(v), "%d%%", s.gpu);
    snprintf(sub, sizeof(sub), "%d C", s.gpuTemp);
    drawTile(4, 136, "GPU", v, sub, s.gpu, C_GPU);
  } else {
    drawTile(4, 136, "GPU", "n/a", "NVIDIA only", -1, C_GPU);
  }

  char up[16];
  fmtRate(s.down, v, sizeof(v));
  fmtRate(s.up, up, sizeof(up));
  snprintf(sub, sizeof(sub), "Up %s", up);
  drawTile(162, 136, "DOWNLOAD", v, sub, -1, C_NET);
}

void drawGraph(int y, const char* label, int current, uint8_t* hist, uint32_t col) {
  const int x = 4, w = 312, h = 102;
  const int gx = 6 + x, gy = y + 26, gh = h - 32;
  char v[24];
  g->fillRoundRect(x, y, w, h, 8, C_TILE);
  g->setFont(&fonts::Font2);
  g->setTextDatum(textdatum_t::top_left);
  g->setTextColor(col);
  snprintf(v, sizeof(v), "%s  %d%%", label, current);
  g->drawString(v, x + 10, y + 6);
  g->setTextDatum(textdatum_t::top_right);
  g->setTextColor(C_DIM);
  g->drawString("last 2.5 min", x + w - 10, y + 6);

  g->fillRect(gx, gy, HIST * 2, gh, C_TRACK);
  for (int i = 0; i < histCount; i++) {
    int bx = gx + (HIST - histCount + i) * 2;
    int bh = hist[i] * gh / 100;
    if (bh > 0) g->fillRect(bx, gy + gh - bh, 2, bh, hist[i] >= 85 ? C_HOT : col);
  }
}

void drawHistory() {
  g->fillScreen(C_BG);
  drawHeader("HISTORY");
  drawGraph(30, "CPU", s.cpu, cpuHist, C_CPU);
  drawGraph(136, "MEMORY", s.ram, ramHist, C_RAM);
}

void drawClock() {
  char v[40];
  g->fillScreen(C_BG);
  g->setTextDatum(textdatum_t::middle_center);
  g->setTextColor(C_TEXT);
  g->setFont(&fonts::Font7);
  g->setTextSize(1.5);
  g->drawString(s.clock, 160, 95);
  g->setTextSize(1);
  g->setFont(&fonts::Font4);
  g->setTextColor(C_DIM);
  g->drawString(s.date, 160, 165);
  g->setFont(&fonts::Font2);
  snprintf(v, sizeof(v), "CPU %d%%    RAM %d%%", s.cpu, s.ram);
  g->setTextColor(C_CPU);
  g->drawString(v, 160, 205);
  for (int i = 0; i < PAGES; i++) g->fillCircle(150 + i * 10, 232, 3, i == page ? C_TEXT : C_DIM);
}

void drawWaiting() {
  g->fillScreen(C_BG);
  g->setTextDatum(textdatum_t::middle_center);
  g->setFont(&fonts::Font4);
  g->setTextColor(C_TEXT);
  g->drawString("PC Stats", 160, 50);
  g->setFont(&fonts::Font2);
  g->setTextColor(C_DIM);
  g->drawString("Waiting for PC...", 160, 85);
  g->drawString("Run cyd_stats.py on your computer", 160, 105);

  // Colour check: these should read red, green, blue
  const uint32_t sw[3] = {MKRGB(255, 0, 0), MKRGB(0, 255, 0), MKRGB(0, 0, 255)};
  const char* names[3] = {"R", "G", "B"};
  for (int i = 0; i < 3; i++) {
    int x = 70 + i * 70;
    g->fillRoundRect(x, 140, 50, 40, 6, sw[i]);
    g->setTextColor(C_TEXT);
    g->drawString(names[i], x + 25, 195);
  }
  g->setTextColor(C_DIM);
  g->drawString("Colours wrong? See settings at top of sketch", 160, 222);
}

void render() {
  if (!haveData) drawWaiting();
  else if (page == 0) drawOverview();
  else if (page == 1) drawHistory();
  else drawClock();
  if (useSprite) spr.pushSprite(0, 0);
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);

  // Backlight is GPIO 21 on most boards, 27 on some, so switch both on
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, false);

  lcd.init();
  lcd.setRotation(SCREEN_ROTATION);

  spr.setColorDepth(8);
  if (spr.createSprite(320, 240)) {
    g = &spr;
    useSprite = true;
  }
  render();
}

void loop() {
  bool dirty = false;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf[len] = '\0';
      if (parseLine(buf)) {
        haveData = true;
        lastData = millis();
        dirty = true;
      }
      len = 0;
    } else if (c != '\r' && len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }

  // Back to the waiting screen if the PC stops sending
  if (haveData && millis() - lastData > 5000) {
    haveData = false;
    setLed(false, false, false);
    dirty = true;
  }

  // Tap anywhere to change page
  int32_t tx, ty;
  bool touched = lcd.getTouch(&tx, &ty);
  if (touched && !wasTouched && millis() - lastTap > 250) {
    page = (page + 1) % PAGES;
    lastTap = millis();
    dirty = true;
  }
  wasTouched = touched;

  if (dirty) render();
  delay(10);
}
