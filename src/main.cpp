#include <Arduino.h>
#include "Config.h"
#include "Types.h"
#include "Globals.h"
#include "TouchDriver.h"
#include "DisplayUtils.h"
#include "Led.h"
#include "Storage.h"
#include "TimeService.h"
#include "AudioApp.h"
#include "MathEngine.h"
#include "GraphApp.h"
#include "KeyboardApp.h"
#include "PomodoroApp.h"
#include "PomodoroStore.h"
#include "TextInput.h"
#include "EarbudControls.h"
#include "SettingsApp.h"
#include "CalibrationApp.h"
#include "HomeApp.h"

// ==========================================
// CORE SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(50);
  // First line of every boot: how much RAM the firmware left itself.  A number
  // far below ~200 KB is what slows the Bluetooth start down later.
  Serial.printf("[I][boot] free heap %u\n", (unsigned)ESP.getFreeHeap());
  // Backlight and the on-board RGB LED share the PWM setup: the backlight is
  // dimmable for the DIM idle mode, and the LED is the load that keeps a power
  // bank awake when the screen is dark.
  pwmBegin(TFT_BL, TFT_BL_CH);
  setBacklight(TFT_BL_FULL_DUTY);
  statusLedBegin();
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
  // A 4 point calibration (when one has been stored) takes over from the
  // min/max axis mapping, which stays as the fallback for older devices.
  loadTouchCalibration();
  {
    int c = prefs.getInt("ledcolor", LED_C_VIOLET);
    int e = prefs.getInt("ledeffect", LED_E_FADE);
    int l = prefs.getInt("ledlevel", LED_L_MED);
    ledColor = (LedColor)constrain(c, 0, LED_C_COUNT - 1);
    ledEffect = (LedEffect)constrain(e, 0, LED_E_COUNT - 1);
    ledLevel = (LedLevel)constrain(l, 0, LED_L_COUNT - 1);
    ledFollowApps = prefs.getBool("ledfollow", true);
  }

  // Older builds stored a plain on/off for the idle screensaver; it maps onto
  // the three idle modes, so an existing setting is not lost.
  if (prefs.isKey("idlemode")) {
    int m = prefs.getInt("idlemode", IDLE_CLOCK);
    idleMode = (m == IDLE_DIM) ? IDLE_DIM : (m == IDLE_DARK) ? IDLE_DARK : IDLE_CLOCK;
  } else {
    idleMode = prefs.getBool("screensaver", true) ? IDLE_CLOCK : IDLE_DARK;
  }
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
  for (int i = 0; i < MAX_POINTS; i++) old_ptsx[i] = old_ptsy[i] = -1000;
  loadFunctions();
  loadPomoStore();
  pomoApplyMode(MODE_WORK);
  pomoLoadTimerState();  // resume a session that a reboot interrupted (paused)
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
  // Earbud buttons are queued from the Bluetooth task and applied here.
  earbudControlsPoll();

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

  pomoTick();

  updateScreensaver();

  updateIdleScreen();
  updateStatusLed();

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

  int sx = -1, sy = -1;
  if (touched) {
    int raw_sx = 0, raw_sy = 0;
    applyTouchCalibration(p, raw_sx, raw_sy);

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

  // Calibration runs after the mapping so it can still see where the panel
  // thinks the touch landed (used for its cancel button).
  if (currentState == STATE_CALIBRATE) {
    handleCalibrationTouch(touched, p, sx, sy);
    delay(2);
    return;
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
  else if (currentState == STATE_TEXT_KBD) handleTextKeyboardTouch(touched, sx, sy);
  else handleKeyboardTouch(touched, sx, sy);

  delay(2);
}