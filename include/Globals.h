#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Preferences.h>
#include <SD.h>
#include "BluetoothA2DPSource.h"
#include "Config.h"
#include "Types.h"
#include "TouchDriver.h"

// ==========================================
// HARDWARE INSTANCES
// ==========================================
extern SPIClass displaySPI;
extern Adafruit_ILI9341 tft;
extern Preferences prefs;
extern SoftTouch ts;
extern BluetoothA2DPSource a2dp_source;

// ==========================================
// SYSTEM & CALIBRATION STATE
// ==========================================
extern AppState currentState;
extern bool touch_swap_xy;
extern int touch_x_min, touch_x_max, touch_y_min, touch_y_max, calibStep;
// Raw touch samples of the four corner targets, in the order TL, TR, BR, BL.
extern TS_Point calTL, calTR, calBR, calBL;
// Affine (4 point) calibration: screen = c[0]*raw_x + c[1]*raw_y + c[2] per
// axis, which also absorbs rotation, shear and a swapped axis pair.  Used when
// touchCalibrated is true, with the min/max values above as the fallback.
extern float tcalX[3], tcalY[3];
extern bool touchCalibrated;
extern int calibFailCount;

// ==========================================
// AUDIO SYSTEM STATE
// ==========================================
extern File audioFile;
extern WavInfo currentWav;
extern volatile bool isPlaying;
extern bool btConnected, lastBtState, btInitialized, audioSystemReady, sdReady;
extern String playlist[MAX_TRACKS];
extern int numTracks, currentTrack, currentVolume, listPage;
extern volatile int audioGainQ8;
extern volatile bool trackFinished, fileReadDone;
extern volatile uint32_t audioStreamPos;
extern unsigned long lastUiUpdateTime, lastMusicBtnPress;

extern uint8_t* audioRingBuffer;
extern volatile int ringHead, ringTail;
extern SemaphoreHandle_t audioMutex;
extern TaskHandle_t audioTaskHandle;

// ==========================================
// POMODORO TIMER STATE
// ==========================================
extern PomoMode pomoMode;
extern bool pomoRunning;
extern int pomodorosCompleted, pomoSeconds;
extern unsigned long lastPomoTick;
// Runtime routine: these start from the Config.h defaults and are replaced by
// the user's saved timings/preset.
extern int pomoWorkTime, pomoShortTime, pomoLongTime, pomoLongEvery;
extern int pomoDailyGoal;
// Wall-clock deadline of the running phase and the auto start preference.
// Counting down from a deadline (instead of subtracting one second per tick)
// keeps the timer honest even if the loop stalls for a while.
extern unsigned long pomoDeadlineMs;
extern bool pomoAutoStart;
// Scroll offset and maximum for the Pomodoro pages that can overflow.
extern int pomoScrollY, pomoScrollMax;
// Length of the phase currently on screen.  Kept separate from the configured
// timings so editing a duration mid-session cannot corrupt the progress ring.
extern int pomoPhaseTotal;
extern PomoView pomoView;
extern StatsTab statsTab;
extern int pomoStatsPage;
extern int pomoActiveTask;
extern PomoTask pomoTasks[MAX_POMO_TASKS];
extern PomoTemplate pomoTemplates[MAX_POMO_TEMPLATES];
extern PomoDayStat pomoHistory[POMO_HISTORY_DAYS];
extern int pomoHistoryCount;
extern uint8_t pomoHourly[24];
extern uint32_t pomoHourlyDay;
extern unsigned long lastPomoDayCheck;

// ==========================================
// GENERIC TEXT KEYBOARD (task & preset names)
// ==========================================
extern String textInputBuf, textInputTitle;
extern int textInputCursor, textInputMax;
extern TextTarget textInputTarget;
extern bool textKbNumeric;

// ==========================================
// POWER, SCREENSAVER & CLOCK STATE
// ==========================================
extern bool screenOn, screensaverActive, timeSynced, autoSyncBoot;
extern IdleMode idleMode;
// On-board RGB LED: what the idle light looks like, and whether the patterns
// also follow the apps (focus, breaks, music) while the screen is on.
extern LedColor ledColor;
extern LedEffect ledEffect;
extern LedLevel ledLevel;
extern LedShow ledShow;
extern bool ledInvert;      // some boards wire the RGB LED the other way round
extern unsigned long lastActivityTime, lastSaverTick;
extern int clockX, clockY, lastDrawnMinute, homeLastMinute;

// ==========================================
// MATH ENGINE & GRAPHER STATE
// ==========================================
extern double math_x, math_y, math_t;
extern CustomVar sliders[NUM_CUSTOM_VARS];
extern te_variable vars[];
extern const int NUM_TE_VARS;

extern FuncSlot funcs[NUM_FUNCS];
extern PlotPoint points[MAX_POINTS];
extern int numPoints;
extern String pointInput;
extern int pointCursor, activeSlot, cursor_idx;
// Point entry: the second page holds the parameter letters, and the last drawn
// marker positions let a moving (slider driven) point be erased cleanly.
extern bool pointKbAlpha;
extern int old_ptsx[MAX_POINTS], old_ptsy[MAX_POINTS];
extern double centerWorldX, centerWorldY, zoom;
extern int16_t prev_y[NUM_FUNCS][321];
extern int old_ax, old_ay, old_tsx, old_tsy, old_boxW;
extern bool tabsVisible, xAxisPi, yAxisPi, needsFullWipe;
extern bool traceActive, touchActive, isPanning, isTracing;
extern int traceSlot;
extern double traceWorldX, traceWorldY;
extern int touchStartX, touchStartY, lastTouchX, lastTouchY;
extern float smoothed_x, smoothed_y;
extern unsigned long lastPanTime;
extern bool varPanelOpen;
extern int activeVarIdx;
// Slider playback (Desmos-style variable animation)
extern bool varAnimating;
extern int animDirection;
extern unsigned long lastAnimTime;
extern int animSpeedIdx;

// ==========================================
// KEYBOARD MATRIX LAYOUTS
// ==========================================
extern const char* main_keys[5][6];
extern const char* func_keys[5][6];
extern const char* var_keys[5][6];
extern const char* point_keys[5][6];
extern const char* point_alpha_keys[5][6];
extern const char* text_alpha_keys[5][6];
extern const char* text_num_keys[5][6];
