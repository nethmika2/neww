#include "TouchDriver.h"
#include <math.h>
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

// Waits for the finger to lift so one tap cannot trigger two actions.  The
// loop is bounded: if the panel keeps reporting a touch (a stuck reading, a
// ghost touch while charging) the UI must carry on instead of freezing.
void waitTouchRelease() {
  unsigned long lastTouch = millis();
  for (int guard = 0; guard < 600; guard++) {
    if (millis() - lastTouch >= 40) return;
    if (ts.touched()) lastTouch = millis();
    delay(2);
  }
}

// ==========================================
// TOUCH MAPPING
// ==========================================
// The 4 point calibration stores an affine transform per axis, so the touch
// panel does not have to be perfectly aligned with the display: rotation,
// shear and a swapped axis pair are all absorbed by the coefficients.  Devices
// calibrated by an older build fall back to the min/max mapping below.
bool applyTouchCalibration(const TS_Point& raw, int& sx, int& sy) {
  if (touchCalibrated) {
    sx = constrain((int)lroundf(tcalX[0] * raw.x + tcalX[1] * raw.y + tcalX[2]), 0, 320);
    sy = constrain((int)lroundf(tcalY[0] * raw.x + tcalY[1] * raw.y + tcalY[2]), 0, 240);
    return true;
  }
  int hw_x = touch_swap_xy ? raw.y : raw.x, hw_y = touch_swap_xy ? raw.x : raw.y;
  sx = constrain(map(hw_x, touch_x_min, touch_x_max, 0, 320), 0, 320);
  sy = constrain(map(hw_y, touch_y_min, touch_y_max, 0, 240), 0, 240);
  return false;
}
