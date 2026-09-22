#include "TouchDriver.h"
#include "Globals.h"

void SoftTouch::begin() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_CLK, OUTPUT);
  digitalWrite(TOUCH_CLK, LOW);
  pinMode(TOUCH_MOSI, OUTPUT);
  digitalWrite(TOUCH_MOSI, LOW);
  pinMode(TOUCH_MISO, INPUT);
  pinMode(TOUCH_IRQ, INPUT_PULLUP);
}

bool SoftTouch::sane(const TS_Point& p) {
  return p.x > 100 && p.x < 4000 && p.y > 100 && p.y < 4000;
}

int SoftTouch::readZ() {
  return readCmd(0xB0) + 4095 - readCmd(0xC0);
}

TS_Point SoftTouch::rawPoint() {
  return TS_Point((readCmd(0xD0) + readCmd(0xD0)) / 2, (readCmd(0x90) + readCmd(0x90)) / 2, 1000);
}

uint16_t SoftTouch::readCmd(uint8_t cmd) {
  digitalWrite(TOUCH_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(TOUCH_CLK, LOW);
    digitalWrite(TOUCH_MOSI, (cmd & (1 << i)) ? HIGH : LOW);
    delayMicroseconds(1);
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(1);
  }
  uint16_t val = 0;
  digitalWrite(TOUCH_CLK, LOW);
  delayMicroseconds(1);
  digitalWrite(TOUCH_CLK, HIGH);
  delayMicroseconds(1);
  for (int i = 0; i < 12; i++) {
    digitalWrite(TOUCH_CLK, LOW);
    delayMicroseconds(1);
    val <<= 1;
    if (digitalRead(TOUCH_MISO)) val |= 1;
    digitalWrite(TOUCH_CLK, HIGH);
    delayMicroseconds(1);
  }
  digitalWrite(TOUCH_CLK, LOW);
  digitalWrite(TOUCH_CS, HIGH);
  return val;
}

bool SoftTouch::touched() {
  int z = readZ();
  if (z < TOUCH_Z_MIN) return false;

  // Take 3 samples to completely eliminate noise spikes
  TS_Point p1 = rawPoint();
  delayMicroseconds(200);
  TS_Point p2 = rawPoint();
  delayMicroseconds(200);
  TS_Point p3 = rawPoint();

  int d12 = abs(p1.x - p2.x) + abs(p1.y - p2.y);
  int d23 = abs(p2.x - p3.x) + abs(p2.y - p3.y);
  int d13 = abs(p1.x - p3.x) + abs(p1.y - p3.y);

  // If any 2 samples agree, it's a real touch!
  if (d12 < 150) {
    last = TS_Point((p1.x + p2.x) / 2, (p1.y + p2.y) / 2, z);
    return true;
  }
  if (d23 < 150) {
    last = TS_Point((p2.x + p3.x) / 2, (p2.y + p3.y) / 2, z);
    return true;
  }
  if (d13 < 150) {
    last = TS_Point((p1.x + p3.x) / 2, (p1.y + p3.y) / 2, z);
    return true;
  }

  return false;
}

TS_Point SoftTouch::getPoint() {
  return last;
}

void waitTouchRelease() {
  unsigned long lastTouch = millis();
  while (millis() - lastTouch < 40) {
    if (ts.touched()) lastTouch = millis();
    delay(2);
  }
}
