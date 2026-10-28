// dashboard_ui.h - everything that gets drawn on the screen.
//
// Depends only on LovyanGFX and dashboard_data.h, so the exact same drawing code
// also runs on a PC (tools/ui_preview) to render the screenshots in the README.
//
// The ESP32 doesn't have enough free RAM for a full 320x240 16-bit frame buffer
// (150 KB), so each frame is drawn in horizontal strips: every page draws through a
// Canvas, which shifts coordinates into the current strip. Anything outside the
// strip is clipped by the sprite.
#pragma once

#include <LovyanGFX.hpp>
#include <stdio.h>

#include "dashboard_data.h"

namespace ui {

static const int W = 320;
static const int H = 240;

// ---------------------------------------------------------------------------
// Theme (0xRRGGBB, always as uint32_t so LovyanGFX treats them as 24-bit colours)
// ---------------------------------------------------------------------------
static const uint32_t BG     = 0x0A0E14;
static const uint32_t CARD   = 0x151B26;
static const uint32_t LINE   = 0x263044;
static const uint32_t TEXT   = 0xE8EDF5;
static const uint32_t DIM    = 0x8A94A6;
static const uint32_t FAINT  = 0x3A4458;
static const uint32_t C_CPU  = 0x22D3EE;  // cyan
static const uint32_t C_GPU  = 0x4ADE80;  // green
static const uint32_t C_RAM  = 0xA78BFA;  // violet
static const uint32_t C_DOWN = 0xFBBF24;  // amber
static const uint32_t C_UP   = 0xF472B6;  // pink
static const uint32_t C_DISK = 0x60A5FA;  // blue
static const uint32_t C_WARM = 0xFACC15;  // yellow
static const uint32_t C_WARN = 0xFB923C;  // orange
static const uint32_t C_HOT  = 0xF87171;  // red

// Linear blend: t = 0 gives a, t = 1 gives b
inline uint32_t mix(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) {
    float ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
    return (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

inline uint32_t loadColor(float pct, uint32_t base) { return pct >= 90 ? C_HOT : base; }

inline uint32_t tempColor(float c) {
  if (c < 0) return DIM;
  if (c < 60) return C_GPU;
  if (c < 75) return C_WARM;
  if (c < 85) return C_WARN;
  return C_HOT;
}

// ---------------------------------------------------------------------------
// Canvas: draws into one horizontal strip of the screen
// ---------------------------------------------------------------------------
using Font = lgfx::IFont;
using Datum = textdatum_t;

struct Canvas {
  LovyanGFX* g;  // the strip sprite
  int oy;        // screen row where this strip starts
  int h;         // strip height

  bool visible(int y, int hh) const { return y + hh > oy && y < oy + h; }

  void clear(uint32_t c) { g->fillScreen(c); }
  void rect(int x, int y, int w, int hh, uint32_t c) {
    if (visible(y, hh)) g->fillRect(x, y - oy, w, hh, c);
  }
  void rrect(int x, int y, int w, int hh, int r, uint32_t c) {
    if (visible(y, hh)) g->fillSmoothRoundRect(x, y - oy, w, hh, r, c);
  }
  void circle(int x, int y, int r, uint32_t c) {
    if (visible(y - r, 2 * r + 1)) g->fillSmoothCircle(x, y - oy, r, c);
  }
  void arc(int x, int y, int r0, int r1, float a0, float a1, uint32_t c) {
    if (visible(y - r1, 2 * r1 + 1)) g->fillArc(x, y - oy, r0, r1, a0, a1, c);
  }
  void ring(int x, int y, int r, uint32_t c) {  // 1 px circle outline
    if (visible(y - r, 2 * r + 1)) g->drawCircle(x, y - oy, r, c);
  }
  void hline(int x, int y, int w, uint32_t c) {
    if (visible(y, 1)) g->drawFastHLine(x, y - oy, w, c);
  }
  void line(int x0, int y0, int x1, int y1, float r, uint32_t c) {
    int top = (y0 < y1 ? y0 : y1) - 2, bot = (y0 > y1 ? y0 : y1) + 2;
    if (visible(top, bot - top)) g->drawWideLine(x0, y0 - oy, x1, y1 - oy, r, c);
  }
  void tri(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t c) {
    g->fillTriangle(x0, y0 - oy, x1, y1 - oy, x2, y2 - oy, c);
  }
  void text(const char* s, int x, int y, const Font* f, uint32_t c, Datum d) {
    if (!visible(y - 40, 80)) return;  // generous: tallest font is 72 px
    g->setFont(f);
    g->setTextColor(c);
    g->setTextDatum(d);
    g->drawString(s, x, y - oy);
  }
  int textWidth(const char* s, const Font* f) {
    g->setFont(f);
    return g->textWidth(s);
  }
};

// Fonts (anti-aliased DejaVu from LovyanGFX)
static const Font* F_TINY  = &fonts::DejaVu9;
static const Font* F_SMALL = &fonts::DejaVu12;
static const Font* F_MED   = &fonts::DejaVu18;
static const Font* F_LARGE = &fonts::DejaVu24;
static const Font* F_HUGE  = &fonts::DejaVu40;
static const Font* F_CLOCK = &fonts::DejaVu72;

static const Datum D_TL = textdatum_t::top_left;
static const Datum D_ML = textdatum_t::middle_left;
static const Datum D_MC = textdatum_t::middle_center;
static const Datum D_MR = textdatum_t::middle_right;
static const Datum D_BL = textdatum_t::baseline_left;
static const Datum D_BR = textdatum_t::baseline_right;

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
enum Page { PAGE_OVERVIEW, PAGE_GPU, PAGE_HISTORY, PAGE_NETWORK, PAGE_CLOCK, PAGE_COUNT };

// Values shown on gauges and bars glide towards the latest reading
struct Smoothed {
  float cpu = 0, gpu = 0, ram = 0, disk = 0, gpuTemp = 0, vram = 0, power = 0, fan = 0;

  static bool approach(float& a, float target) {
    if (target < 0) target = 0;
    float d = target - a;
    if (fabsf(d) < 0.15f) { a = target; return false; }
    a += d * 0.22f;
    return true;
  }
  static float pct(float part, float whole) { return (part >= 0 && whole > 0) ? part * 100 / whole : -1; }

  // Moves one animation step. Returns true while anything is still moving.
  bool step(const Metrics& m) {
    bool moving = false;
    moving |= approach(cpu, m.cpu);
    moving |= approach(gpu, m.gpu);
    moving |= approach(ram, m.ram);
    moving |= approach(disk, m.disk);
    moving |= approach(gpuTemp, m.gpuTemp);
    moving |= approach(vram, pct(m.vramUsedMB, m.vramTotalMB));
    moving |= approach(power, pct(m.gpuPowerW, m.gpuPowerLimitW));
    moving |= approach(fan, m.fan);
    return moving;
  }
};

struct State {
  Metrics m;
  History hist;
  Smoothed s;
  int page = PAGE_OVERVIEW;
  bool connected = false;
  uint32_t ms = 0;  // milliseconds since boot, for the waiting-screen spinner

  State() { m.clear(); }
};

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------
inline void fmtPct(char* out, size_t n, float v) {
  if (v < 0) snprintf(out, n, "--");
  else snprintf(out, n, "%d%%", (int)lroundf(v));
}

inline void fmtRate(char* out, size_t n, float kbs) {
  if (kbs < 0) snprintf(out, n, "--");
  else if (kbs < 1000) snprintf(out, n, "%d KB/s", (int)lroundf(kbs));
  else if (kbs < 10240) snprintf(out, n, "%.1f MB/s", kbs / 1024);
  else snprintf(out, n, "%d MB/s", (int)lroundf(kbs / 1024));
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
// Page indicator: a pill for the current page, dots for the others
inline void pageDots(Canvas& c, int page, int y) {
  const int active = 16, dot = 6, gap = 5;
  int total = active + (PAGE_COUNT - 1) * dot + (PAGE_COUNT - 1) * gap;
  int x = (W - total) / 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    int w = (i == page) ? active : dot;
    c.rrect(x, y - 3, w, 6, 3, i == page ? TEXT : FAINT);
    x += w + gap;
  }
}

inline void header(Canvas& c, const State& st, const char* title, uint32_t accent) {
  c.circle(14, 14, 3, accent);
  c.text(title, 24, 14, F_SMALL, TEXT, D_ML);
  pageDots(c, st.page, 14);
  c.text(st.m.time, W - 12, 14, F_SMALL, DIM, D_MR);
}

// Temperature like "64°C". The fonts have no degree sign, so it's drawn as a small ring.
// Supports the middle_left / middle_center / middle_right datums.
inline void tempText(Canvas& c, float t, int x, int y, const Font* f, uint32_t col, Datum d) {
  if (t < 0) { c.text("--", x, y, f, col, d); return; }
  char num[8];
  snprintf(num, sizeof(num), "%d", (int)lroundf(t));
  c.g->setFont(f);
  int fh = c.g->fontHeight();
  int wn = c.textWidth(num, f), wc = c.textWidth("C", f);
  int dr = fh / 6 < 2 ? 2 : fh / 6;    // degree ring radius
  int dw = 2 * dr + 4;                  // space it takes up
  int total = wn + dw + wc;
  int left = (d == D_MC) ? x - total / 2 : (d == D_MR) ? x - total : x;
  c.text(num, left, y, f, col, D_ML);
  int ry = y - fh / 2 + dr + 2;        // level with the top of the digits
  c.ring(left + wn + 2 + dr, ry, dr, col);
  if (dr >= 3) c.ring(left + wn + 2 + dr, ry, dr - 1, col);  // 2 px thick on bigger text
  c.text("C", left + wn + dw, y, f, col, D_ML);
}

inline void card(Canvas& c, int x, int y, int w, int h) { c.rrect(x, y, w, h, 8, CARD); }

inline void bar(Canvas& c, int x, int y, int w, int h, float pct, uint32_t col, uint32_t track = BG) {
  c.rrect(x, y, w, h, h / 2, track);
  if (pct <= 0) return;
  int fw = (int)(w * (pct > 100 ? 100 : pct) / 100);
  if (fw < h) fw = h;  // keep the rounded end visible for small values
  c.rrect(x, y, fw, h, h / 2, col);
}

// 270-degree ring gauge with rounded ends, open at the bottom
inline void ring(Canvas& c, int cx, int cy, int r, int thick, float pct, uint32_t col) {
  const float a0 = 135, sweep = 270;
  const float rm = r - thick / 2.0f;
  auto cap = [&](float deg, uint32_t color) {
    float rad = deg * 0.0174533f;
    c.circle((int)lroundf(cx + rm * cosf(rad)), (int)lroundf(cy + rm * sinf(rad)), thick / 2, color);
  };
  uint32_t track = mix(col, BG, 0.82f);
  c.arc(cx, cy, r - thick + 1, r, a0, a0 + sweep, track);
  cap(a0, track);
  cap(a0 + sweep, track);
  if (pct > 0.5f) {
    float a1 = a0 + sweep * (pct > 100 ? 100 : pct) / 100;
    c.arc(cx, cy, r - thick + 1, r, a0, a1, col);
    cap(a0, col);
    cap(a1, col);
  }
}

// Down/up arrow drawn as a small triangle
inline void arrow(Canvas& c, int x, int y, bool down, uint32_t col) {
  if (down) c.tri(x - 5, y - 3, x + 5, y - 3, x, y + 4, col);
  else c.tri(x - 5, y + 4, x + 5, y + 4, x, y - 3, col);
}

// Area graph of `count` samples, newest on the right. value(i) returns 0..1.
template <typename F>
inline void areaGraph(Canvas& c, int x, int y, int w, int h, int count, int capacity, F value,
                      uint32_t col, uint32_t bgCol) {
  const int step = w / capacity;  // px per sample
  const int base = y + h - 1;
  uint32_t fill = mix(col, bgCol, 0.72f);
  c.hline(x, y + h / 2, w, mix(LINE, bgCol, 0.3f));  // 50% gridline
  int px0 = x + (capacity - count) * step;
  int prevX = 0, prevY = 0;
  for (int i = 0; i < count; i++) {
    float v = value(i);
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    int bh = (int)lroundf(v * (h - 2));
    int px = px0 + i * step;
    if (bh > 0) c.rect(px, base - bh + 1, step, bh, fill);
    int ty = base - bh;
    if (i > 0) c.line(prevX, prevY, px + step / 2, ty, 0.8f, col);
    prevX = px + step / 2;
    prevY = ty;
  }
}

// A titled graph card. `label` in colour, `value` in white, `note` dim on the right.
template <typename F>
inline void graphCard(Canvas& c, int x, int y, int w, int h, const char* label, const char* value,
                      const char* note, int count, F valueAt, uint32_t col) {
  card(c, x, y, w, h);
  areaGraph(c, x + 2, y + 20, w - 4, h - 22, count, HISTORY_LEN, valueAt, col, CARD);
  c.text(label, x + 10, y + 11, F_SMALL, col, D_ML);
  int lw = c.textWidth(label, F_SMALL);
  c.text(value, x + 16 + lw, y + 11, F_SMALL, TEXT, D_ML);
  if (note) c.text(note, x + w - 10, y + 11, F_TINY, DIM, D_MR);
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------
inline void gaugeWithLabels(Canvas& c, int cx, int cy, int r, float shown, float actual,
                            uint32_t col, const char* label, const char* sub, uint32_t subCol) {
  ring(c, cx, cy, r, 9, shown, loadColor(actual, col));
  char v[8];
  fmtPct(v, sizeof(v), actual);
  c.text(v, cx, cy - 1, F_LARGE, TEXT, D_MC);
  c.text(label, cx, cy + r - 8, F_TINY, DIM, D_MC);  // sits in the gap at the bottom of the ring
  if (sub) c.text(sub, cx, cy + r + 13, F_SMALL, subCol, D_MC);
}

inline void pageOverview(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "Overview", C_CPU);
  char sub[24];

  // Three gauges
  if (m.cpuThreads > 0) snprintf(sub, sizeof(sub), "%d threads", m.cpuThreads);
  else sub[0] = '\0';
  gaugeWithLabels(c, 56, 88, 44, st.s.cpu, m.cpu, C_CPU, "CPU", sub, DIM);

  gaugeWithLabels(c, 160, 88, 44, st.s.gpu, m.gpu, C_GPU, "GPU", m.hasGpu() ? nullptr : "n/a", DIM);
  if (m.hasGpu()) tempText(c, m.gpuTemp, 160, 88 + 44 + 13, F_SMALL, tempColor(m.gpuTemp), D_MC);

  if (m.ramUsedMB >= 0 && m.ramTotalMB > 0)
    snprintf(sub, sizeof(sub), "%.1f / %.0f GB", m.ramUsedMB / 1024, m.ramTotalMB / 1024);
  else sub[0] = '\0';
  gaugeWithLabels(c, 264, 88, 44, st.s.ram, m.ram, C_RAM, "RAM", sub, DIM);

  // Network card with a mini graph
  const int cy = 166, ch = 66;
  card(c, 8, cy, 200, ch);
  c.text("NETWORK", 18, cy + 13, F_TINY, DIM, D_ML);
  char r[16];
  fmtRate(r, sizeof(r), m.downKBs);
  arrow(c, 22, cy + 34, true, C_DOWN);
  c.text(r, 32, cy + 34, F_MED, TEXT, D_ML);
  fmtRate(r, sizeof(r), m.upKBs);
  arrow(c, 22, cy + 53, false, C_UP);
  c.text(r, 32, cy + 53, F_SMALL, DIM, D_ML);

  const History& h = st.hist;
  const int spark = 36;  // samples shown in the mini graph
  int n = h.down.count < spark ? h.down.count : spark;
  int off = h.down.count - n;
  float peak = 1;
  for (int i = off; i < h.down.count; i++) {
    if (h.down.v[i] > peak) peak = h.down.v[i];
    if (h.up.v[i] > peak) peak = h.up.v[i];
  }
  areaGraph(c, 130, cy + 10, 72, 50, n, spark,
            [&](int i) { return h.up.v[off + i] / peak; }, C_UP, CARD);
  areaGraph(c, 130, cy + 10, 72, 50, n, spark,
            [&](int i) { return h.down.v[off + i] / peak; }, C_DOWN, CARD);

  // Disk card
  card(c, 216, cy, 96, ch);
  c.text("DISK", 226, cy + 13, F_TINY, DIM, D_ML);
  char d[8];
  fmtPct(d, sizeof(d), m.disk);
  c.text(d, 226, cy + 35, F_LARGE, TEXT, D_ML);
  bar(c, 226, cy + 52, 76, 6, st.s.disk, loadColor(m.disk, C_DISK));

}

// One labelled row with a value on the right and a bar underneath
inline void statRow(Canvas& c, int x, int y, int w, const char* label, const char* value,
                    float barPct, uint32_t col) {
  c.text(label, x, y + 9, F_TINY, DIM, D_ML);
  c.text(value, x + w, y + 9, F_MED, TEXT, D_MR);
  bar(c, x, y + 24, w, 6, barPct, col, CARD);
}

inline void pageGpu(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "GPU", C_GPU);
  if (!m.hasGpu()) {
    c.text("No NVIDIA GPU detected", W / 2, 110, F_MED, TEXT, D_MC);
    c.text("GPU stats need an NVIDIA card and driver", W / 2, 138, F_SMALL, DIM, D_MC);
      return;
  }

  // Big load gauge
  const int gx = 82, gy = 116, gr = 64;
  ring(c, gx, gy, gr, 12, st.s.gpu, loadColor(m.gpu, C_GPU));
  char v[24];
  fmtPct(v, sizeof(v), m.gpu);
  c.text(v, gx, gy - 2, F_HUGE, TEXT, D_MC);
  c.text("LOAD", gx, gy + gr - 12, F_TINY, DIM, D_MC);
  if (m.gpuName[0]) c.text(m.gpuName, gx, 210, F_SMALL, DIM, D_MC);

  // Detail rows
  const int x = 172, w = 138;
  card(c, x - 10, 32, w + 20, 188);
  statRow(c, x, 40, w, "TEMP", "", st.s.gpuTemp, tempColor(m.gpuTemp));
  tempText(c, m.gpuTemp, x + w, 40 + 9, F_MED, TEXT, D_MR);

  if (m.vramUsedMB >= 0 && m.vramTotalMB > 0)
    snprintf(v, sizeof(v), "%.1f/%.0f GB", m.vramUsedMB / 1024, m.vramTotalMB / 1024);
  else snprintf(v, sizeof(v), "--");
  statRow(c, x, 85, w, "VRAM", v, st.s.vram, C_RAM);

  if (m.gpuPowerW >= 0) snprintf(v, sizeof(v), "%d W", (int)lroundf(m.gpuPowerW));
  else snprintf(v, sizeof(v), "--");
  statRow(c, x, 130, w, "POWER", v, st.s.power, C_WARM);

  fmtPct(v, sizeof(v), m.fan);
  statRow(c, x, 175, w, "FAN", v, st.s.fan, C_DISK);

}

inline void pageHistory(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  const History& h = st.hist;
  header(c, st, "History", C_RAM);
  char v[8], note[20];

  fmtPct(v, sizeof(v), m.cpu);
  snprintf(note, sizeof(note), "peak %d%%", h.cpu.maxValue());
  graphCard(c, 8, 30, 304, 64, "CPU", v, note, h.cpu.count,
            [&](int i) { return h.cpu.v[i] / 100.0f; }, C_CPU);

  fmtPct(v, sizeof(v), m.gpu);
  snprintf(note, sizeof(note), "peak %d%%", h.gpu.maxValue());
  graphCard(c, 8, 99, 304, 64, "GPU", m.hasGpu() ? v : "n/a", m.hasGpu() ? note : nullptr, h.gpu.count,
            [&](int i) { return h.gpu.v[i] / 100.0f; }, C_GPU);

  fmtPct(v, sizeof(v), m.ram);
  snprintf(note, sizeof(note), "last 2.5 min");
  graphCard(c, 8, 168, 304, 64, "RAM", v, note, h.ram.count,
            [&](int i) { return h.ram.v[i] / 100.0f; }, C_RAM);
}

inline void pageNetwork(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  const History& h = st.hist;
  header(c, st, "Network", C_DOWN);
  char v[16], pk[16], note[24];

  float peakDown = h.down.maxValue();
  fmtRate(v, sizeof(v), m.downKBs);
  fmtRate(pk, sizeof(pk), peakDown);
  snprintf(note, sizeof(note), "peak %s", pk);
  float scaleD = peakDown > 1 ? peakDown : 1;
  graphCard(c, 8, 30, 304, 98, "DOWN", v, note, h.down.count,
            [&](int i) { return h.down.v[i] / scaleD; }, C_DOWN);

  float peakUp = h.up.maxValue();
  fmtRate(v, sizeof(v), m.upKBs);
  fmtRate(pk, sizeof(pk), peakUp);
  snprintf(note, sizeof(note), "peak %s", pk);
  float scaleU = peakUp > 1 ? peakUp : 1;
  graphCard(c, 8, 134, 304, 98, "UP", v, note, h.up.count,
            [&](int i) { return h.up.v[i] / scaleU; }, C_UP);
}

inline void miniStat(Canvas& c, int x, int y, int w, const char* label, const char* value,
                     float barPct, uint32_t col) {
  card(c, x, y, w, 48);
  c.text(label, x + w / 2, y + 11, F_TINY, DIM, D_MC);
  c.text(value, x + w / 2, y + 27, F_MED, TEXT, D_MC);
  bar(c, x + 8, y + 39, w - 16, 4, barPct, col);
}

inline void pageClock(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  c.text(m.time, W / 2, 78, F_CLOCK, TEXT, D_MC);
  c.text(m.date, W / 2, 132, F_MED, DIM, D_MC);

  char v[12];
  const int y = 168, w = 72, gap = 6, x0 = (W - (4 * w + 3 * gap)) / 2;
  fmtPct(v, sizeof(v), m.cpu);
  miniStat(c, x0, y, w, "CPU", v, st.s.cpu, C_CPU);
  fmtPct(v, sizeof(v), m.gpu);
  miniStat(c, x0 + (w + gap), y, w, "GPU", m.hasGpu() ? v : "n/a", st.s.gpu, C_GPU);
  fmtPct(v, sizeof(v), m.ram);
  miniStat(c, x0 + 2 * (w + gap), y, w, "RAM", v, st.s.ram, C_RAM);
  miniStat(c, x0 + 3 * (w + gap), y, w, "GPU TEMP", "", st.s.gpuTemp, tempColor(m.gpuTemp));
  tempText(c, m.gpuTemp, x0 + 3 * (w + gap) + w / 2, y + 27, F_MED, TEXT, D_MC);

  pageDots(c, st.page, 230);
}

inline void pageWaiting(Canvas& c, const State& st) {
  c.text("PC Stats", W / 2, 52, F_LARGE, TEXT, D_MC);

  // Spinner: a short arc going round
  float a = (st.ms % 1200) * 0.3f;
  c.arc(W / 2, 108, 14, 18, 0, 360, CARD);
  c.arc(W / 2, 108, 14, 18, a, a + 90, C_CPU);

  c.text("Waiting for PC...", W / 2, 150, F_MED, TEXT, D_MC);
  c.text("Start pc_stats.py on your computer", W / 2, 174, F_SMALL, DIM, D_MC);

  // Colour check: these should read red, green, blue (see settings in the .ino)
  static const uint32_t sw[3] = {0xFF0000, 0x00FF00, 0x0000FF};
  static const char* names[3] = {"R", "G", "B"};
  for (int i = 0; i < 3; i++) {
    int x = W / 2 - 36 + i * 28;
    c.rrect(x, 204, 16, 16, 4, sw[i]);
    c.text(names[i], x + 8, 229, F_TINY, DIM, D_MC);
  }
}

// Draws the whole screen (or the part of it inside this canvas's strip)
inline void drawFrame(Canvas& c, const State& st) {
  c.clear(BG);
  if (!st.connected) { pageWaiting(c, st); return; }
  switch (st.page) {
    case PAGE_OVERVIEW: pageOverview(c, st); break;
    case PAGE_GPU:      pageGpu(c, st); break;
    case PAGE_HISTORY:  pageHistory(c, st); break;
    case PAGE_NETWORK:  pageNetwork(c, st); break;
    default:            pageClock(c, st); break;
  }
}

}  // namespace ui
