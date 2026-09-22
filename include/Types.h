#pragma once
#include <Arduino.h>
#include "tinyexpr.h"

enum AppState {
  STATE_HOME,
  STATE_GRAPH,
  STATE_MAIN_KBD,
  STATE_FUNC_KBD,
  STATE_VAR_KBD,
  STATE_POINT_KBD,
  STATE_SETTINGS,
  STATE_MUSIC,
  STATE_MUSIC_LIST,
  STATE_CALIBRATE,
  STATE_POMODORO
};

enum PomoMode {
  MODE_WORK,
  MODE_SHORT_BREAK,
  MODE_LONG_BREAK
};

enum EqType {
  EQ_EXPLICIT,
  EQ_IMPLICIT,
  EQ_PARAMETRIC,
  EQ_POINT,
  EQ_EMPTY
};

struct FuncSlot {
  String input;
  EqType type;
  te_expr* exprX;
  te_expr* exprY;
  bool visible;
  uint16_t color;
};

struct CustomVar {
  String name;
  double value;
  double min_val;
  double max_val;
  bool in_use;
};

struct PlotPoint {
  double x, y;
};

struct TS_Point {
  int16_t x, y, z;
  TS_Point(int16_t _x = 0, int16_t _y = 0, int16_t _z = 0)
    : x(_x), y(_y), z(_z) {}
};

struct WavInfo {
  uint32_t dataStart = 0, dataSize = 0, sampleRate = 0;
  uint16_t numChannels = 0, bitsPerSample = 0;
  bool valid = false;
};
