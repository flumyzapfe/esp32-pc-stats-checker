// Renders every dashboard page to PNG using the firmware's own drawing code.
//
// Usage:
//   ui_preview <output_dir>                  sample data -> screen_<page>.png, screen_menu.png
//   ui_preview <output_dir> --edge-cases     awkward states -> edge_*.png
//   ui_preview <output_dir> --line "<line>"  render a real line from pc_stats.py --sensors
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include <cmath>
#include <cstdio>
#include <string>

#include "dashboard_ui.h"

// A typical gaming PC mid-session, in the same format pc_stats.py sends
static const char* SAMPLE_LINE =
    "cpu=37,cpun=16,cput=67.5,cpuw=88.2,cpuc=4450,ram=61,ramu=9984,ramt=16310,gpu=72,gput=64,"
    "gpuh=76,vram=3120,vramt=8192,gpw=145.5,gpwl=215,fan=45,gclk=1905,gmclk=7000,dn=12697,up=380,"
    "disk=79,dr=2048,dw=512,procn=287,upt=273600,hs=88,sens=1,t=14:12,d=Wed 28 Oct,"
    "gn=RTX 2070 SUPER,cpunm=Ryzen 7 5800X,hn=jasperpc,os=Windows 11,"
    "cores=42;18;65;12;30;9;77;25;14;33;51;8;20;44;16;29,"
    "fans=CPU:1180;Pump:2650;Sys 1:820;Sys 2:0;GPU 1:1450,"
    "temps=Motherboard:38;VRM MOS:52;Chipset:47,"
    "procs=chrome:12.4:2310;Cyberpunk2077:9.8:6120;Discord:3.1:412;Spotify:1.4:298;"
    "explorer:1.2:156;pc_stats:0.6:48,"
    "drv=C:79:932;D:41:1863,dsk=Samsung 970 EVO Plus:41:98;WD Blue 2TB:34:91,"
    "al=1:Drive C 91% full;0:Restart soon (up 3d),"
    "ok=CPU temp normal (68C);GPU temp normal (64C);Memory OK (61% used);4 fans spinning";

// Fills history with plausible curves: a spike of activity in the middle
static void fillHistory(ui::State& st, float cpuTempBase, bool gpu) {
  unsigned seed = 7;
  auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return ((seed >> 16) & 0x7FFF) / 32767.0f; };
  for (int i = 0; i < HISTORY_LEN; i++) {
    Metrics m;
    m.clear();
    float t = i / (float)HISTORY_LEN;
    bool busy = t > 0.45f && t < 0.62f;
    m.cpu = 18 + 10 * sinf(i / 7.0f) + rnd() * 8 + (busy ? 45 : 0);
    if (gpu) m.gpu = 25 + 15 * sinf(i / 11.0f) + rnd() * 6 + (busy ? 50 : t > 0.62f ? 30 : 0);
    m.ram = 52 + t * 9 + rnd() * 1.5f;
    if (cpuTempBase > 0) m.cpuTemp = cpuTempBase + 6 * sinf(i / 9.0f) + rnd() * 2 + (busy ? 14 : 0);
    if (gpu) m.gpuTemp = 55 + (busy ? 12 : 0) + rnd() * 2;
    m.downKBs = (t > 0.15f && t < 0.35f) ? 9000 + rnd() * 4000 : rnd() * 300;
    m.upKBs = 60 + rnd() * 200 + (busy ? 900 : 0);
    st.hist.add(m);
  }
}

static bool load(ui::State& st, const char* line) {
  char buf[LINE_MAX_LEN];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  if (!proto::parseLine(buf, st.m)) return false;
  while (st.s.step(st.m)) {}  // settle the animated values
  st.connected = true;
  st.ms = 400;
  return true;
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
  printf("%s %s\n", ok ? "wrote" : "FAILED", path.c_str());
  return ok;
}

static std::string slug(int page) {
  std::string s = ui::PAGE_NAMES[page];
  for (char& ch : s) ch = (char)tolower(ch);
  return s;
}

static int renderAllPages(ui::State& st, const std::string& prefix) {
  int failures = 0;
  for (int p = 0; p < ui::PAGE_COUNT; p++) {
    st.page = p;
    failures += !renderPng(st, prefix + slug(p) + ".png");
  }
  return failures;
}

static int renderSamples(const std::string& out) {
  ui::State st;
  fillHistory(st, 58, true);
  if (!load(st, SAMPLE_LINE)) { printf("sample line did not parse\n"); return 1; }
  int failures = renderAllPages(st, out + "/screen_");
  st.page = ui::PAGE_THERMALS;
  st.menuOpen = true;
  failures += !renderPng(st, out + "/screen_menu.png");
  ui::State waiting;
  waiting.ms = 200;
  failures += !renderPng(waiting, out + "/screen_waiting.png");
  return failures;
}

static int renderEdgeCases(const std::string& out) {
  int failures = 0;

  // 1. The PC script from before the health upgrade: no sensors, lists or health
  ui::State old;
  fillHistory(old, 0, true);
  load(old, "cpu=25,cpun=8,ram=40,ramu=6400,ramt=16000,gpu=12,gput=41,vram=900,vramt=8192,"
            "gpw=30,gpwl=215,fan=30,dn=60,up=3,disk=55,t=09:05,d=Thu 29 Oct,gn=RTX 2070 SUPER");
  failures += renderAllPages(old, out + "/edge_oldscript_");

  // 2. A laptop with no NVIDIA GPU and no LibreHardwareMonitor, just started
  ui::State bare;
  for (int i = 0; i < 4; i++) { Metrics m; m.clear(); m.cpu = 10 + i * 5; m.ram = 40; bare.hist.add(m); }
  load(bare, "cpu=25,cpun=4,ram=40,ramu=3200,ramt=8000,dn=60,up=3,disk=55,dr=10,dw=4,procn=143,"
             "upt=420,hs=100,sens=0,t=09:05,d=Thu 29 Oct,cpunm=Core i5-8250U,hn=laptop,"
             "os=Windows 10,cores=30;20;25;24;31;18;22;27,drv=C:55:238,"
             "procs=Teams:6.2:512;chrome:4.0:890,al=0:Temps need LibreHWMon");
  failures += renderAllPages(bare, out + "/edge_bare_");

  // 3. A PC in trouble: hot, full drives, stopped CPU fan, 32 threads, long names
  ui::State bad;
  fillHistory(bad, 82, true);
  load(bad, "cpu=97,cpun=32,cput=96,cpuw=242,cpuc=5120,ram=96,ramu=62900,ramt=65536,gpu=99,"
            "gput=89,gpuh=104,vram=23900,vramt=24576,gpw=448,gpwl=450,fan=100,gclk=2730,"
            "gmclk=10501,dn=96000,up=48000,disk=97,dr=812000,dw=402000,procn=412,upt=1900000,"
            "hs=0,sens=1,t=23:59,d=Sun 31 Dec,gn=RTX 4090,cpunm=Threadripper 7970X,"
            "hn=WORKSTATION-01,os=Windows 11,"
            "cores=99;98;100;97;99;100;96;99;98;100;99;97;100;98;99;100;97;99;100;98;96;99;100;"
            "97;99;98;100;99;97;100;98;99,"
            "fans=CPU:0;Pump:0;Sys 1:1900;Sys 2:1880;Sys 3:1850;Sys 4:1870;GPU 1:3100;GPU 2:3090,"
            "temps=Motherboard:58;VRM MOS:98;Chipset:71;PCH:69,"
            "procs=blender-renderer-x:61.2:31200;ffmpeg:22.4:2100;chrome:8.1:9100;"
            "MsMpEng:3.9:310;dwm:1.1:150;explorer:0.4:120,"
            "drv=C:97:1863;D:99:3726;E:96:931,dsk=Samsung 990 PRO:79:4;Seagate Barracuda:52:12,"
            "al=2:CPU very hot (96C);2:CPU fan not spinning;2:GPU hotspot 104C;"
            "2:Memory 96% full;2:Drive C 97% full;1:CPU at 98% for 1 min");
  failures += renderAllPages(bad, out + "/edge_bad_");

  // 4. Mid-animation: gauges partway through sweeping up after connecting
  ui::State anim;
  fillHistory(anim, 58, true);
  load(anim, SAMPLE_LINE);
  anim.s = ui::Smoothed();
  for (int i = 0; i < 3; i++) anim.s.step(anim.m);
  failures += !renderPng(anim, out + "/edge_animating.png");
  return failures;
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  std::string mode = argc > 2 ? argv[2] : "";
  if (mode == "--edge-cases") return renderEdgeCases(out);
  if (mode == "--line" && argc > 3) {
    ui::State st;
    if (!load(st, argv[3])) { printf("That line didn't parse (is cpu= in it?)\n"); return 1; }
    fillHistory(st, st.m.cpuTemp > 0 ? st.m.cpuTemp : 0, st.m.hasGpu());
    return renderAllPages(st, out + "/line_");
  }
  return renderSamples(out);
}
