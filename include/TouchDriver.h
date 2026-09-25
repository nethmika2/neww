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

// Reads the panel and returns the calibrated screen position, exactly as the
// main loop computes it.  Any control that samples the touch itself (a
// press-and-hold button, say) must use this: re-deriving the position from the
// raw sample silently ignores the 4 point calibration, so its hit test fails on
// a calibrated panel.
bool readCalibratedTouch(int& sx, int& sy);

// Maps a raw touch sample to screen pixels.  Returns true when the 4 point
// calibration is in use (otherwise the stored min/max axis mapping is used).
bool applyTouchCalibration(const TS_Point& raw, int& sx, int& sy);
