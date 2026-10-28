// Tests for the serial protocol parser in dashboard_data.h (no display needed).
#include <cstdio>
#include <cstring>

#include "dashboard_data.h"

static int failures = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } \
  } while (0)

static bool parse(const char* text, Metrics& m) {
  char buf[256];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return proto::parseLine(buf, m);
}

// "test_protocol -" parses every line on stdin (e.g. piped from the Python script),
// so CI can check both sides of the protocol agree.
static int parseStdin() {
  char buf[512];
  int lines = 0, bad = 0;
  while (fgets(buf, sizeof(buf), stdin)) {
    buf[strcspn(buf, "\r\n")] = '\0';
    if (!buf[0]) continue;
    lines++;
    Metrics m;
    if (!proto::parseLine(buf, m)) { printf("REJECTED line %d\n", lines); bad++; continue; }
    printf("line %d: cpu=%g ram=%g gpu=%g temp=%g vram=%g/%g power=%g/%g fan=%g threads=%d "
           "down=%g up=%g disk=%g time=%s date=%s name=%s\n",
           lines, m.cpu, m.ram, m.gpu, m.gpuTemp, m.vramUsedMB, m.vramTotalMB, m.gpuPowerW,
           m.gpuPowerLimitW, m.fan, m.cpuThreads, m.downKBs, m.upKBs, m.disk, m.time, m.date,
           m.gpuName);
  }
  if (lines == 0) { printf("no input\n"); return 1; }
  return bad != 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "-") == 0) return parseStdin();

  Metrics m;
  m.clear();

  // Full key=value line from pc_stats.py
  CHECK(parse("cpu=37,cpun=16,ram=61,ramu=9984,ramt=16310,gpu=72,gput=64,vram=3120,vramt=8192,"
              "gpw=145.5,gpwl=215,fan=45,dn=12697,up=380,disk=79,t=14:12,d=Wed 28 Oct,gn=RTX 2070 SUPER", m));
  CHECK(m.cpu == 37 && m.cpuThreads == 16 && m.ram == 61 && m.gpu == 72 && m.gpuTemp == 64);
  CHECK(m.vramUsedMB == 3120 && m.vramTotalMB == 8192 && m.gpuPowerW == 145.5f && m.fan == 45);
  CHECK(m.downKBs == 12697 && m.upKBs == 380 && m.disk == 79);
  CHECK(strcmp(m.time, "14:12") == 0 && strcmp(m.date, "Wed 28 Oct") == 0);
  CHECK(strcmp(m.gpuName, "RTX 2070 SUPER") == 0 && m.hasGpu());

  // Missing keys read as unknown; unknown keys are ignored
  CHECK(parse("cpu=5,ram=50,future_field=123,t=09:00", m));
  CHECK(m.cpu == 5 && m.gpu == -1 && !m.hasGpu() && m.fan == -1 && m.cpuThreads == -1);

  // Legacy positional format still works
  CHECK(parse("37,61,72,64,12697,380,48,14:12,Wed 28 Oct", m));
  CHECK(m.cpu == 37 && m.gpuTemp == 64 && m.disk == 48 && strcmp(m.date, "Wed 28 Oct") == 0);

  // Invalid lines are rejected and leave the previous reading untouched
  CHECK(!parse("", m));
  CHECK(!parse("hello world", m));
  CHECK(!parse("ram=50,t=10:00", m));  // cpu is required
  CHECK(!parse("1,2,3", m));
  CHECK(m.cpu == 37);

  // Over-long text values are truncated, not overflowed
  CHECK(parse("cpu=1,gn=NVIDIA GeForce RTX 4090 Founders Edition Extra Long,t=12:345678", m));
  CHECK(strlen(m.gpuName) == sizeof(m.gpuName) - 1 && strlen(m.time) == sizeof(m.time) - 1);

  // History ring buffer keeps the newest samples
  History h;
  for (int i = 0; i < HISTORY_LEN + 10; i++) { Metrics s; s.clear(); s.cpu = i % 101; h.add(s); }
  CHECK(h.cpu.count == HISTORY_LEN);
  CHECK(h.cpu.v[HISTORY_LEN - 1] == (HISTORY_LEN + 9) % 101);

  printf(failures ? "%d test(s) failed\n" : "All protocol tests passed\n", failures);
  return failures != 0;
}
