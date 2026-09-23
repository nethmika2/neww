#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Types.h"

class SoftTouch {
public:
  void begin();
  bool touched();
  TS_Point getPoint();

private:
  TS_Point last;
  bool sane(const TS_Point& p);
  int readZ();
  TS_Point rawPoint();
  uint16_t readCmd(uint8_t cmd);
};

void waitTouchRelease();

// Maps a raw touch sample to screen pixels.  Returns true when the 4 point
// calibration is in use (otherwise the stored min/max axis mapping is used).
bool applyTouchCalibration(const TS_Point& raw, int& sx, int& sy);
