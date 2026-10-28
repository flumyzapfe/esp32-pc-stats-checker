// dashboard_data.h - stats model, history buffers and serial protocol parsing.
//
// Plain C++ with no Arduino or display dependencies, so the same code runs on the
// ESP32 and in the desktop preview tool (tools/ui_preview).
//
// Protocol: one line per second of comma-separated key=value pairs. Lists use ';'
// between items and ':' between an item's parts:
//   cpu=37,cput=67.5,ram=61,gpu=72,...,fans=CPU:1180;Sys 1:820,procs=chrome:12.4:2310;...
// Unknown keys are ignored and missing keys read as "unknown" (-1), so the PC script
// and firmware can gain new fields without breaking each other. The older positional
// format (cpu,ram,gpu,gpuTemp,down,up,disk,HH:MM,date) is still accepted.
// host/pcstats/protocol.py is the other half of this.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const int LINE_MAX_LEN = 1024;   // longest line accepted from the PC

// ---------------------------------------------------------------------------
// List items
// ---------------------------------------------------------------------------
struct FanInfo     { char name[13]; int rpm; };
struct TempInfo    { char name[13]; float celsius; };
struct ProcInfo    { char name[17]; float cpu; int memMB; };
struct DriveInfo   { char name[7]; float usedPct; float totalGB; };
struct DiskInfo    { char name[21]; float temp; float life; };
struct AlertInfo   { uint8_t level; char text[35]; };  // level: 0 info, 1 warning, 2 critical

static const int MAX_CORES = 32, MAX_CORE_TEMPS = 16, MAX_FANS = 8, MAX_TEMPS = 8,
                 MAX_PROCS = 6, MAX_DRIVES = 4, MAX_DISKS = 4, MAX_ALERTS = 6, MAX_OK = 6;

// ---------------------------------------------------------------------------
// Latest reading from the PC. Every numeric field is -1 when unknown.
// ---------------------------------------------------------------------------
struct Metrics {
  // CPU
  float cpu, cpuTemp, cpuPowerW, cpuClockMHz;
  int cpuThreads;
  uint8_t coreLoad[MAX_CORES];
  int coreCount;
  uint8_t coreTemp[MAX_CORE_TEMPS];
  int coreTempCount;
  // Memory
  float ram, ramUsedMB, ramTotalMB;
  // GPU
  float gpu, gpuTemp, gpuHotspot, vramUsedMB, vramTotalMB, gpuPowerW, gpuPowerLimitW, fan,
        gpuClockMHz, gpuMemClockMHz;
  // Storage and network
  float disk, diskReadKBs, diskWriteKBs, downKBs, upKBs;
  // System
  float uptimeS, procCount, healthScore;
  int sensors;  // 1 = PC has LibreHardwareMonitor running (temps/fans), 0 = not, -1 = unknown
  char time[6];        // "HH:MM"
  char date[16];       // "Wed 28 Oct"
  char gpuName[24];    // "RTX 2070 SUPER"
  char cpuName[24];    // "Ryzen 7 5800X"
  char hostName[16];
  char osName[16];
  // Lists
  FanInfo fans[MAX_FANS];       int fanCount;
  TempInfo temps[MAX_TEMPS];    int tempCount;
  ProcInfo procs[MAX_PROCS];    int procListCount;
  DriveInfo drives[MAX_DRIVES]; int driveCount;
  DiskInfo disks[MAX_DISKS];    int diskCount;
  AlertInfo alerts[MAX_ALERTS]; int alertCount;
  char okChecks[MAX_OK][25];    int okCount;   // health checks that passed

  void clear() {
    memset(this, 0, sizeof(*this));
    float* unknown[] = {&cpu, &cpuTemp, &cpuPowerW, &cpuClockMHz, &ram, &ramUsedMB, &ramTotalMB,
                        &gpu, &gpuTemp, &gpuHotspot, &vramUsedMB, &vramTotalMB, &gpuPowerW,
                        &gpuPowerLimitW, &fan, &gpuClockMHz, &gpuMemClockMHz, &disk,
                        &diskReadKBs, &diskWriteKBs, &downKBs, &upKBs, &uptimeS, &procCount,
                        &healthScore};
    for (float* f : unknown) *f = -1;
    cpuThreads = -1;
    sensors = -1;
    strcpy(time, "--:--");
  }
  bool hasGpu() const { return gpu >= 0; }
  bool hasHealth() const { return healthScore >= 0; }
};

// ---------------------------------------------------------------------------
// Rolling history, one sample per reading. Oldest sample is index 0.
// ---------------------------------------------------------------------------
template <typename T, int N>
struct Ring {
  T v[N];
  int count = 0;
  void push(T x) {
    if (count == N) memmove(v, v + 1, sizeof(T) * (N - 1));
    else count++;
    v[count - 1] = x;
  }
  T maxValue() const {
    T m = 0;
    for (int i = 0; i < count; i++) if (v[i] > m) m = v[i];
    return m;
  }
  T minValue() const {
    if (count == 0) return 0;
    T m = v[0];
    for (int i = 1; i < count; i++) if (v[i] < m) m = v[i];
    return m;
  }
};

static const int HISTORY_LEN = 150;  // 150 samples x 2 px = 300 px wide graphs, 2.5 minutes

struct History {
  Ring<uint8_t, HISTORY_LEN> cpu, gpu, ram, cpuTemp, gpuTemp;
  Ring<float, HISTORY_LEN> down, up;

  void add(const Metrics& m) {
    cpu.push(clampPct(m.cpu));
    gpu.push(clampPct(m.gpu));
    ram.push(clampPct(m.ram));
    cpuTemp.push(clampPct(m.cpuTemp));
    gpuTemp.push(clampPct(m.gpuTemp));
    down.push(m.downKBs > 0 ? m.downKBs : 0);
    up.push(m.upKBs > 0 ? m.upKBs : 0);
  }
  static uint8_t clampPct(float x) { return x < 0 ? 0 : x > 100 ? 100 : (uint8_t)lroundf(x); }
};

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------
namespace proto {

inline void copyText(char* dst, size_t n, const char* src) {
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

// Splits `s` in place on `sep`. Returns the number of parts (at most maxParts; the last
// part keeps any remaining separators).
inline int split(char* s, char sep, char** parts, int maxParts) {
  int n = 0;
  char* p = s;
  while (n < maxParts) {
    parts[n++] = p;
    if (n == maxParts) break;
    char* c = strchr(p, sep);
    if (!c) break;
    *c = '\0';
    p = c + 1;
  }
  return n;
}

// Calls item(parts, count) for each ';'-separated item of a list value.
template <typename F>
inline int forEachItem(char* value, int maxItems, F item) {
  char* items[MAX_CORES + 1];
  if (maxItems > MAX_CORES) maxItems = MAX_CORES;
  int n = split(value, ';', items, maxItems + 1);  // one extra slot soaks up any surplus items
  if (n > maxItems) n = maxItems;
  int stored = 0;
  for (int i = 0; i < n; i++) {
    char* parts[4];
    int np = split(items[i], ':', parts, 4);
    if (item(stored, parts, np)) stored++;
  }
  return stored;
}

inline uint8_t pctByte(const char* s) {
  float v = (float)atof(s);
  return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)lroundf(v);
}

inline bool setList(Metrics& m, const char* key, char* val) {
  if (strcmp(key, "cores") == 0) {
    m.coreCount = forEachItem(val, MAX_CORES, [&](int i, char** p, int) {
      m.coreLoad[i] = pctByte(p[0]); return true; });
  } else if (strcmp(key, "ctemps") == 0) {
    m.coreTempCount = forEachItem(val, MAX_CORE_TEMPS, [&](int i, char** p, int) {
      m.coreTemp[i] = pctByte(p[0]); return true; });
  } else if (strcmp(key, "fans") == 0) {
    m.fanCount = forEachItem(val, MAX_FANS, [&](int i, char** p, int n) {
      if (n < 2) return false;
      copyText(m.fans[i].name, sizeof(m.fans[i].name), p[0]);
      m.fans[i].rpm = atoi(p[1]);
      return true; });
  } else if (strcmp(key, "temps") == 0) {
    m.tempCount = forEachItem(val, MAX_TEMPS, [&](int i, char** p, int n) {
      if (n < 2) return false;
      copyText(m.temps[i].name, sizeof(m.temps[i].name), p[0]);
      m.temps[i].celsius = (float)atof(p[1]);
      return true; });
  } else if (strcmp(key, "procs") == 0) {
    m.procListCount = forEachItem(val, MAX_PROCS, [&](int i, char** p, int n) {
      if (n < 3) return false;
      copyText(m.procs[i].name, sizeof(m.procs[i].name), p[0]);
      m.procs[i].cpu = (float)atof(p[1]);
      m.procs[i].memMB = atoi(p[2]);
      return true; });
  } else if (strcmp(key, "drv") == 0) {
    m.driveCount = forEachItem(val, MAX_DRIVES, [&](int i, char** p, int n) {
      if (n < 3) return false;
      copyText(m.drives[i].name, sizeof(m.drives[i].name), p[0]);
      m.drives[i].usedPct = (float)atof(p[1]);
      m.drives[i].totalGB = (float)atof(p[2]);
      return true; });
  } else if (strcmp(key, "dsk") == 0) {
    m.diskCount = forEachItem(val, MAX_DISKS, [&](int i, char** p, int n) {
      if (n < 3) return false;
      copyText(m.disks[i].name, sizeof(m.disks[i].name), p[0]);
      m.disks[i].temp = (float)atof(p[1]);
      m.disks[i].life = (float)atof(p[2]);
      return true; });
  } else if (strcmp(key, "al") == 0) {
    m.alertCount = forEachItem(val, MAX_ALERTS, [&](int i, char** p, int n) {
      if (n < 2) return false;
      int lvl = atoi(p[0]);
      m.alerts[i].level = (uint8_t)(lvl < 0 ? 0 : lvl > 2 ? 2 : lvl);
      copyText(m.alerts[i].text, sizeof(m.alerts[i].text), p[1]);
      return true; });
  } else if (strcmp(key, "ok") == 0) {
    m.okCount = forEachItem(val, MAX_OK, [&](int i, char** p, int) {
      copyText(m.okChecks[i], sizeof(m.okChecks[i]), p[0]); return true; });
  } else {
    return false;
  }
  return true;
}

inline void setField(Metrics& m, const char* key, char* val) {
  struct NumKey { const char* key; float Metrics::*field; };
  static const NumKey nums[] = {
    {"cpu", &Metrics::cpu},            {"cput", &Metrics::cpuTemp},
    {"cpuw", &Metrics::cpuPowerW},     {"cpuc", &Metrics::cpuClockMHz},
    {"ram", &Metrics::ram},            {"ramu", &Metrics::ramUsedMB},
    {"ramt", &Metrics::ramTotalMB},    {"gpu", &Metrics::gpu},
    {"gput", &Metrics::gpuTemp},       {"gpuh", &Metrics::gpuHotspot},
    {"vram", &Metrics::vramUsedMB},    {"vramt", &Metrics::vramTotalMB},
    {"gpw", &Metrics::gpuPowerW},      {"gpwl", &Metrics::gpuPowerLimitW},
    {"fan", &Metrics::fan},            {"gclk", &Metrics::gpuClockMHz},
    {"gmclk", &Metrics::gpuMemClockMHz}, {"disk", &Metrics::disk},
    {"dr", &Metrics::diskReadKBs},     {"dw", &Metrics::diskWriteKBs},
    {"dn", &Metrics::downKBs},         {"up", &Metrics::upKBs},
    {"upt", &Metrics::uptimeS},        {"procn", &Metrics::procCount},
    {"hs", &Metrics::healthScore},
  };
  for (const NumKey& k : nums) {
    if (strcmp(key, k.key) == 0) { m.*(k.field) = (float)atof(val); return; }
  }
  if (setList(m, key, val)) return;
  if (strcmp(key, "cpun") == 0) m.cpuThreads = atoi(val);
  else if (strcmp(key, "sens") == 0) m.sensors = atoi(val);
  else if (strcmp(key, "t") == 0) copyText(m.time, sizeof(m.time), val);
  else if (strcmp(key, "d") == 0) copyText(m.date, sizeof(m.date), val);
  else if (strcmp(key, "gn") == 0) copyText(m.gpuName, sizeof(m.gpuName), val);
  else if (strcmp(key, "cpunm") == 0) copyText(m.cpuName, sizeof(m.cpuName), val);
  else if (strcmp(key, "hn") == 0) copyText(m.hostName, sizeof(m.hostName), val);
  else if (strcmp(key, "os") == 0) copyText(m.osName, sizeof(m.osName), val);
  // anything else: a newer PC script sending a field this firmware doesn't know yet
}

// Parses one line into `m`. Returns false (leaving `m` untouched) if it isn't valid.
inline bool parseLine(char* line, Metrics& m) {
  char* f[64];
  int n = split(line, ',', f, 64);
  static Metrics out;  // static: Metrics is ~1.3 KB, kept off the stack
  out.clear();

  if (strchr(f[0], '=')) {                       // key=value format
    int known = 0;
    for (int i = 0; i < n; i++) {
      char* eq = strchr(f[i], '=');
      if (!eq) continue;
      *eq = '\0';
      setField(out, f[i], eq + 1);
      known++;
    }
    if (known == 0 || out.cpu < 0) return false;  // cpu is the one required field
  } else if (n == 9) {                            // legacy positional format
    out.cpu = (float)atof(f[0]);
    out.ram = (float)atof(f[1]);
    out.gpu = (float)atof(f[2]);
    out.gpuTemp = (float)atof(f[3]);
    out.downKBs = (float)atof(f[4]);
    out.upKBs = (float)atof(f[5]);
    out.disk = (float)atof(f[6]);
    copyText(out.time, sizeof(out.time), f[7]);
    copyText(out.date, sizeof(out.date), f[8]);
  } else {
    return false;
  }
  m = out;
  return true;
}

}  // namespace proto
