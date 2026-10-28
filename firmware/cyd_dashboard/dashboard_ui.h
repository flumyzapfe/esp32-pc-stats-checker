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
static const int HEADER_H = 30;  // tapping above this opens the page menu

// ---------------------------------------------------------------------------
// Theme (0xRRGGBB, always as uint32_t so LovyanGFX treats them as 24-bit colours)
// ---------------------------------------------------------------------------
static const uint32_t BG     = 0x0A0E14;
static const uint32_t CARD   = 0x151B26;
static const uint32_t CARD2  = 0x1C2433;  // raised elements inside cards
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
static const uint32_t C_OK   = 0x4ADE80;  // green
static const uint32_t C_WARM = 0xFACC15;  // yellow
static const uint32_t C_WARN = 0xFB923C;  // orange
static const uint32_t C_HOT  = 0xF87171;  // red
static const uint32_t C_INFO = 0x60A5FA;  // blue

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
  if (c < 60) return C_OK;
  if (c < 75) return C_WARM;
  if (c < 85) return C_WARN;
  return C_HOT;
}

inline uint32_t healthColor(float score) {
  if (score < 0) return DIM;
  if (score >= 90) return C_OK;
  if (score >= 70) return C_WARM;
  if (score >= 40) return C_WARN;
  return C_HOT;
}

inline const char* healthText(float score) {
  if (score >= 90) return "Healthy";
  if (score >= 70) return "Fair";
  if (score >= 40) return "Needs attention";
  return "Critical";
}

inline uint32_t alertColor(int level) { return level >= 2 ? C_HOT : level == 1 ? C_WARN : C_INFO; }

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
  void ring(int x, int y, int r, uint32_t c) {  // 1 px circle outline
    if (visible(y - r, 2 * r + 1)) g->drawCircle(x, y - oy, r, c);
  }
  void arc(int x, int y, int r0, int r1, float a0, float a1, uint32_t c) {
    if (visible(y - r1, 2 * r1 + 1)) g->fillArc(x, y - oy, r0, r1, a0, a1, c);
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

// Text alignment (not "MR"/"BR": those are register macros on the ESP32)
static const Datum D_TL = textdatum_t::top_left;
static const Datum D_ML = textdatum_t::middle_left;
static const Datum D_MC = textdatum_t::middle_center;
static const Datum D_MR = textdatum_t::middle_right;

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
enum Page {
  PAGE_OVERVIEW, PAGE_HEALTH, PAGE_CPU, PAGE_GPU, PAGE_THERMALS, PAGE_PROCESSES,
  PAGE_STORAGE, PAGE_HISTORY, PAGE_NETWORK, PAGE_CLOCK, PAGE_COUNT
};
static const char* const PAGE_NAMES[PAGE_COUNT] = {
  "Overview", "Health", "CPU", "GPU", "Thermals", "Processes", "Storage", "History",
  "Network", "Clock"};

// Values shown on gauges and bars glide towards the latest reading
struct Smoothed {
  float cpu = 0, gpu = 0, ram = 0, disk = 0, gpuTemp = 0, cpuTemp = 0, vram = 0, power = 0,
        fan = 0, health = 0;

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
    moving |= approach(cpuTemp, m.cpuTemp);
    moving |= approach(vram, pct(m.vramUsedMB, m.vramTotalMB));
    moving |= approach(power, pct(m.gpuPowerW, m.gpuPowerLimitW));
    moving |= approach(fan, m.fan);
    moving |= approach(health, m.healthScore);
    return moving;
  }
};

struct State {
  Metrics m;
  History hist;
  Smoothed s;
  int page = PAGE_OVERVIEW;
  bool menuOpen = false;
  bool connected = false;
  uint32_t ms = 0;  // milliseconds since boot, for spinners and fan animation
  int tapX = 0, tapY = 0;   // where the last touch landed, shown as a fading ring
  uint32_t tapMs = 0;

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

inline void fmtMB(char* out, size_t n, float mb) {
  if (mb < 0) snprintf(out, n, "--");
  else if (mb < 1000) snprintf(out, n, "%d MB", (int)lroundf(mb));
  else snprintf(out, n, "%.1f GB", mb / 1024);
}

inline void fmtGB(char* out, size_t n, float gb) {
  if (gb >= 1000) snprintf(out, n, "%.1f TB", gb / 1024);
  else snprintf(out, n, "%d GB", (int)lroundf(gb));
}

inline void fmtUptime(char* out, size_t n, float s) {
  if (s < 0) { snprintf(out, n, "--"); return; }
  long t = (long)s, d = t / 86400, h = t % 86400 / 3600, mnt = t % 3600 / 60;
  if (d > 0) snprintf(out, n, "%ldd %ldh", d, h);
  else if (h > 0) snprintf(out, n, "%ldh %ldm", h, mnt);
  else snprintf(out, n, "%ldm", mnt);
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
// Page indicator: a pill for the current page, dots for the others
inline void pageDots(Canvas& c, int page, int cx, int y) {
  const int active = 14, dot = 5, gap = 4;
  int total = active + (PAGE_COUNT - 1) * (dot + gap);
  int x = cx - total / 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    int w = (i == page) ? active : dot;
    c.rrect(x, y - 2, w, 5, 2, i == page ? TEXT : FAINT);
    x += w + gap;
  }
}

// Small coloured pill with the health score, shown on every page
inline void healthBadge(Canvas& c, int right, int y, float score) {
  char v[6];
  snprintf(v, sizeof(v), "%d", (int)lroundf(score));
  uint32_t col = healthColor(score);
  int w = c.textWidth(v, F_SMALL) + 20;
  int x = right - w;
  c.rrect(x, y - 8, w, 16, 8, mix(col, BG, 0.78f));
  c.circle(x + 8, y, 3, col);
  c.text(v, x + 14, y, F_SMALL, col, D_ML);
}

inline void header(Canvas& c, const State& st, const char* title, uint32_t accent) {
  c.circle(14, 14, 3, accent);
  c.text(title, 24, 14, F_SMALL, TEXT, D_ML);
  pageDots(c, st.page, 160, 14);
  int right = W - 10;
  c.text(st.m.time, right, 14, F_SMALL, DIM, D_MR);
  if (st.m.hasHealth()) healthBadge(c, right - c.textWidth(st.m.time, F_SMALL) - 6, 14, st.m.healthScore);
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

// One labelled row with a value on the right and a bar underneath
inline void statRow(Canvas& c, int x, int y, int w, const char* label, const char* value,
                    float barPct, uint32_t col) {
  c.text(label, x, y + 9, F_TINY, DIM, D_ML);
  int room = w - c.textWidth(label, F_TINY) - 8;  // smaller font for long values
  c.text(value, x + w, y + 9, c.textWidth(value, F_MED) <= room ? F_MED : F_SMALL, TEXT, D_MR);
  bar(c, x, y + 24, w, 6, barPct, col, CARD2);
}

// Small card: label on top, big value, thin bar
inline void miniStat(Canvas& c, int x, int y, int w, const char* label, const char* value,
                     float barPct, uint32_t col) {
  card(c, x, y, w, 48);
  c.text(label, x + w / 2, y + 11, F_TINY, DIM, D_MC);
  c.text(value, x + w / 2, y + 27, F_MED, TEXT, D_MC);
  if (barPct >= 0) bar(c, x + 8, y + 39, w - 16, 4, barPct, col);
}

// Label above a value, no card (used in rows of key figures)
inline void figure(Canvas& c, int x, int y, const char* label, const char* value, Datum d,
                   uint32_t col = TEXT) {
  c.text(label, x, y, F_TINY, DIM, d);
  c.text(value, x, y + 17, F_MED, col, d);
}

// Draws text with the small font, or the tiny one if it doesn't fit in maxW, and
// trims it with ".." if even that is too wide
inline void fitText(Canvas& c, const char* s, int x, int y, int maxW, uint32_t col, Datum d,
                    bool allowSmall = true) {
  if (allowSmall && c.textWidth(s, F_SMALL) <= maxW) { c.text(s, x, y, F_SMALL, col, d); return; }
  char buf[48];
  snprintf(buf, sizeof(buf), "%s", s);
  size_t n = strlen(buf);
  while (n > 1 && c.textWidth(buf, F_TINY) > maxW) {
    buf[--n] = '\0';
    if (n > 2) { buf[n - 2] = '.'; buf[n - 1] = '.'; }
  }
  c.text(buf, x, y, F_TINY, col, d);
}

// Spinning fan icon; speed is proportional to RPM (not literal, or it would blur)
inline void fanIcon(Canvas& c, int cx, int cy, int r, int rpm, uint32_t ms, uint32_t col) {
  float a = rpm > 0 ? fmodf(ms * rpm * 0.0003f, 360.0f) : 20;
  uint32_t blade = rpm > 0 ? col : FAINT;
  c.circle(cx, cy, r, mix(blade, CARD, 0.85f));
  for (int i = 0; i < 5; i++) {
    float b = a + i * 72;
    c.arc(cx, cy, 3, r - 1, b, b + 38, blade);
  }
  c.circle(cx, cy, 3, CARD);
  c.circle(cx, cy, 2, blade);
}

// Status dot for an alert level, with a "!" or "i" for clarity
inline void levelDot(Canvas& c, int cx, int cy, int level) {
  uint32_t col = alertColor(level);
  c.circle(cx, cy, 7, mix(col, CARD, 0.7f));
  c.circle(cx, cy, 4, col);
}

// Small green tick for a passed check
inline void tick(Canvas& c, int cx, int cy) {
  c.circle(cx, cy, 7, mix(C_OK, CARD, 0.75f));
  c.line(cx - 3, cy, cx - 1, cy + 3, 1.2f, C_OK);
  c.line(cx - 1, cy + 3, cx + 4, cy - 3, 1.2f, C_OK);
}

// Little icons for the page menu, drawn from primitives (about 28 x 28 px)
inline void pageIcon(Canvas& c, int page, int cx, int cy, uint32_t col) {
  switch (page) {
    case PAGE_OVERVIEW:
      for (int i = -1; i <= 1; i++) c.arc(cx + i * 13, cy, 4, 6, 135, 405, col);
      break;
    case PAGE_HEALTH:  // heart
      c.circle(cx - 5, cy - 3, 6, col);
      c.circle(cx + 5, cy - 3, 6, col);
      c.tri(cx - 11, cy - 1, cx + 11, cy - 1, cx, cy + 11, col);
      break;
    case PAGE_CPU:  // chip with pins
      c.rrect(cx - 9, cy - 9, 18, 18, 3, col);
      c.rrect(cx - 4, cy - 4, 8, 8, 1, BG);
      for (int i = -1; i <= 1; i++) {
        c.rect(cx + i * 6 - 1, cy - 13, 2, 4, col);
        c.rect(cx + i * 6 - 1, cy + 9, 2, 4, col);
        c.rect(cx - 13, cy + i * 6 - 1, 4, 2, col);
        c.rect(cx + 9, cy + i * 6 - 1, 4, 2, col);
      }
      break;
    case PAGE_GPU:  // card with a fan
      c.rrect(cx - 13, cy - 8, 26, 16, 3, col);
      c.circle(cx - 3, cy, 5, BG);
      c.circle(cx - 3, cy, 2, col);
      c.rect(cx + 5, cy - 4, 5, 8, BG);
      break;
    case PAGE_THERMALS:  // thermometer
      c.rrect(cx - 3, cy - 13, 7, 18, 3, col);
      c.circle(cx, cy + 7, 6, col);
      c.rect(cx - 1, cy - 9, 2, 12, BG);
      break;
    case PAGE_PROCESSES:  // list
      for (int i = -1; i <= 1; i++) {
        c.circle(cx - 9, cy + i * 8, 2, col);
        c.rrect(cx - 4, cy + i * 8 - 2, 16 - (i + 1) * 4, 4, 2, col);
      }
      break;
    case PAGE_STORAGE:  // stacked drives
      for (int i = -1; i <= 1; i++) {
        c.rrect(cx - 11, cy + i * 8 - 3, 22, 6, 2, col);
        c.circle(cx + 7, cy + i * 8, 1, BG);
      }
      break;
    case PAGE_HISTORY:  // graph line
      c.line(cx - 12, cy + 6, cx - 5, cy - 2, 1.2f, col);
      c.line(cx - 5, cy - 2, cx + 1, cy + 3, 1.2f, col);
      c.line(cx + 1, cy + 3, cx + 12, cy - 9, 1.2f, col);
      c.hline(cx - 12, cy + 11, 24, col);
      break;
    case PAGE_NETWORK:  // down + up arrows
      c.rect(cx - 7, cy - 10, 3, 14, col);
      c.tri(cx - 11, cy + 2, cx, cy + 2, cx - 6, cy + 10, col);
      c.rect(cx + 4, cy - 4, 3, 14, col);
      c.tri(cx, cy - 2, cx + 11, cy - 2, cx + 5, cy - 10, col);
      break;
    default:  // clock
      c.circle(cx, cy, 12, col);
      c.circle(cx, cy, 10, BG);
      c.line(cx, cy, cx, cy - 7, 1.0f, col);
      c.line(cx, cy, cx + 5, cy + 2, 1.0f, col);
      break;
  }
}

inline uint32_t pageAccent(const State& st, int page) {
  switch (page) {
    case PAGE_HEALTH: return st.m.hasHealth() ? healthColor(st.m.healthScore) : C_OK;
    case PAGE_GPU: return C_GPU;
    case PAGE_THERMALS: return C_WARN;
    case PAGE_PROCESSES: return C_RAM;
    case PAGE_STORAGE: return C_DISK;
    case PAGE_HISTORY: return C_RAM;
    case PAGE_NETWORK: return C_DOWN;
    case PAGE_CLOCK: return TEXT;
    default: return C_CPU;
  }
}

// "Needs LibreHardwareMonitor" hint shown where temps or fans would go
inline void sensorsHint(Canvas& c, int x, int y, int w, int h, const char* what) {
  c.text(what, x + w / 2, y + h / 2 - 14, F_SMALL, DIM, D_MC);
  c.text("needs", x + w / 2, y + h / 2 + 4, F_TINY, FAINT, D_MC);
  c.text("LibreHardwareMonitor", x + w / 2, y + h / 2 + 17, F_TINY, FAINT, D_MC);
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

  // CPU gauge: temperature underneath when known, otherwise thread count
  if (m.cpuTemp >= 0) {
    gaugeWithLabels(c, 56, 88, 44, st.s.cpu, m.cpu, C_CPU, "CPU", nullptr, DIM);
    tempText(c, m.cpuTemp, 56, 88 + 44 + 13, F_SMALL, tempColor(m.cpuTemp), D_MC);
  } else {
    if (m.cpuThreads > 0) snprintf(sub, sizeof(sub), "%d threads", m.cpuThreads);
    else sub[0] = '\0';
    gaugeWithLabels(c, 56, 88, 44, st.s.cpu, m.cpu, C_CPU, "CPU", sub, DIM);
  }

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

inline void pageHealth(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "Health", pageAccent(st, PAGE_HEALTH));

  // Score gauge
  const int gx = 66, gy = 92, gr = 52;
  uint32_t col = healthColor(m.healthScore);
  ring(c, gx, gy, gr, 10, st.s.health, col);
  char v[32];
  if (m.hasHealth()) snprintf(v, sizeof(v), "%d", (int)lroundf(m.healthScore));
  else snprintf(v, sizeof(v), "--");
  c.text(v, gx, gy - 3, F_HUGE, TEXT, D_MC);
  c.text("SCORE", gx, gy + gr - 10, F_TINY, DIM, D_MC);
  c.text(m.hasHealth() ? healthText(m.healthScore) : "Update PC script", gx, gy + gr + 12,
         F_SMALL, col, D_MC);

  // System facts
  int y = 178;
  if (m.hostName[0]) { c.text(m.hostName, 14, y, F_SMALL, TEXT, D_ML); y += 17; }
  if (m.osName[0]) { c.text(m.osName, 14, y, F_TINY, DIM, D_ML); y += 14; }
  char up[24];
  fmtUptime(up, sizeof(up), m.uptimeS);
  snprintf(v, sizeof(v), "Up %s", up);
  if (m.uptimeS >= 0) { c.text(v, 14, y, F_TINY, DIM, D_ML); y += 14; }
  if (m.procCount >= 0) {
    snprintf(v, sizeof(v), "%d processes", (int)m.procCount);
    c.text(v, 14, y, F_TINY, DIM, D_ML);
  }

  // Checks
  const int x = 136, w = 176, top = 34, hh = 198;
  card(c, x, top, w, hh);
  c.text("CHECKS", x + 12, top + 13, F_TINY, DIM, D_ML);
  if (m.alertCount == 0 && m.okCount == 0) {
    int cx = x + w / 2, cy = top + 86;
    c.circle(cx, cy, 22, mix(C_OK, CARD, 0.75f));
    c.line(cx - 10, cy + 1, cx - 3, cy + 8, 2.2f, C_OK);
    c.line(cx - 3, cy + 8, cx + 11, cy - 7, 2.2f, C_OK);
    c.text(m.hasHealth() ? "All checks passed" : "No health data yet", cx, cy + 40, F_SMALL,
           TEXT, D_MC);
    c.text("Temps, memory, drives and fans", cx, cy + 58, F_TINY, DIM, D_MC);
    return;
  }
  if (m.alertCount == 0) c.text("ALL PASSED", x + w - 12, top + 13, F_TINY, C_OK, D_MR);
  const int rowH = 27, maxRows = 6, tx = x + 27, tw = w - 33;
  // One font size for the whole list: the larger one if every row fits
  bool small = true;
  for (int i = 0; i < m.alertCount; i++) small &= c.textWidth(m.alerts[i].text, F_SMALL) <= tw;
  for (int i = 0; i < m.okCount; i++) small &= c.textWidth(m.okChecks[i], F_SMALL) <= tw;
  auto rowText = [&](const char* t, int ry, uint32_t col) {
    if (small) c.text(t, tx, ry, F_SMALL, col, D_ML);
    else fitText(c, t, tx, ry, tw, col, D_ML, false);  // tiny font, trimmed if still too long
  };
  int row = 0;
  for (int i = 0; i < m.alertCount && row < maxRows; i++, row++) {
    int ry = top + 34 + row * rowH;
    const AlertInfo& a = m.alerts[i];
    levelDot(c, x + 14, ry, a.level);
    rowText(a.text, ry, a.level == 0 ? DIM : TEXT);
  }
  for (int i = 0; i < m.okCount && row < maxRows; i++, row++) {
    int ry = top + 34 + row * rowH;
    tick(c, x + 14, ry);
    rowText(m.okChecks[i], ry, DIM);
  }
}

inline void pageCpu(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "CPU", C_CPU);
  char v[24];

  // Temperature gauge (0-100 C scale)
  const int gx = 62, gy = 86, gr = 48;
  if (m.cpuTemp >= 0) {
    ring(c, gx, gy, gr, 10, st.s.cpuTemp, tempColor(m.cpuTemp));
    tempText(c, m.cpuTemp, gx, gy - 2, F_LARGE, TEXT, D_MC);
  } else {
    ring(c, gx, gy, gr, 10, 0, DIM);
    c.text("--", gx, gy - 2, F_LARGE, TEXT, D_MC);
  }
  c.text("TEMP", gx, gy + gr - 10, F_TINY, DIM, D_MC);

  // Temperature history (or a hint if there are no temperatures)
  const int hx = 8, hy = 150, hw = 108, hh = 82;
  card(c, hx, hy, hw, hh);
  if (m.cpuTemp >= 0) {
    const History& h = st.hist;
    c.text("TEMP", hx + 10, hy + 12, F_TINY, DIM, D_ML);
    int lo = h.cpuTemp.minValue(), hi = h.cpuTemp.maxValue();
    snprintf(v, sizeof(v), "%d-%d", lo, hi);
    c.text(v, hx + hw - 8, hy + 12, F_TINY, DIM, D_MR);
    int n = h.cpuTemp.count < 52 ? h.cpuTemp.count : 52;
    int off = h.cpuTemp.count - n;
    areaGraph(c, hx + 2, hy + 24, hw - 4, hh - 26, n, 52,
              [&](int i) { return h.cpuTemp.v[off + i] / 100.0f; }, tempColor(m.cpuTemp), CARD);
  } else {
    c.text("Temperatures", hx + hw / 2, hy + 32, F_SMALL, DIM, D_MC);
    c.text("need Libre-", hx + hw / 2, hy + 50, F_TINY, FAINT, D_MC);
    c.text("HardwareMonitor", hx + hw / 2, hy + 63, F_TINY, FAINT, D_MC);
  }

  // Name and key figures
  const int x = 128, w = 184;
  c.text(m.cpuName[0] ? m.cpuName : "CPU", x, 44, F_SMALL, TEXT, D_ML);
  fmtPct(v, sizeof(v), m.cpu);
  figure(c, x, 66, "LOAD", v, D_TL, loadColor(m.cpu, TEXT));
  if (m.cpuClockMHz >= 0) snprintf(v, sizeof(v), "%.2f", m.cpuClockMHz / 1000);
  else snprintf(v, sizeof(v), "--");
  figure(c, x + 70, 66, "GHZ", v, D_TL);
  if (m.cpuPowerW >= 0) snprintf(v, sizeof(v), "%d", (int)lroundf(m.cpuPowerW));
  else snprintf(v, sizeof(v), "--");
  figure(c, x + 134, 66, "WATTS", v, D_TL);

  // Per-core load bars
  const int by = 106, bh = 126;
  card(c, x - 6, by, w + 12, bh);
  if (m.cpuThreads > 0) snprintf(v, sizeof(v), "%d THREADS", m.cpuThreads);
  else snprintf(v, sizeof(v), "CORES");
  c.text(v, x + 4, by + 12, F_TINY, DIM, D_ML);
  if (m.coreCount > 0) {
    const int gx0 = x + 4, gw = w - 8, gy0 = by + 24, gh = bh - 32;
    const int gap = m.coreCount > 16 ? 1 : 2;
    int bw = (gw - gap * (m.coreCount - 1)) / m.coreCount;
    if (bw < 2) bw = 2;
    int used = bw * m.coreCount + gap * (m.coreCount - 1);
    int left = gx0 + (gw - used) / 2;
    for (int i = 0; i < m.coreCount; i++) {
      int bx = left + i * (bw + gap);
      int fill = (int)lroundf(m.coreLoad[i] * gh / 100.0f);
      c.rrect(bx, gy0, bw, gh, bw > 4 ? 2 : 0, CARD2);
      if (fill > 0) {
        uint32_t col = m.coreLoad[i] >= 90 ? C_HOT : mix(C_CPU, CARD, 0.45f - m.coreLoad[i] * 0.0045f);
        c.rrect(bx, gy0 + gh - fill, bw, fill, bw > 4 && fill > 4 ? 2 : 0, col);
      }
    }
  } else {
    c.text("Per-core load: update PC script", x + w / 2, by + bh / 2 + 6, F_TINY, DIM, D_MC);
  }
}

inline void pageGpu(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "GPU", C_GPU);
  if (!m.hasGpu()) {
    c.text("No GPU detected", W / 2, 110, F_MED, TEXT, D_MC);
    c.text("NVIDIA works directly; AMD and Intel", W / 2, 138, F_SMALL, DIM, D_MC);
    c.text("need LibreHardwareMonitor", W / 2, 156, F_SMALL, DIM, D_MC);
    return;
  }

  // Big load gauge
  const int gx = 82, gy = 106, gr = 64;
  ring(c, gx, gy, gr, 12, st.s.gpu, loadColor(m.gpu, C_GPU));
  char v[24];
  fmtPct(v, sizeof(v), m.gpu);
  c.text(v, gx, gy - 2, F_HUGE, TEXT, D_MC);
  c.text("LOAD", gx, gy + gr - 12, F_TINY, DIM, D_MC);
  if (m.gpuName[0]) c.text(m.gpuName, gx, 196, F_SMALL, TEXT, D_MC);
  if (m.gpuClockMHz >= 0) {
    if (m.gpuMemClockMHz >= 0)
      snprintf(v, sizeof(v), "%d / %d MHz", (int)m.gpuClockMHz, (int)m.gpuMemClockMHz);
    else snprintf(v, sizeof(v), "%d MHz", (int)m.gpuClockMHz);
    c.text(v, gx, 214, F_TINY, DIM, D_MC);
  }

  // Detail rows
  const int x = 172, w = 138;
  card(c, x - 10, 32, w + 20, 200);
  statRow(c, x, 40, w, "TEMP", "", st.s.gpuTemp, tempColor(m.gpuTemp));
  tempText(c, m.gpuTemp, x + w, 40 + 9, F_MED, TEXT, D_MR);

  if (m.vramUsedMB >= 0 && m.vramTotalMB > 0)
    snprintf(v, sizeof(v), "%.1f/%.0f GB", m.vramUsedMB / 1024, m.vramTotalMB / 1024);
  else snprintf(v, sizeof(v), "--");
  statRow(c, x, 88, w, "VRAM", v, st.s.vram, C_RAM);

  if (m.gpuPowerW >= 0) snprintf(v, sizeof(v), "%d W", (int)lroundf(m.gpuPowerW));
  else snprintf(v, sizeof(v), "--");
  statRow(c, x, 136, w, "POWER", v, st.s.power, C_WARM);

  fmtPct(v, sizeof(v), m.fan);
  statRow(c, x, 184, w, "FAN", v, st.s.fan, C_DISK);
}

inline void pageThermals(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "Thermals", C_WARN);

  // Temperatures: CPU, GPU, hotspot, then motherboard and drive sensors
  const int x = 8, w = 150, top = 34, hh = 198;
  card(c, x, top, w, hh);
  c.text("TEMPERATURES", x + 10, top + 12, F_TINY, DIM, D_ML);
  struct Row { const char* name; float t; };
  Row rows[10];
  int n = 0;
  if (m.cpuTemp >= 0) rows[n++] = {"CPU", m.cpuTemp};
  if (m.gpuTemp >= 0) rows[n++] = {"GPU", m.gpuTemp};
  if (m.gpuHotspot >= 0) rows[n++] = {"Hotspot", m.gpuHotspot};
  for (int i = 0; i < m.tempCount && n < 10; i++) rows[n++] = {m.temps[i].name, m.temps[i].celsius};
  for (int i = 0; i < m.diskCount && n < 10; i++)
    if (m.disks[i].temp >= 0) rows[n++] = {"Drive", m.disks[i].temp};
  if (n == 0) {
    sensorsHint(c, x, top + 14, w, hh - 14, "No temperatures");
  } else {
    const int maxRows = 6, rowH = 29;
    for (int i = 0; i < n && i < maxRows; i++) {
      int ry = top + 34 + i * rowH;
      c.text(rows[i].name, x + 10, ry, F_SMALL, TEXT, D_ML);
      tempText(c, rows[i].t, x + w - 10, ry, F_SMALL, tempColor(rows[i].t), D_MR);
      bar(c, x + 10, ry + 9, w - 20, 3, rows[i].t, tempColor(rows[i].t), CARD2);
    }
  }

  // Fans with spinning icons
  const int fx = 166, fw = 146;
  card(c, fx, top, fw, hh);
  c.text("FANS", fx + 10, top + 12, F_TINY, DIM, D_ML);
  if (m.fanCount == 0) {
    sensorsHint(c, fx, top + 14, fw, hh - 14, "No fan speeds");
    return;
  }
  const int maxFans = 5, rowH = 34;
  for (int i = 0; i < m.fanCount && i < maxFans; i++) {
    int ry = top + 40 + i * rowH;
    const FanInfo& f = m.fans[i];
    fanIcon(c, fx + 20, ry, 11, f.rpm, st.ms, f.rpm > 0 ? C_CPU : FAINT);
    c.text(f.name, fx + 38, ry - 6, F_SMALL, TEXT, D_ML);
    char v[16];
    if (f.rpm > 0) snprintf(v, sizeof(v), "%d RPM", f.rpm);
    else snprintf(v, sizeof(v), "stopped");
    c.text(v, fx + 38, ry + 8, F_TINY, f.rpm > 0 ? DIM : C_WARN, D_ML);
  }
  if (m.fanCount > maxFans) {
    char more[24];
    snprintf(more, sizeof(more), "+%d more", m.fanCount - maxFans);
    c.text(more, fx + fw - 10, top + 12, F_TINY, DIM, D_MR);
  }
}

inline void pageProcesses(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "Processes", C_RAM);
  card(c, 8, 32, 304, 200);
  c.text("PROGRAM", 18, 44, F_TINY, DIM, D_ML);
  c.text("CPU", 238, 44, F_TINY, DIM, D_MR);
  c.text("MEMORY", 302, 44, F_TINY, DIM, D_MR);
  if (m.procCount >= 0) {
    char v[20];
    snprintf(v, sizeof(v), "%d running", (int)m.procCount);
    c.text(v, 120, 44, F_TINY, FAINT, D_ML);
  }
  if (m.procListCount == 0) {
    c.text("No process data yet", 160, 130, F_SMALL, DIM, D_MC);
    return;
  }
  float top = 5;  // bar scale: the busiest program, at least 5%
  for (int i = 0; i < m.procListCount; i++) if (m.procs[i].cpu > top) top = m.procs[i].cpu;
  const int rowH = 30;
  for (int i = 0; i < m.procListCount && i < 6; i++) {
    const ProcInfo& p = m.procs[i];
    int ry = 66 + i * rowH;
    c.text(p.name, 18, ry - 2, F_SMALL, TEXT, D_ML);
    bar(c, 18, ry + 9, 150, 3, p.cpu * 100 / top, mix(C_RAM, CARD, 0.15f * i), CARD2);
    char v[16];
    snprintf(v, sizeof(v), p.cpu >= 10 ? "%.0f%%" : "%.1f%%", p.cpu);
    c.text(v, 238, ry + 2, F_SMALL, p.cpu >= 50 ? C_WARN : TEXT, D_MR);
    fmtMB(v, sizeof(v), (float)p.memMB);
    c.text(v, 302, ry + 2, F_SMALL, DIM, D_MR);
  }
}

inline void pageStorage(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  header(c, st, "Storage", C_DISK);
  char v[32], total[12];

  // Drives (space)
  const int maxDrives = 3;
  int nd = m.driveCount < maxDrives ? m.driveCount : maxDrives;
  if (nd == 0 && m.disk >= 0) {  // older PC script: only the system drive percentage
    card(c, 8, 34, 304, 44);
    c.text("System drive", 18, 50, F_SMALL, TEXT, D_ML);
    fmtPct(v, sizeof(v), m.disk);
    c.text(v, 302, 50, F_SMALL, TEXT, D_MR);
    bar(c, 18, 64, 284, 5, st.s.disk, loadColor(m.disk, C_DISK), CARD2);
    nd = 1;
  }
  for (int i = 0; i < nd && m.driveCount > 0; i++) {
    const DriveInfo& d = m.drives[i];
    int y = 34 + i * 46;
    card(c, 8, y, 304, 40);
    // Windows drive letters arrive without their colon ("C"); put it back
    if (strlen(d.name) == 1) snprintf(v, sizeof(v), "%s:", d.name);
    else snprintf(v, sizeof(v), "%s", d.name);
    c.text(v, 20, y + 20, F_MED, TEXT, D_ML);
    fmtGB(total, sizeof(total), d.totalGB);
    snprintf(v, sizeof(v), "%d%% of %s", (int)lroundf(d.usedPct), total);
    c.text(v, 302, y + 12, F_TINY, DIM, D_MR);
    uint32_t col = d.usedPct >= 95 ? C_HOT : d.usedPct >= 90 ? C_WARN : C_DISK;
    bar(c, 62, y + 24, 240, 6, d.usedPct, col, CARD2);
  }

  // Activity and drive health
  const int y = 34 + (nd > 0 ? nd : 1) * 46, hh = 232 - y;
  if (hh < 40) return;
  card(c, 8, y, 120, hh);
  c.text("ACTIVITY", 18, y + 12, F_TINY, DIM, D_ML);
  if (hh >= 80) {  // roomy: labels and values
    arrow(c, 22, y + 34, true, C_DISK);
    c.text("READ", 32, y + 30, F_TINY, DIM, D_ML);
    fmtRate(v, sizeof(v), m.diskReadKBs);
    c.text(v, 32, y + 43, F_SMALL, TEXT, D_ML);
    arrow(c, 22, y + 66, false, C_UP);
    c.text("WRITE", 32, y + 62, F_TINY, DIM, D_ML);
    fmtRate(v, sizeof(v), m.diskWriteKBs);
    c.text(v, 32, y + 75, F_SMALL, TEXT, D_ML);
  } else {         // compact: arrows only
    arrow(c, 22, y + 29, true, C_DISK);
    fmtRate(v, sizeof(v), m.diskReadKBs);
    c.text(v, 32, y + 29, F_SMALL, TEXT, D_ML);
    arrow(c, 22, y + 46, false, C_UP);
    fmtRate(v, sizeof(v), m.diskWriteKBs);
    c.text(v, 32, y + 46, F_SMALL, TEXT, D_ML);
  }

  card(c, 136, y, 176, hh);
  c.text("DRIVE HEALTH", 146, y + 12, F_TINY, DIM, D_ML);
  if (m.diskCount == 0) {
    c.text("needs LibreHardwareMonitor", 224, y + hh / 2 + 6, F_TINY, FAINT, D_MC);
    return;
  }
  int rows = (hh - 22) / 30;
  for (int i = 0; i < m.diskCount && i < rows; i++) {
    const DiskInfo& d = m.disks[i];
    int ry = y + 32 + i * 30;
    fitText(c, d.name, 146, ry - 4, 160, TEXT, D_ML);
    if (d.life >= 0) {
      snprintf(v, sizeof(v), "%d%% life", (int)lroundf(d.life));
      c.text(v, 146, ry + 10, F_TINY, d.life <= 25 ? C_WARN : C_OK, D_ML);
    }
    if (d.temp >= 0) tempText(c, d.temp, 302, ry + 10, F_TINY, tempColor(d.temp), D_MR);
  }
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
  graphCard(c, 8, 99, 304, 64, "GPU", m.hasGpu() ? v : "n/a", m.hasGpu() ? note : nullptr,
            h.gpu.count, [&](int i) { return h.gpu.v[i] / 100.0f; }, C_GPU);

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

inline void pageClock(Canvas& c, const State& st) {
  const Metrics& m = st.m;
  c.text(m.time, W / 2, 74, F_CLOCK, TEXT, D_MC);
  char line[48], up[24];
  if (m.uptimeS >= 0) {
    fmtUptime(up, sizeof(up), m.uptimeS);
    snprintf(line, sizeof(line), "%s  -  up %s", m.date, up);
  } else {
    snprintf(line, sizeof(line), "%s", m.date);
  }
  c.text(line, W / 2, 128, F_SMALL, DIM, D_MC);
  if (m.hasHealth()) healthBadge(c, W / 2 + 22, 150, m.healthScore);

  char v[12];
  const int y = 168, w = 72, gap = 6, x0 = (W - (4 * w + 3 * gap)) / 2;
  fmtPct(v, sizeof(v), m.cpu);
  miniStat(c, x0, y, w, "CPU", v, st.s.cpu, C_CPU);
  fmtPct(v, sizeof(v), m.gpu);
  miniStat(c, x0 + (w + gap), y, w, "GPU", m.hasGpu() ? v : "n/a", st.s.gpu, C_GPU);
  fmtPct(v, sizeof(v), m.ram);
  miniStat(c, x0 + 2 * (w + gap), y, w, "RAM", v, st.s.ram, C_RAM);
  // Last tile: CPU temperature if known, otherwise GPU temperature
  bool cpuT = m.cpuTemp >= 0;
  float t = cpuT ? m.cpuTemp : m.gpuTemp;
  miniStat(c, x0 + 3 * (w + gap), y, w, cpuT ? "CPU TEMP" : "GPU TEMP", "",
           cpuT ? st.s.cpuTemp : st.s.gpuTemp, tempColor(t));
  tempText(c, t, x0 + 3 * (w + gap) + w / 2, y + 27, F_MED, TEXT, D_MC);

  pageDots(c, st.page, W / 2, 230);
}

// Page menu: a grid of every page
static const int MENU_COLS = 5, MENU_GAP = 6, MENU_X = 8, MENU_Y = 34;
inline void menuTile(int i, int& x, int& y, int& w, int& h) {
  w = (W - 2 * MENU_X - (MENU_COLS - 1) * MENU_GAP) / MENU_COLS;
  h = (H - MENU_Y - 8 - MENU_GAP) / 2;
  x = MENU_X + (i % MENU_COLS) * (w + MENU_GAP);
  y = MENU_Y + (i / MENU_COLS) * (h + MENU_GAP);
}

// The "Touch setup" button in the menu's title bar
static const int CAL_BTN_X = 214, CAL_BTN_W = 98;
inline bool menuCalibrateHit(int px, int py) {
  return py < HEADER_H && px >= CAL_BTN_X - 6;
}

// Which page tile is at (x, y), or -1
inline int menuHit(int px, int py) {
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x, y, w, h;
    menuTile(i, x, y, w, h);
    if (px >= x && px < x + w && py >= y && py < y + h) return i;
  }
  return -1;
}

inline void pageMenu(Canvas& c, const State& st) {
  c.text("Pages", 14, 14, F_SMALL, TEXT, D_ML);
  c.rrect(CAL_BTN_X, 4, CAL_BTN_W, 20, 10, CARD);
  c.text("Touch setup", CAL_BTN_X + CAL_BTN_W / 2, 14, F_TINY, DIM, D_MC);
  for (int i = 0; i < PAGE_COUNT; i++) {
    int x, y, w, h;
    menuTile(i, x, y, w, h);
    bool cur = i == st.page;
    uint32_t accent = pageAccent(st, i);
    c.rrect(x, y, w, h, 10, cur ? mix(accent, BG, 0.75f) : CARD);
    pageIcon(c, i, x + w / 2, y + h / 2 - 12, cur ? TEXT : accent);
    c.text(PAGE_NAMES[i], x + w / 2, y + h - 18, F_TINY, cur ? TEXT : DIM, D_MC);
  }
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

static const uint32_t TAP_RING_MS = 350;

// Fading ring where the screen was touched, so it's clear where taps land
inline void tapRing(Canvas& c, const State& st) {
  uint32_t age = st.ms - st.tapMs;
  if (st.tapMs == 0 || age >= TAP_RING_MS) return;
  float t = age / (float)TAP_RING_MS;
  uint32_t col = mix(TEXT, BG, t);
  int r = 8 + (int)(t * 14);
  c.ring(st.tapX, st.tapY, r, col);
  c.ring(st.tapX, st.tapY, r - 1, col);
}

// True if the current screen has something moving even without new data
inline bool needsAnimation(const State& st) {
  if (st.tapMs != 0 && st.ms - st.tapMs < TAP_RING_MS) return true;
  if (!st.connected) return true;  // spinner
  if (st.menuOpen) return false;
  if (st.page == PAGE_THERMALS)
    for (int i = 0; i < st.m.fanCount; i++) if (st.m.fans[i].rpm > 0) return true;
  return false;
}

// Draws the whole screen (or the part of it inside this canvas's strip)
inline void drawPage(Canvas& c, const State& st) {
  if (!st.connected) { pageWaiting(c, st); return; }
  if (st.menuOpen) { pageMenu(c, st); return; }
  switch (st.page) {
    case PAGE_OVERVIEW:  pageOverview(c, st); break;
    case PAGE_HEALTH:    pageHealth(c, st); break;
    case PAGE_CPU:       pageCpu(c, st); break;
    case PAGE_GPU:       pageGpu(c, st); break;
    case PAGE_THERMALS:  pageThermals(c, st); break;
    case PAGE_PROCESSES: pageProcesses(c, st); break;
    case PAGE_STORAGE:   pageStorage(c, st); break;
    case PAGE_HISTORY:   pageHistory(c, st); break;
    case PAGE_NETWORK:   pageNetwork(c, st); break;
    default:             pageClock(c, st); break;
  }
}

inline void drawFrame(Canvas& c, const State& st) {
  c.clear(BG);
  drawPage(c, st);
  tapRing(c, st);
}

}  // namespace ui
