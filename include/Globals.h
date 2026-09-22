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
extern TS_Point calTL, calTR, calBR;

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

// ==========================================
// POWER, SCREENSAVER & CLOCK STATE
// ==========================================
extern bool screenOn, screensaverEnabled, screensaverActive, timeSynced, autoSyncBoot;
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

// ==========================================
// KEYBOARD MATRIX LAYOUTS
// ==========================================
extern const char* main_keys[5][6];
extern const char* func_keys[5][6];
extern const char* var_keys[5][6];
extern const char* point_keys[5][6];
