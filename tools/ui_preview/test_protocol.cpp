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
  char buf[LINE_MAX_LEN];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  return proto::parseLine(buf, m);
}

// "test_protocol -" parses every line on stdin (e.g. piped from the Python script),
// so CI can check both sides of the protocol agree.
static int parseStdin() {
  char buf[LINE_MAX_LEN + 2];
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

  // Health-upgrade fields: scalars and lists
  CHECK(parse("cpu=37,cput=67.5,cpuw=88.2,cpuc=4450,gpuh=76,gclk=1905,gmclk=7000,dr=2048,dw=512,"
              "procn=287,upt=273600,hs=88,sens=1,cpunm=Ryzen 7 5800X,hn=jasperpc,os=Windows 11,"
              "cores=42;18;65,ctemps=60;61,fans=CPU:1180;Sys 1:820,temps=Motherboard:38;VRM MOS:52,"
              "procs=chrome:12.4:2310;Discord:3.1:412,drv=C:79:932;D:41:1863,"
              "dsk=Samsung 970 EVO Plus:41:98,al=1:Drive C filling up (91%);0:Up 3 days - restart soon", m));
  CHECK(m.cpuTemp == 67.5f && m.cpuPowerW == 88.2f && m.cpuClockMHz == 4450 && m.gpuHotspot == 76);
  CHECK(m.gpuClockMHz == 1905 && m.gpuMemClockMHz == 7000 && m.diskReadKBs == 2048);
  CHECK(m.procCount == 287 && m.uptimeS == 273600 && m.healthScore == 88 && m.sensors == 1);
  CHECK(strcmp(m.cpuName, "Ryzen 7 5800X") == 0 && strcmp(m.hostName, "jasperpc") == 0);
  CHECK(m.coreCount == 3 && m.coreLoad[0] == 42 && m.coreLoad[2] == 65);
  CHECK(m.coreTempCount == 2 && m.coreTemp[1] == 61);
  CHECK(m.fanCount == 2 && strcmp(m.fans[1].name, "Sys 1") == 0 && m.fans[1].rpm == 820);
  CHECK(m.tempCount == 2 && strcmp(m.temps[1].name, "VRM MOS") == 0 && m.temps[1].celsius == 52);
  CHECK(m.procListCount == 2 && strcmp(m.procs[0].name, "chrome") == 0 && m.procs[0].cpu == 12.4f
        && m.procs[0].memMB == 2310);
  CHECK(m.driveCount == 2 && strcmp(m.drives[1].name, "D") == 0 && m.drives[1].totalGB == 1863);
  CHECK(m.diskCount == 1 && strcmp(m.disks[0].name, "Samsung 970 EVO Plus") == 0
        && m.disks[0].temp == 41 && m.disks[0].life == 98);
  CHECK(m.alertCount == 2 && m.alerts[0].level == 1
        && strcmp(m.alerts[0].text, "Drive C filling up (91%)") == 0 && m.alerts[1].level == 0);
  CHECK(m.hasHealth());

  // Lists: surplus items are dropped, malformed items skipped, values clamped
  CHECK(parse("cpu=1,fans=A:1;B:2;C:3;D:4;E:5;F:6;G:7;H:8;I:9;J:10,"
              "procs=bad;ok:1:2;alsobad:1,al=9:too high;-3:too low,cores=300;-5;50", m));
  CHECK(m.fanCount == MAX_FANS && strcmp(m.fans[7].name, "H") == 0 && m.fans[7].rpm == 8);
  CHECK(m.procListCount == 1 && strcmp(m.procs[0].name, "ok") == 0);
  CHECK(m.alertCount == 2 && m.alerts[0].level == 2 && m.alerts[1].level == 0);
  CHECK(m.coreCount == 3 && m.coreLoad[0] == 255 && m.coreLoad[1] == 0 && m.coreLoad[2] == 50);

  // A line without lists leaves them empty, and health unknown
  CHECK(parse("cpu=5,ram=50", m));
  CHECK(m.fanCount == 0 && m.coreCount == 0 && m.alertCount == 0 && !m.hasHealth() && m.sensors == -1);

  // History ring buffer keeps the newest samples
  History h;
  for (int i = 0; i < HISTORY_LEN + 10; i++) { Metrics s; s.clear(); s.cpu = i % 101; h.add(s); }
  CHECK(h.cpu.count == HISTORY_LEN);
  CHECK(h.cpu.v[HISTORY_LEN - 1] == (HISTORY_LEN + 9) % 101);

  printf(failures ? "%d test(s) failed\n" : "All protocol tests passed\n", failures);
  return failures != 0;
}
