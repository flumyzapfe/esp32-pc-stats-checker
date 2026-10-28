// dashboard_data.h - stats model, history buffers and serial protocol parsing.
//
// Plain C++ with no Arduino or display dependencies, so the same code runs on the
// ESP32 and in the desktop preview tool (tools/ui_preview).
//
// Protocol: one line per second of comma-separated key=value pairs, e.g.
//   cpu=37,cpun=16,ram=61,ramu=10034,ramt=16310,gpu=72,gput=64,vram=3120,vramt=8192,
//   gpw=145,gpwl=215,fan=45,dn=12697,up=380,disk=48,t=14:12,d=Wed 28 Oct,gn=RTX 2070 SUPER
// Unknown keys are ignored and missing keys read as "unknown" (-1), so the PC script
// and firmware can gain new fields without breaking each other. The older positional
// format (cpu,ram,gpu,gpuTemp,down,up,disk,HH:MM,date) is still accepted.
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Latest reading from the PC. Every numeric field is -1 when unknown.
// ---------------------------------------------------------------------------
struct Metrics {
  float cpu, ram, gpu, gpuTemp, disk, fan;  // percentages / degrees C
  float ramUsedMB, ramTotalMB;
  float vramUsedMB, vramTotalMB;
  float gpuPowerW, gpuPowerLimitW;
  float downKBs, upKBs;
  int cpuThreads;
  char time[6];       // "HH:MM"
  char date[16];      // "Wed 28 Oct"
  char gpuName[24];   // "RTX 2070 SUPER"

  void clear() {
    cpu = ram = gpu = gpuTemp = disk = fan = -1;
    ramUsedMB = ramTotalMB = vramUsedMB = vramTotalMB = -1;
    gpuPowerW = gpuPowerLimitW = -1;
    downKBs = upKBs = -1;
    cpuThreads = -1;
    strcpy(time, "--:--");
    date[0] = '\0';
    gpuName[0] = '\0';
  }
  bool hasGpu() const { return gpu >= 0; }
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
};

static const int HISTORY_LEN = 150;  // 150 samples x 2 px = 300 px wide graphs, 2.5 minutes

struct History {
  Ring<uint8_t, HISTORY_LEN> cpu, gpu, ram;
  Ring<float, HISTORY_LEN> down, up;

  void add(const Metrics& m) {
    cpu.push(clampPct(m.cpu));
    gpu.push(clampPct(m.gpu));
    ram.push(clampPct(m.ram));
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

inline void setField(Metrics& m, const char* key, const char* val) {
  struct NumKey { const char* key; float Metrics::*field; };
  static const NumKey nums[] = {
    {"cpu", &Metrics::cpu},          {"ram", &Metrics::ram},
    {"ramu", &Metrics::ramUsedMB},   {"ramt", &Metrics::ramTotalMB},
    {"gpu", &Metrics::gpu},          {"gput", &Metrics::gpuTemp},
    {"vram", &Metrics::vramUsedMB},  {"vramt", &Metrics::vramTotalMB},
    {"gpw", &Metrics::gpuPowerW},    {"gpwl", &Metrics::gpuPowerLimitW},
    {"fan", &Metrics::fan},          {"disk", &Metrics::disk},
    {"dn", &Metrics::downKBs},       {"up", &Metrics::upKBs},
  };
  for (const NumKey& k : nums) {
    if (strcmp(key, k.key) == 0) { m.*(k.field) = (float)atof(val); return; }
  }
  if (strcmp(key, "cpun") == 0) m.cpuThreads = atoi(val);
  else if (strcmp(key, "t") == 0) copyText(m.time, sizeof(m.time), val);
  else if (strcmp(key, "d") == 0) copyText(m.date, sizeof(m.date), val);
  else if (strcmp(key, "gn") == 0) copyText(m.gpuName, sizeof(m.gpuName), val);
  // anything else: a newer PC script sending a field this firmware doesn't know yet
}

// Splits `line` in place on commas. Returns the number of fields.
inline int split(char* line, char** fields, int maxFields) {
  int n = 0;
  char* p = line;
  while (n < maxFields) {
    fields[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = '\0';
    p = c + 1;
  }
  return n;
}

// Parses one line into `m`. Returns false (leaving `m` untouched) if it isn't valid.
inline bool parseLine(char* line, Metrics& m) {
  char* f[32];
  int n = split(line, f, 32);
  Metrics out;
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
