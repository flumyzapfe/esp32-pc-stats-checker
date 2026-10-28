// PC Stats Ticker - Arduino Nano + 0.91" SSD1306 OLED (128x32, I2C)
// https://github.com/flumyzapfe/esp32-pc-stats-checker
//
// Shows CPU and RAM bars plus the time, sent once a second by
//   python host/pc_stats.py --board nano
//
// Wiring: OLED GND->GND, VCC->5V, SCL->A5, SDA->A4
// Serial line format (115200 baud):  cpu,ram,HH:MM   e.g. 23,54,14:05
// Libraries: "Adafruit SSD1306" (+ "Adafruit GFX")

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 display(128, 32, &Wire, -1);

char buf[32];
byte len = 0;
int cpu = 0, ram = 0;
char clockStr[6] = "--:--";
unsigned long lastData = 0;

void drawWaiting() {
  display.clearDisplay();
  display.setCursor(0, 12);
  display.print(F("Waiting for PC..."));
  display.display();
}

void drawRow(int y, const __FlashStringHelper* label, int val) {
  val = constrain(val, 0, 100);
  display.setCursor(0, y);
  display.print(label);
  display.print(val);
  display.print('%');
  display.drawRect(54, y, 74, 7, SSD1306_WHITE);           // bar outline
  display.fillRect(56, y + 2, (70 * val) / 100, 3, SSD1306_WHITE); // bar fill
}

void drawStats() {
  display.clearDisplay();
  drawRow(0,  F("CPU "), cpu);
  drawRow(12, F("RAM "), ram);
  display.setCursor(0, 24);
  display.print(F("Time "));
  display.print(clockStr);
  display.display();
}

void parseLine() {
  if (sscanf(buf, "%d,%d,%5s", &cpu, &ram, clockStr) == 3) {
    lastData = millis();
    drawStats();
  }
}

void setup() {
  Serial.begin(115200);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // try 0x3D if screen stays blank
    for (;;);
  }
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  drawWaiting();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf[len] = '\0';
      parseLine();
      len = 0;
    } else if (c != '\r' && len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }

  // Go back to the waiting screen if the PC script stops
  if (lastData && millis() - lastData > 5000) {
    lastData = 0;
    drawWaiting();
  }
}
