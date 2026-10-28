// Renders every dashboard page to a PNG using the firmware's own drawing code.
// Usage: ui_preview <output_dir>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <cmath>
#include <cstdio>
#include <string>

#include "dashboard_ui.h"

// Plausible-looking sample data: a gaming session with a spike of activity
static void fillSample(ui::State& st) {
  unsigned seed = 7;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) & 0x7FFF) / 32767.0f; };
  for (int i = 0; i < HISTORY_LEN; i++) {
    Metrics m;
    m.clear();
    float t = i / (float)HISTORY_LEN;
    bool busy = t > 0.45f && t < 0.62f;
    m.cpu = 18 + 10 * sinf(i / 7.0f) + rnd() * 8 + (busy ? 45 : 0);
    m.gpu = 25 + 15 * sinf(i / 11.0f) + rnd() * 6 + (busy ? 50 : t > 0.62f ? 30 : 0);
    m.ram = 52 + t * 9 + rnd() * 1.5f;
    m.downKBs = (t > 0.15f && t < 0.35f) ? 9000 + rnd() * 4000 : rnd() * 300;
    m.upKBs = 60 + rnd() * 200 + (busy ? 900 : 0);
    st.hist.add(m);
  }
  Metrics& m = st.m;
  m.clear();
  m.cpu = 37; m.cpuThreads = 16;
  m.ram = 61; m.ramUsedMB = 9984; m.ramTotalMB = 16310;
  m.gpu = 72; m.gpuTemp = 64; m.vramUsedMB = 3120; m.vramTotalMB = 8192;
  m.gpuPowerW = 145; m.gpuPowerLimitW = 215; m.fan = 45;
  m.downKBs = 12697; m.upKBs = 380; m.disk = 79;
  strcpy(m.time, "14:12");
  strcpy(m.date, "Wed 28 Oct");
  strcpy(m.gpuName, "RTX 2070 SUPER");
  while (st.s.step(m)) {}  // settle the animated values
  st.connected = true;
}

// Draws the screen in two 120-row strips, exactly like the ESP32 does
static bool renderPng(const ui::State& st, const std::string& path) {
  LGFX_Sprite full, strip;
  full.setColorDepth(16);
  strip.setColorDepth(16);
  if (!full.createSprite(ui::W, ui::H) || !strip.createSprite(ui::W, ui::H / 2)) return false;
  for (int oy = 0; oy < ui::H; oy += strip.height()) {
    ui::Canvas c{&strip, oy, strip.height()};
    ui::drawFrame(c, st);
    strip.pushSprite(&full, 0, oy);
  }
  size_t len = 0;
  void* png = full.createPng(&len, 0, 0, ui::W, ui::H);
  if (!png) return false;
  FILE* f = fopen(path.c_str(), "wb");
  bool ok = f && fwrite(png, 1, len, f) == len;
  if (f) fclose(f);
  free(png);
  return ok;
}

// Extra renders for checking awkward states: no GPU, and just after connecting
static int renderEdgeCases(const std::string& out) {
  ui::State st;
  for (int i = 0; i < 4; i++) {
    Metrics m;
    m.clear();
    m.cpu = 10 + i * 5; m.ram = 40; m.downKBs = 20 * i; m.upKBs = 3;
    st.hist.add(m);
  }
  st.m.clear();
  st.m.cpu = 25; st.m.ram = 40; st.m.disk = 55; st.m.downKBs = 60; st.m.upKBs = 3;
  strcpy(st.m.time, "09:05");
  strcpy(st.m.date, "Thu 29 Oct");
  st.connected = true;
  while (st.s.step(st.m)) {}
  const char* names[] = {"overview", "gpu", "history", "network", "clock"};
  int failures = 0;
  for (int p = 0; p < ui::PAGE_COUNT; p++) {
    st.page = p;
    failures += !renderPng(st, out + "/edge_nogpu_" + names[p] + ".png");
  }
  // Mid-animation frame: gauges partway through sweeping up
  ui::State anim;
  fillSample(anim);
  anim.s = ui::Smoothed();
  for (int i = 0; i < 3; i++) anim.s.step(anim.m);
  failures += !renderPng(anim, out + "/edge_animating.png");
  printf("edge cases rendered to %s (%d failures)\n", out.c_str(), failures);
  return failures;
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  if (argc > 2 && std::string(argv[2]) == "--edge-cases") return renderEdgeCases(out);
  ui::State st;
  fillSample(st);
  const char* names[] = {"overview", "gpu", "history", "network", "clock"};
  int failures = 0;
  for (int p = 0; p < ui::PAGE_COUNT; p++) {
    st.page = p;
    std::string path = out + "/screen_" + names[p] + ".png";
    bool ok = renderPng(st, path);
    printf("%s %s\n", ok ? "wrote" : "FAILED", path.c_str());
    failures += !ok;
  }
  ui::State waiting;
  waiting.ms = 200;
  std::string path = out + "/screen_waiting.png";
  bool ok = renderPng(waiting, path);
  printf("%s %s\n", ok ? "wrote" : "FAILED", path.c_str());
  return failures + !ok;
}
