#include <Arduino.h>
#include "Config.h"
#include "Types.h"
#include "Globals.h"
#include "TouchDriver.h"
#include "DisplayUtils.h"
#include "Storage.h"
#include "TimeService.h"
#include "AudioApp.h"
#include "MathEngine.h"
#include "GraphApp.h"
#include "KeyboardApp.h"
#include "PomodoroApp.h"
#include "SettingsApp.h"
#include "CalibrationApp.h"
#include "HomeApp.h"

// ==========================================
// CORE SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  displaySPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(1);
  ts.begin();

  prefs.begin("cyd-os", false);
  touch_swap_xy = prefs.getBool("swap_xy", true);
  touch_x_min = prefs.getInt("x_min", 200);
  touch_x_max = prefs.getInt("x_max", 3800);
  touch_y_min = prefs.getInt("y_min", 200);
  touch_y_max = prefs.getInt("y_max", 3800);
  screensaverEnabled = prefs.getBool("screensaver", true);
  autoSyncBoot = prefs.getBool("autosync", true);
  xAxisPi = prefs.getBool("xpi", false);
  yAxisPi = prefs.getBool("ypi", false);
  currentVolume = prefs.getInt("volume", 80);
  applyVolume();

  SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS, SPI, SD_SPI_HZ);
  if (!sdReady) sdReady = SD.begin(SD_CS, SPI, 4000000);
  audioMutex = xSemaphoreCreateMutex();

  for (int i = 0; i < NUM_FUNCS; i++)
    for (int j = 0; j <= 320; j++) prev_y[i][j] = -1000;
  loadFunctions();
  loadPoints();
  loadVariables();
  for (int i = 0; i < NUM_FUNCS; i++) compileSlot(i);
  refreshActiveVariables();
  cursor_idx = funcs[0].input.length();

  lastActivityTime = millis();
  if (ts.touched()) {
    currentState = STATE_CALIBRATE;
    calibStep = 0;
    drawCalibrationScreen();
  } else {
    int h, m;
    if (getClock(h, m)) timeSynced = true;
    else if (autoSyncBoot) syncTimeNTP(true);
    drawHomeScreen();
  }
}

// ==========================================
// MAIN LOOP & EVENT DISPATCH
// ==========================================
void loop() {
  if (btInitialized) btConnected = a2dp_source.is_connected();

  if (trackFinished) {
    trackFinished = false;
    if (numTracks > 0) {
      currentTrack = (currentTrack + 1) % numTracks;
      playTrack(currentTrack);
      if (displayActive()) {
        if (currentState == STATE_MUSIC) drawMusicScreen(true);
        else if (currentState == STATE_MUSIC_LIST) drawMusicList();
      }
    }
  }

  if (currentState == STATE_HOME && displayActive()) {
    int h, m;
    if (getClock(h, m) && m != homeLastMinute) drawHomeClock();
  }

  if (currentState == STATE_MUSIC && displayActive() && millis() - lastUiUpdateTime > 1000) {
    lastUiUpdateTime = millis();
    drawMusicScreen(false);
  }

  if (pomoRunning && millis() - lastPomoTick >= 1000) {
    lastPomoTick += 1000;
    if (pomoSeconds > 0) pomoSeconds--;
    else {
      pomoRunning = false;
      setScreenPower(true);
      if (pomoMode == MODE_WORK) {
        pomodorosCompleted++;
        if (pomodorosCompleted >= POMOS_BEFORE_LONG) {
          pomoMode = MODE_LONG_BREAK;
          pomoSeconds = LONG_BREAK_TIME;
          pomodorosCompleted = 0;
        } else {
          pomoMode = MODE_SHORT_BREAK;
          pomoSeconds = SHORT_BREAK_TIME;
        }
      } else {
        pomoMode = MODE_WORK;
        pomoSeconds = WORK_TIME;
      }
      if (currentState == STATE_POMODORO) drawPomodoroScreen(true);
    }
    if (currentState == STATE_POMODORO && displayActive()) drawPomodoroScreen(false);
  }

  if (screensaverActive && millis() - lastSaverTick > 1000) {
    lastSaverTick = millis();
    updateScreensaver();
  }
  if (currentState != STATE_CALIBRATE) {
    unsigned long idle = millis() - lastActivityTime;
    if (displayActive() && idle > SCREEN_TIMEOUT_MS) setScreenPower(false);
    else if (screensaverActive && !pomoRunning && idle > SCREEN_TIMEOUT_MS + SAVER_OFF_MS) {
      digitalWrite(TFT_BL, LOW);
      screenOn = false;
      screensaverActive = false;
    }
  }

  bool touched = ts.touched();
  TS_Point p;
  if (touched) {
    p = ts.getPoint();
    lastActivityTime = millis();
    if (!displayActive()) {
      setScreenPower(true);
      while (ts.touched()) delay(10);
      return;
    }
  }

  if (currentState == STATE_CALIBRATE) {
    handleCalibrationTouch(touched, p);
    delay(2);
    return;
  }

  int sx = -1, sy = -1;
  if (touched) {
    int hw_x = touch_swap_xy ? p.y : p.x, hw_y = touch_swap_xy ? p.x : p.y;
    int raw_sx = map(hw_x, touch_x_min, touch_x_max, 0, 320);
    int raw_sy = map(hw_y, touch_y_min, touch_y_max, 0, 240);

    // Snap instantly for taps or fast movements, smooth only for slow panning
    if (smoothed_x == -1 || abs(raw_sx - smoothed_x) > 25 || abs(raw_sy - smoothed_y) > 25) {
      smoothed_x = raw_sx;
      smoothed_y = raw_sy;
    } else {
      smoothed_x = (smoothed_x * 0.3) + (raw_sx * 0.7);
      smoothed_y = (smoothed_y * 0.3) + (raw_sy * 0.7);
    }
    sx = constrain((int)smoothed_x, 0, 320);
    sy = constrain((int)smoothed_y, 0, 240);
  } else {
    smoothed_x = -1;
  }

  // Slider playback runs ahead of touch dispatch so a finger-down frame
  // never fights the animation for the screen.
  if (currentState == STATE_GRAPH && varPanelOpen && varAnimating && !touched && displayActive()) {
    tickVarAnimation();
  }

  if (currentState == STATE_HOME) handleHomeTouch(touched, sx, sy);
  else if (currentState == STATE_GRAPH) handleGraphTouch(touched, sx, sy);
  else if (currentState == STATE_SETTINGS) handleSettingsTouch(touched, sx, sy);
  else if (currentState == STATE_MUSIC) handleMusicTouch(touched, sx, sy);
  else if (currentState == STATE_MUSIC_LIST) handleMusicListTouch(touched, sx, sy);
  else if (currentState == STATE_POMODORO) handlePomodoroTouch(touched, sx, sy);
  else if (currentState == STATE_POINT_KBD) handlePointKeyboardTouch(touched, sx, sy);
  else handleKeyboardTouch(touched, sx, sy);

  delay(2);
}