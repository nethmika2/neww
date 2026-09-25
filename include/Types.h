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
  STATE_POMODORO,
  STATE_TEXT_KBD
};

// What happens when the screen has been idle for a while.  Every one of these
// keeps the board drawing power, because a power bank that sees no load cuts
// the device off: CLOCK leaves the panel on with the drifting clock, DIM keeps
// the backlight at a low duty, DARK turns the backlight off and hands the job to
// the on-board RGB LED at full brightness.
enum IdleMode {
  IDLE_CLOCK = 0,
  IDLE_DIM,
  IDLE_DARK
};

enum PomoMode {
  MODE_WORK,
  MODE_SHORT_BREAK,
  MODE_LONG_BREAK
};

// Sub screens of the Pomodoro app.  All of them share STATE_POMODORO so the
// screensaver/wake-up plumbing only has to know about one app state.
enum PomoView {
  POMO_VIEW_TIMER,
  POMO_VIEW_TASKS,
  POMO_VIEW_PRESETS,
  POMO_VIEW_STATS
};

enum StatsTab {
  STATS_DAY,
  STATS_WEEK,
  STATS_MONTH
};

// Where the generic text keyboard hands its result back to.
enum TextTarget {
  TEXT_TARGET_NONE,
  TEXT_TARGET_TASK,
  TEXT_TARGET_TEMPLATE
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
  // Plain numbers are stored in x/y.  A point may also be typed with the same
  // parameters the grapher sliders expose (e.g. "(2m, c+1)"), in which case the
  // compiled expressions are re-evaluated on every redraw so the dot follows
  // the sliders.  exprX/exprY hold what the user typed for the storage layer.
  double x = 0, y = 0;
  String exprX = "", exprY = "";
  te_expr* compX = nullptr;
  te_expr* compY = nullptr;
  bool live = false;
};

struct PomoTask {
  String text = "";
  uint8_t done = 0;    // checked off by the user
  uint8_t blocks = 0;  // focus blocks credited to this task
  uint8_t target = 1;  // planned focus blocks
  bool in_use = false;
};

struct PomoTemplate {
  String name = "";
  uint16_t work = 25;       // minutes
  uint16_t shortBreak = 5;  // minutes
  uint16_t longBreak = 15;  // minutes
  uint8_t cycles = 4;       // work blocks before a long break
  bool in_use = false;
};

// One row of the focus history.  Only aggregates are kept: minutes of focus
// and the number of finished work blocks for that day.
struct PomoDayStat {
  uint32_t day = 0;      // local-midnight day number
  uint16_t minutes = 0;  // focused minutes
  uint8_t blocks = 0;    // completed work blocks
  uint8_t synced = 0;    // 1 when the clock was valid, 0 for uptime days
  bool in_use = false;
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
