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
