// ---------------------------------------------------------------------------
// Host side test suite.  Compiles the real firmware sources against the stubs
// and drives the parts that do not need a panel: the data layer, the phase
// flow, the countdown arithmetic, the point keyboard, the touch calibration and
// the earbud (AVRCP) handling.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include "Arduino.h"
#include "Adafruit_GFX.h"
#include "HostDraw.h"
#include "SD.h"
#include "Preferences.h"
#include "Globals.h"
#include "Config.h"
#include "Types.h"
#include "DisplayUtils.h"
#include "Led.h"
#include "PomodoroApp.h"
#include "PomodoroStore.h"
#include "KeyboardApp.h"
#include "HomeApp.h"
#include "GraphApp.h"
#include "SettingsApp.h"
#include "TextInput.h"
#include "TouchDriver.h"
#include "CalibrationApp.h"
#include "EarbudControls.h"
#include "AudioApp.h"
#include "MathEngine.h"
#include "Storage.h"
#include "esp_avrc_api.h"
#include "BluetoothA2DPSource.h"

// harness hooks provided by stubs.cpp
void hostSetMillis(unsigned long v);
void hostSetMillisStep(unsigned long v);
unsigned long hostAdvanceMillis(unsigned long by);
unsigned long hostMillisValue();          // reads the clock without advancing it
esp_err_t hostAvrcRnCapHas(esp_avrc_rn_event_ids_t e);
void hostAvrcReset();
void hostMakeWav(const char *path, int seconds, int freq, uint32_t sampleRate);
void hostMakeFile(const char *path, const char *text);
void hostSetNameBaseOnly(bool v);

static int failures = 0;
static int checks = 0;
static const char *currentSuite = "";

#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      printf("  FAIL [%s] %s:%d  %s\n", currentSuite, __FILE__, __LINE__, #cond); \
    }                                                                     \
  } while (0)

#define CHECK_EQ(a, b)                                                          \
  do {                                                                          \
    checks++;                                                                   \
    auto va = (a);                                                              \
    auto vb = (b);                                                              \
    if (!(va == vb)) {                                                          \
      failures++;                                                               \
      printf("  FAIL [%s] %s:%d  %s == %s  (%ld vs %ld)\n", currentSuite, __FILE__, __LINE__, #a, #b, (long)va, (long)vb); \
    }                                                                           \
  } while (0)

#define SUITE(name)                 \
  do {                              \
    currentSuite = name;            \
    printf("- %s\n", name);         \
  } while (0)

// ===========================================================================
// helpers
// ===========================================================================
static void resetPomodoroState() {
  Preferences::resetAll();
  // Assignments, not memset: the task and template structs own String members.
  for (int i = 0; i < MAX_POMO_TASKS; i++) pomoTasks[i] = PomoTask();
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) pomoTemplates[i] = PomoTemplate();
  for (int i = 0; i < POMO_HISTORY_DAYS; i++) pomoHistory[i] = PomoDayStat();
  pomoHistoryCount = 0;
  pomoActiveTask = -1;
  pomoDailyGoal = DEFAULT_DAILY_GOAL;
  pomoWorkTime = DEFAULT_WORK_TIME;
  pomoShortTime = DEFAULT_SHORT_BREAK;
  pomoLongTime = DEFAULT_LONG_BREAK;
  pomoLongEvery = DEFAULT_CYCLES_BEFORE_LONG;
  pomodorosCompleted = 0;
  pomoAutoStart = false;
  pomoScrollReset();
  pomoView = POMO_VIEW_TIMER;
  statsTab = STATS_DAY;
  statsTab = STATS_DAY;
  currentState = STATE_POMODORO;
  setScreenPower(true);
  hostSetMillisStep(250);
  hostSetMillis(100000);
}

// ===========================================================================
// countdown arithmetic
// ===========================================================================
static void testCountdown() {
  SUITE("countdown");
  resetPomodoroState();
  hostSetMillisStep(0);
  hostSetMillis(500000);

  pomoApplyMode(MODE_WORK);
  CHECK_EQ(pomoSeconds, pomoWorkTime);
  CHECK_EQ(pomoPhaseTotal, pomoWorkTime);
  CHECK(pomoDeadlineMs == 500000UL + (unsigned long)pomoWorkTime * 1000UL);

  // Start the phase; the deadline is anchored to "now".
  pomoRunning = true;
  pomoStartTiming();
  CHECK(pomoDeadlineMs == 500000UL + (unsigned long)pomoWorkTime * 1000UL);

  // Ten seconds later the timer shows ten seconds less.
  hostSetMillis(510000);
  pomoTick();
  CHECK_EQ(pomoSeconds, pomoWorkTime - 10);

  // A long stall (a slow redraw, a blocked SD read) must not lose time: the
  // displayed value jumps to what the clock says instead of one second per pass.
  hostSetMillis(510000 + 30000);
  pomoTick();
  CHECK_EQ(pomoSeconds, pomoWorkTime - 40);

  // Pausing freezes the remaining time, and resuming continues from it.
  pomoPauseTiming();          // the UI pauses first ...
  pomoRunning = false;        // ... and then flips the flag
  int frozen = pomoSeconds;
  hostSetMillis(570000);
  pomoTick();
  CHECK_EQ(pomoSeconds, frozen);
  hostSetMillis(575000);
  pomoRunning = true;
  pomoStartTiming();          // resumes from the frozen value
  CHECK(pomoDeadlineMs == 575000UL + (unsigned long)frozen * 1000UL);
  hostSetMillis(580000);
  pomoTick();
  CHECK_EQ(pomoSeconds, frozen - 5);

  // Sub-second remainders round up: the last second is shown as 1, not 0.
  hostSetMillis(pomoDeadlineMs - 400);
  pomoTick();
  CHECK_EQ(pomoSeconds, 1);

  // Reaching zero completes the phase and credits a full work block.
  hostSetMillis(pomoDeadlineMs + 10);
  int blocksBefore = pomoStatBlocks(pomoTodayDay());
  pomoTick();
  CHECK_EQ(pomoSeconds, pomoPhaseTotal);
  CHECK_EQ(pomoMode, MODE_SHORT_BREAK);
  CHECK_EQ(pomoStatBlocks(pomoTodayDay()), blocksBefore + 1);
  CHECK(!pomoRunning);

  // +5 minutes lands in the deadline while the timer runs.
  hostSetMillisStep(0);
  hostSetMillis(2000000);
  pomoApplyMode(MODE_WORK);
  pomoRunning = true;
  pomoStartTiming();
  unsigned long beforeDeadline = pomoDeadlineMs;
  int beforeTotal = pomoPhaseTotal;
  pomoExtendPhase(300);
  CHECK(pomoDeadlineMs == beforeDeadline + 300000UL);
  CHECK_EQ(pomoPhaseTotal, beforeTotal + 300);
  hostSetMillis(2000000 + 1000);
  pomoTick();
  CHECK_EQ(pomoSeconds, beforeTotal + 300 - 1);
}

// ===========================================================================
// task list: estimate stepper, focus, done, clearing
// ===========================================================================
static void testTaskList() {
  SUITE("task list");
  resetPomodoroState();

  int a = addPomoTask("Write report");
  int b = addPomoTask("Email");
  CHECK(a >= 0);
  CHECK(b >= 0);
  CHECK_EQ(pomoTasks[a].target, 1);

  // The estimate must go up *and* down again (the old stepper only cycled up).
  adjustPomoTaskTarget(a, 1);
  CHECK_EQ(pomoTasks[a].target, 2);
  adjustPomoTaskTarget(a, 1);
  adjustPomoTaskTarget(a, 1);
  CHECK_EQ(pomoTasks[a].target, 4);
  adjustPomoTaskTarget(a, -1);
  CHECK_EQ(pomoTasks[a].target, 3);

  // It never falls below one block.
  for (int i = 0; i < 8; i++) adjustPomoTaskTarget(a, -1);
  CHECK_EQ(pomoTasks[a].target, 1);

  // ... nor above the maximum.
  for (int i = 0; i < 40; i++) adjustPomoTaskTarget(a, 1);
  CHECK_EQ(pomoTasks[a].target, MAX_TASK_BLOCKS);

  // Tapping the on screen steppers must hit the right zones.
  pomoView = POMO_VIEW_TASKS;
  drawPomodoroScreen(true);
  int rowY = 32 - pomoScrollY + 10;  // first row of the list
  adjustPomoTaskTarget(a, -1);
  int before = pomoTasks[a].target;
  handlePomodoroTouch(true, 256, rowY + 12);   // "+" button (246..266)
  CHECK_EQ(pomoTasks[a].target, before + 1);
  handlePomodoroTouch(true, 196, rowY + 12);   // "-" button (186..206)
  CHECK_EQ(pomoTasks[a].target, before);
  handlePomodoroTouch(true, 196, rowY + 12);   // and again
  CHECK_EQ(pomoTasks[a].target, before - 1);
  handlePomodoroTouch(true, 256, rowY + 12);
  CHECK_EQ(pomoTasks[a].target, before);

  // Focusing a row selects it for the next finished block.
  handlePomodoroTouch(true, 60, rowY + 12);
  CHECK_EQ(pomoActiveTask, a);

  // Completing a block credits the focused task.
  int credited = pomoTasks[a].blocks;
  pomoCreditActiveTask();
  CHECK_EQ(pomoTasks[a].blocks, credited + 1);

  // Ticking a row marks it done; clearing removes it.
  handlePomodoroTouch(true, 12, rowY + 12);
  CHECK(pomoTasks[a].done);
  clearDonePomoTasks();
  CHECK(!pomoTasks[1].in_use);           // the list shifted up by one
  CHECK(pomoTasks[0].in_use);
  CHECK_EQ(pomoTasks[0].text, String("Email"));
}

// ===========================================================================
// scrolling on the pages that overflow
// ===========================================================================
static void testScrolling() {
  SUITE("scrolling");
  resetPomodoroState();
  pomoView = POMO_VIEW_TASKS;

  // A short list fits, so no scroll control is offered.
  addPomoTask("One");
  drawPomodoroScreen(true);
  CHECK_EQ(pomoScrollMax, 0);
  CHECK(!pomoHandleScrollTouch(280, 230));

  // Fill the list: the page now continues below the fold.
  for (int i = 0; i < MAX_POMO_TASKS - 1; i++) addPomoTask("Task");
  drawPomodoroScreen(true);
  int contentH = 12 + MAX_POMO_TASKS * 26;  // hint line + one row per task
  CHECK_EQ(pomoScrollMax, contentH - (204 - 32));
  CHECK(pomoScrollMax > 0);
  CHECK_EQ(pomoScrollY, 0);

  // The down button (bottom right) moves the content down, the up button
  // brings it back.
  CHECK(pomoHandleScrollTouch(287, 230));
  CHECK(pomoScrollY > 0);
  int mid = pomoScrollY;
  CHECK(pomoHandleScrollTouch(287, 213));
  CHECK_EQ(pomoScrollY, 0);
  pomoHandleScrollTouch(287, 230);
  pomoHandleScrollTouch(287, 230);
  CHECK(pomoScrollY >= mid);
  CHECK_EQ(pomoScrollY, pomoScrollMax);  // clamped at the end
  CHECK(pomoHandleScrollTouch(287, 213));
  CHECK_EQ(pomoScrollY, 0);
  CHECK(pomoHandleScrollTouch(287, 213));  // up at the top is a no-op

  // The control sits inside the screen and clear of the action bar buttons.
  CHECK(287 >= 262 - 1 && 287 <= 262 + 50);
  CHECK(222 + 16 <= 240);   // the down button fits on the panel
  CHECK(205 >= 204 - 1);    // and stays clear of the action bar

  // Switching pages resets the offset.
  pomoScrollY = 20;
  pomoSwitchView(POMO_VIEW_PRESETS);
  CHECK_EQ(pomoScrollY, 0);

  // The presets page also scrolls when routines are saved.
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    pomoTemplates[i].name = String("Routine ") + String(i);
    pomoTemplates[i].in_use = 1;
  }
  drawPomodoroScreen(true);
  CHECK(pomoScrollMax > 0);
  CHECK(pomoHandleScrollTouch(287, 230));
  CHECK(pomoScrollY > 0);
}

// ===========================================================================
// keyboard layout and key repaint
// ===========================================================================
static void testKeyboard() {
  SUITE("keyboard");
  resetPomodoroState();

  // Letters are laid out the way a normal keyboard is (QWERTY), not A-Z.
  CHECK(!strcmp(text_alpha_keys[0][0], "q"));
  CHECK(!strcmp(text_alpha_keys[0][1], "w"));
  CHECK(!strcmp(text_alpha_keys[0][2], "e"));
  CHECK(!strcmp(text_alpha_keys[1][0], "u"));
  CHECK(!strcmp(text_alpha_keys[4][0], "n"));
  CHECK(!strcmp(text_alpha_keys[4][1], "m"));
  CHECK(!strcmp(text_alpha_keys[4][2], "SP"));
  CHECK(!strcmp(text_alpha_keys[4][5], "DEL"));

  // Every letter of the alphabet appears exactly once.
  std::string letters;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 6; c++) letters += text_alpha_keys[r][c];
  letters += text_alpha_keys[4][0];
  letters += text_alpha_keys[4][1];
  CHECK_EQ((int)letters.size(), 26);
  for (char ch = 'a'; ch <= 'z'; ch++) CHECK(letters.find(ch) != std::string::npos);

  // Typing a letter writes it and the key is repainted afterwards, so the
  // label of the tapped key does not vanish (the bug the user hit: the pressed
  // key was left in the pressed colour until the board was redrawn).
  currentState = STATE_TEXT_KBD;
  textInputBuf = "";
  textInputCursor = 0;
  textKbNumeric = false;
  textInputTarget = TEXT_TARGET_TASK;
  textInputMax = 22;
  startTextInput("NEW TASK", "", TEXT_TARGET_TASK, POMO_NAME_LEN);
  HostDraw::reset();
  handleTextKeyboardTouch(true, 2 + 0 * 53 + 5, 78 + 0 * 32 + 5);  // top left = Q
  CHECK_EQ(textInputBuf, String("q"));
  bool labelRepainted = false;
  for (auto &line : HostDraw::log()) {
    if (line.rfind("text ", 0) == 0 && line.find(" q") != std::string::npos) labelRepainted = true;
  }
  CHECK(labelRepainted);

  // Same for the letters of the point keyboard: the tapped key is repainted.
  currentState = STATE_POINT_KBD;
  pointKbAlpha = true;
  pointInput = "";
  pointCursor = 0;
  HostDraw::reset();
  handlePointKeyboardTouch(true, 2 + 0 * 53 + 5, 40 + 0 * 40 + 5);  // top left = a
  bool pointRepaint = false;
  for (auto &line : HostDraw::log()) {
    if (line.rfind("text ", 0) == 0 && line.find(" a") != std::string::npos) pointRepaint = true;
  }
  CHECK(pointRepaint);
  pointKbAlpha = false;
  pointInput = "";
  pointCursor = 0;
}

// ===========================================================================
// touch calibration
// ===========================================================================
static void testCalibration() {
  SUITE("calibration");
  Preferences::resetAll();
  touchCalibrated = false;
  calBL = TS_Point();

  // A swapped panel: raw x follows the screen y and raw y the screen x.
  auto panelRaw = [](int sx, int sy) {
    TS_Point p;
    p.x = (int)(sy * (3600.0 / 240.0)) + 200;
    p.y = (int)(sx * (3600.0 / 320.0)) + 150;
    p.z = 500;
    return p;
  };
  calTL = panelRaw(24, 24);
  calTR = panelRaw(296, 24);
  calBR = panelRaw(296, 216);
  calBL = panelRaw(24, 216);
  CHECK(fitTouchCalibration());
  CHECK(touch_swap_xy);
  int sx = 0, sy = 0;
  TS_Point raw = panelRaw(24, 24);
  CHECK(applyTouchCalibration(raw, sx, sy));
  CHECK(abs(sx - 24) <= 2 && abs(sy - 24) <= 2);
  raw = panelRaw(296, 216);
  CHECK(applyTouchCalibration(raw, sx, sy));
  CHECK(abs(sx - 296) <= 2 && abs(sy - 216) <= 2);
  raw = panelRaw(160, 120);
  CHECK(applyTouchCalibration(raw, sx, sy));
  CHECK(abs(sx - 160) <= 2 && abs(sy - 120) <= 2);

  // An unswapped panel is detected as such and still fits.
  auto panelRaw2 = [](int sx, int sy) {
    TS_Point p;
    p.x = (int)(sx * (3600.0 / 320.0)) + 150;
    p.y = (int)(sy * (3600.0 / 240.0)) + 200;
    p.z = 500;
    return p;
  };
  calTL = panelRaw2(24, 24);
  calTR = panelRaw2(296, 24);
  calBR = panelRaw2(296, 216);
  calBL = panelRaw2(24, 216);
  CHECK(fitTouchCalibration());
  CHECK(!touch_swap_xy);
  raw = panelRaw2(296, 24);
  CHECK(applyTouchCalibration(raw, sx, sy));
  CHECK(abs(sx - 296) <= 2 && abs(sy - 24) <= 2);

  // Four taps that are not really in four corners are rejected, so a bad
  // calibration can never be stored.
  calTL = panelRaw2(200, 200);
  calTR = panelRaw2(210, 205);
  calBR = panelRaw2(205, 210);
  calBL = panelRaw2(202, 208);
  CHECK(!fitTouchCalibration());

  // Saved calibration survives a reload.
  calTL = panelRaw2(24, 24);
  calTR = panelRaw2(296, 24);
  calBR = panelRaw2(296, 216);
  calBL = panelRaw2(24, 216);
  CHECK(fitTouchCalibration());
  saveTouchCalibration();
  touchCalibrated = false;
  tcalX[0] = tcalX[1] = tcalX[2] = 0;
  loadTouchCalibration();
  CHECK(touchCalibrated);
  raw = panelRaw2(160, 120);
  CHECK(applyTouchCalibration(raw, sx, sy));
  CHECK(abs(sx - 160) <= 3 && abs(sy - 120) <= 3);

  // A truncated record falls back to the legacy mapping instead of crashing.
  Preferences::resetAll();
  touchCalibrated = false;
  loadTouchCalibration();
  CHECK(!touchCalibrated);
}

// ===========================================================================
// earbud / AVRCP control
// ===========================================================================
static void testEarbuds() {
  SUITE("earbuds");
  resetPomodoroState();
  hostAvrcReset();
  btInitialized = true;
  a2dp_source.set_connected(false);
  pomoApplyMode(MODE_WORK);
  String n0 = earbudLastEvent();
  (void)n0;

  SD.reset();
  hostMakeWav("/track1.wav", 1);
  hostMakeWav("/track2.wav", 1);
  sdReady = true;
  btInitialized = true;
  numTracks = 2;
  playlist[0] = "track1.wav";
  playlist[1] = "track2.wav";
  currentTrack = 0;
  currentVolume = 40;
  isPlaying = false;
  a2dp_source.start("cyd-os");
  earbudControlsSetEnabled(true);

  // The passthrough handler and the notification capabilities are installed
  // before the stack starts.
  earbudControlsPrepare();
  CHECK(a2dp_source.passthruCallback() != nullptr);
  CHECK(a2dp_source.passthruCallback() == earbudPassthruHandler);
  bool hasVolume = false, hasPlay = false;
  for (auto e : a2dp_source.rnEventsRef()) {
    if (e == ESP_AVRC_RN_VOLUME_CHANGE) hasVolume = true;
    if (e == ESP_AVRC_RN_PLAY_STATUS_CHANGE) hasPlay = true;
  }
  CHECK(hasVolume);
  CHECK(hasPlay);

  // No device yet: nothing to attach, and presses are simply queued.
  earbudControlsPoll();
  CHECK(hostAvrc.registered == nullptr);

  // Once a device is connected the target callback is installed, and the
  // library's own handler still receives every event.
  a2dp_source.set_connected(true);
  earbudControlsPoll();
  CHECK(hostAvrc.registered != nullptr);

  // A controller that registers for the volume notification must get the
  // interim response the AVRCP spec requires, otherwise the buds stop sending
  // their buttons.
  esp_avrc_tg_cb_param_t p;
  memset(&p, 0, sizeof(p));
  p.reg_ntf.event_id = ESP_AVRC_RN_VOLUME_CHANGE;
  hostAvrc.deliver(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT, &p);
  CHECK_EQ(hostAvrc.rnResponseCount(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM), 1);
  CHECK_EQ(a2dp_source.lastTgEvent, (int)ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT);

  memset(&p, 0, sizeof(p));
  p.reg_ntf.event_id = ESP_AVRC_RN_PLAY_STATUS_CHANGE;
  hostAvrc.deliver(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT, &p);
  CHECK_EQ(hostAvrc.rnResponseCount(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_INTERIM), 1);

  // A play/pause tap is acted on once, releases are ignored.
  earbudPassthruHandler(ESP_AVRC_PT_CMD_PLAY, false);
  earbudPassthruHandler(ESP_AVRC_PT_CMD_PLAY, true);
  earbudControlsPoll();
  CHECK(isPlaying);
  CHECK_EQ(earbudLastEvent(), String("PLAY"));
  CHECK(earbudEventCount() > 0);

  // The buds are told about the state change they asked for.
  CHECK_EQ(hostAvrc.rnResponseCount(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_CHANGED), 1);

  // Next / previous move through the playlist.
  earbudPassthruHandler(ESP_AVRC_PT_CMD_FORWARD, false);
  earbudControlsPoll();
  CHECK_EQ(currentTrack, 1);
  earbudPassthruHandler(ESP_AVRC_PT_CMD_BACKWARD, false);
  earbudControlsPoll();
  CHECK_EQ(currentTrack, 0);
  CHECK_EQ(earbudLastEvent(), String("PREV"));

  // Volume keys step the local volume.
  currentVolume = 40;
  earbudPassthruHandler(ESP_AVRC_PT_CMD_VOL_UP, false);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 45);
  earbudPassthruHandler(ESP_AVRC_PT_CMD_VOL_DOWN, false);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 40);
  for (int i = 0; i < 20; i++) {
    earbudPassthruHandler(ESP_AVRC_PT_CMD_VOL_UP, false);
    earbudControlsPoll();
  }
  CHECK_EQ(currentVolume, 100);  // clamped

  // Absolute volume: most true wireless buds (including the Soundcore R50i NC)
  // send volume taps this way.  The library only logged this event, so the
  // volume never moved before.
  memset(&p, 0, sizeof(p));
  p.set_abs_vol.volume = 0x40;  // 64 of 127
  hostAvrc.deliver(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &p);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 50);
  CHECK_EQ(earbudLastEvent(), String("ABS VOL"));

  memset(&p, 0, sizeof(p));
  p.set_abs_vol.volume = 0x7F;
  hostAvrc.deliver(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &p);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 100);

  memset(&p, 0, sizeof(p));
  p.set_abs_vol.volume = 0;
  hostAvrc.deliver(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &p);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 0);

  // A lone release (no matching press) does nothing.
  int beforeVolume = currentVolume;
  earbudPassthruHandler(ESP_AVRC_PT_CMD_VOL_DOWN, true);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, beforeVolume);

  // Switching the feature off ignores the buds entirely.
  earbudControlsSetEnabled(false);
  currentVolume = 30;
  earbudPassthruHandler(ESP_AVRC_PT_CMD_VOL_UP, false);
  hostAvrc.deliver(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT, &p);
  earbudControlsPoll();
  CHECK_EQ(currentVolume, 30);
  earbudControlsSetEnabled(true);

  // Disconnecting forgets the notification registrations.
  memset(&p, 0, sizeof(p));
  p.conn_stat.connected = false;
  hostAvrc.deliver(ESP_AVRC_TG_CONNECTION_STATE_EVT, &p);
  hostAvrc.reset();
  earbudPassthruHandler(ESP_AVRC_PT_CMD_PAUSE, false);
  earbudControlsPoll();
  CHECK_EQ(hostAvrc.rnResponseCount(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_CHANGED), 0);
}

// ===========================================================================
// persistence
// ===========================================================================
// ===========================================================================
// idle screen + the RGB LED
// ===========================================================================
// Two things are being checked here: no idle state leaves the board looking
// switched off to a USB power bank, and the LED's patterns and colours really
// do follow the settings and the running task.
static int peakOver(LedEffect effect, LedColor color, LedLevel level, int period, int step = 25) {
  int peak = 0;
  hostSetMillisStep(0);
  for (int t = 0; t < period; t += step) {
    hostSetMillis(1000 + t);
    ledRequest(color, effect, level);
    ledTick();
    peak = max(peak, ledBrightnessNow());
  }
  return peak;
}

static void testLed() {
  SUITE("idle led");
  pomoRunning = false;
  currentState = STATE_HOME;
  hostSetMillisStep(0);
  // Earlier suites finish pomodoro phases, which fires an announcement; clear it
  // so this suite starts from a known light state.
  ledAlert(0);
  hostSetMillis(1000);

  // DARK: backlight off, and the LED doing the keep-awake duty.
  idleMode = IDLE_DARK;
  ledColor = LED_C_VIOLET;
  ledEffect = LED_E_FADE;
  ledLevel = LED_L_MED;
  screenOn = true;
  screensaverActive = false;
  setScreenPower(false);
  updateStatusLed();
  CHECK(!screenOn);
  CHECK_EQ(backlightLevel(), 0);
  CHECK_EQ(hostPwmDuty(TFT_BL_CH), 0);
  CHECK(ledBrightnessNow() > 0);                    // never looks unplugged
  CHECK(ledBrightnessNow() < 255);                  // and never full blast

  // DIM: the panel is not black, but it is dark, and the LED runs too.
  idleMode = IDLE_DIM;
  screenOn = true;
  setScreenPower(false);
  updateStatusLed();
  CHECK(!screenOn);
  CHECK_EQ(backlightLevel(), TFT_BL_DIM_DUTY);
  CHECK(hostPwmDuty(TFT_BL_CH) < (int)TFT_BL_FULL_DUTY);
  CHECK(ledBrightnessNow() > 0);

  // CLOCK: the screensaver keeps the panel lit, so the LED is not needed.
  idleMode = IDLE_CLOCK;
  screenOn = true;
  setScreenPower(false);
  updateStatusLed();
  CHECK(screensaverActive);
  CHECK_EQ(backlightLevel(), TFT_BL_FULL_DUTY);
  CHECK_EQ(ledBrightnessNow(), 0);

  // ...and after the long saver timeout the screen goes dark and the LED takes
  // over, which is the case that used to switch the board off.
  hostSetMillis(hostMillisValue() + SCREEN_TIMEOUT_MS + SAVER_OFF_MS + 5000);
  lastActivityTime = millis() - (SCREEN_TIMEOUT_MS + SAVER_OFF_MS + 5000);
  updateIdleScreen();
  updateStatusLed();
  CHECK(!screensaverActive);
  CHECK(!screenOn);
  CHECK_EQ(backlightLevel(), 0);
  CHECK(ledBrightnessNow() > 0);

  // Waking restores the panel fully and puts the LED out.
  setScreenPower(true);
  updateStatusLed();
  CHECK(screenOn);
  CHECK_EQ(backlightLevel(), TFT_BL_FULL_DUTY);
  CHECK_EQ(hostPwmDuty(TFT_BL_CH), (int)TFT_BL_FULL_DUTY);
  CHECK_EQ(ledBrightnessNow(), 0);

  // Brightness levels: LOW < MED < HIGH, and the default is not full blast.
  CHECK(ledLevelPeak(LED_L_LOW) < ledLevelPeak(LED_L_MED));
  CHECK(ledLevelPeak(LED_L_MED) < ledLevelPeak(LED_L_HIGH));
  CHECK(ledLevelPeak(LED_L_HIGH) == 255);
  CHECK(ledLevelPeak(LED_L_MED) <= 140);            // "100% is too much"

  // FADE is a linear ramp: it rises to the level's peak and falls back, and the
  // floor keeps it visible (and the power bank loaded) at the bottom.
  int lowPeak = peakOver(LED_E_FADE, LED_C_VIOLET, LED_L_MED, 2600);
  int hiPeak = peakOver(LED_E_FADE, LED_C_VIOLET, LED_L_HIGH, 2600);
  CHECK_EQ(hiPeak, 255);
  CHECK(lowPeak > 0 && lowPeak < hiPeak);

  // The ramp starts at phase 0, so sampling at 0 / 650 / 1300 / 1950 ms of the
  // 2600 ms period lands on the floor, the quarter, the peak and three quarters.
  hostSetMillis(0);
  ledRequest(LED_C_VIOLET, LED_E_FADE, LED_L_MED);
  ledTick();
  int atZero = ledBrightnessNow();
  hostSetMillis(650);
  ledTick();
  int atQuarter = ledBrightnessNow();
  hostSetMillis(1300);
  ledTick();
  int atHalf = ledBrightnessNow();
  hostSetMillis(1950);
  ledTick();
  int atThreeQ = ledBrightnessNow();
  CHECK(atZero > 0);                                // the floor, not black
  CHECK(atQuarter > atZero && atHalf > atQuarter);  // straight ramp up
  CHECK(atThreeQ < atHalf);                         // and straight back down
  CHECK(abs((int)atQuarter - (int)atThreeQ) <= 12); // symmetric ramp
  CHECK(abs((int)atHalf - lowPeak) <= 6);           // it reaches the peak

  // BREATHE is the smooth one, CYCLE walks the colour wheel, PULSE is a
  // sawtooth: all three move over time and none of them goes black.
  for (LedEffect e : { LED_E_BREATHE, LED_E_CYCLE, LED_E_PULSE }) {
    int minV = 255, maxV = 0;
    hostSetMillisStep(0);
    for (int t = 0; t < 3600; t += 50) {
      hostSetMillis(5000 + t);
      ledRequest(LED_C_BLUE, e, LED_L_MED);
      ledTick();
      minV = min(minV, ledBrightnessNow());
      maxV = max(maxV, ledBrightnessNow());
    }
    CHECK(maxV > minV);                             // it animates
    CHECK(minV > 0);                                // and never switches off
  }

  // CYCLE really does change colour, not just brightness.
  uint8_t r0, g0, b0, r1, g1, b1;
  hostSetMillis(0);
  ledRequest(LED_C_BLUE, LED_E_CYCLE, LED_L_MED);
  ledTick();
  ledChannelsNow(&r0, &g0, &b0);
  hostSetMillis(1800);
  ledTick();
  ledChannelsNow(&r1, &g1, &b1);
  CHECK(r0 != r1 || g0 != g1 || b0 != b1);

  // Colours are what they say: green is green, and so on.
  hostSetMillis(0);
  ledRequest(LED_C_GREEN, LED_E_SOLID, LED_L_HIGH);
  ledTick();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(g0 > r0 && g0 > b0);
  ledRequest(LED_C_BLUE, LED_E_SOLID, LED_L_HIGH);
  ledTick();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(b0 > r0 && b0 > g0);
  ledRequest(LED_C_RED, LED_E_SOLID, LED_L_HIGH);
  ledTick();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(r0 > g0 && r0 > b0);

  // Tasks drive the light while the screen is on, when the switch is on:
  // focus fades amber, a short break breathes green, a long break blue, and
  // music walks the colour wheel.
  ledFollowApps = true;
  screenOn = true;
  screensaverActive = false;
  pomoRunning = true;
  pomoMode = MODE_WORK;
  hostSetMillis(0);
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(r0 > 0 && r0 >= g0 && g0 > b0);             // amber
  pomoMode = MODE_SHORT_BREAK;
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(g0 > 0 && g0 > r0 && g0 > b0);              // green
  pomoMode = MODE_LONG_BREAK;
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(b0 > 0 && b0 > r0 && b0 > g0);              // blue
  pomoRunning = false;
  isPlaying = true;
  hostSetMillis(0);
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  hostSetMillis(1800);
  updateStatusLed();
  ledChannelsNow(&r1, &g1, &b1);
  CHECK(r0 != r1 || g0 != g1 || b0 != b1);          // the wheel turns
  isPlaying = false;

  // Switch it off and the LED stays dark while the panel is lit...
  ledFollowApps = false;
  pomoRunning = true;
  pomoMode = MODE_WORK;
  updateStatusLed();
  CHECK_EQ(ledBrightnessNow(), 0);
  pomoRunning = false;
  // ...but the dark screen still gets its keep-awake light, whatever the switch
  // says.
  idleMode = IDLE_DARK;
  setScreenPower(false);
  updateStatusLed();
  CHECK(ledBrightnessNow() > 0);

  // An announcement flashes red over everything else.
  hostSetMillis(20000);
  ledAlert(2000);
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  hostSetMillis(20100);                    // inside a bright half of the flash
  updateStatusLed();
  ledChannelsNow(&r0, &g0, &b0);
  CHECK(r0 > 0 && g0 == 0 && b0 == 0);
  hostSetMillis(20200);                    // ...and the dark half of the flash
  updateStatusLed();
  CHECK_EQ(ledBrightnessNow(), 0);
  CHECK(ledAlertActiveForTest());
  hostSetMillis(23000);                            // the flash is over
  ledRequest(LED_C_BLUE, LED_E_SOLID, LED_L_MED);
  updateStatusLed();
  CHECK(!ledAlertActiveForTest());

  // Settings previews show the pattern even while everything else is idle.
  setScreenPower(true);
  idleMode = IDLE_CLOCK;
  hostSetMillis(30000);
  ledPreview(LED_C_AMBER, LED_E_PULSE, LED_L_HIGH);
  updateStatusLed();
  CHECK(ledBrightnessNow() > 0);
  CHECK(ledPreviewActive());
  hostSetMillis(33000);                            // preview expired
  updateStatusLed();
  CHECK_EQ(ledBrightnessNow(), 0);
  CHECK(!ledPreviewActive());

  // The black level must never be dark enough to be mistaken for "unplugged".
  CHECK(TFT_BL_DIM_DUTY > 0);
  ledLevel = LED_L_MED;
  hostSetMillisStep(250);
}

static void testPersistence() {
  SUITE("persistence");
  resetPomodoroState();

  addPomoTask("Deep work");
  addPomoTask("Inbox");
  adjustPomoTaskTarget(1, 3);
  togglePomoTaskDone(1);
  pomoActiveTask = 1;
  pomoWorkTime = 40 * 60;
  pomoLongEvery = 5;
  pomoDailyGoal = 10;
  pomoAutoStart = true;
  pomoRecordWorkBlock(25 * 60, true);
  savePomoSettings();
  savePomoTasks();

  // Simulate a reboot: clear the RAM copy and load it back.
  for (int i = 0; i < MAX_POMO_TASKS; i++) pomoTasks[i] = PomoTask();
  pomoActiveTask = -1;
  int goalBefore = pomoDailyGoal;
  loadPomoStore();

  CHECK_EQ(pomoDailyGoal, goalBefore);
  CHECK(pomoAutoStart);
  CHECK_EQ(pomoWorkTime, 40 * 60);
  CHECK_EQ(pomoLongEvery, 5);
  CHECK(pomoTasks[0].in_use && pomoTasks[1].in_use);
  CHECK_EQ(pomoTasks[1].target, 4);
  CHECK(pomoTasks[1].done);
  CHECK_EQ(pomoActiveTask, 1);
  CHECK_EQ(pomoStatBlocks(pomoTodayDay()), 1);
  CHECK_EQ(pomoStatMinutes(pomoTodayDay()), 25);

  // Timer state survives a reboot too, and comes back paused.
  pomoApplyMode(MODE_WORK);
  pomoSeconds = 600;
  pomoPhaseTotal = 1500;
  pomoRunning = true;
  pomoSaveTimerState();
  pomoSeconds = 0;
  pomoMode = MODE_LONG_BREAK;
  pomoLoadTimerState();
  CHECK_EQ(pomoSeconds, 600);
  CHECK_EQ(pomoMode, MODE_WORK);
  CHECK_EQ(pomoPhaseTotal, 1500);
  CHECK(!pomoRunning);

  // Dots never over-fill when the cycle length is shortened.
  pomodorosCompleted = 6;
  pomoLongEvery = 4;
  pomoApplyMode(MODE_WORK);
  CHECK(pomodorosCompleted < pomoLongEvery);
}

// ===========================================================================
// rendering smoke tests: every page of every app must draw without straying
// ===========================================================================
static void testRendering() {
  SUITE("rendering");
  resetPomodoroState();

  struct Shot { const char *name; std::function<void()> draw; };
  std::vector<Shot> shots;

  shots.push_back({"01-home", []() { currentState = STATE_HOME; drawHomeScreen(); }});
  shots.push_back({"02-music", []() { currentState = STATE_MUSIC; drawMusicScreen(true); }});
  shots.push_back({"03-graph", []() { currentState = STATE_GRAPH; drawGraphScreen(true); }});
  shots.push_back({"04-keyboard", []() {
                     currentState = STATE_POINT_KBD;
                     pointInput = "2m,3";
                     pointCursor = 4;
                     pointKbAlpha = false;
                     drawPointKeyboardScreen();
                   }});
  shots.push_back({"04b-keyboard-alpha", []() {
                     currentState = STATE_POINT_KBD;
                     pointInput = "a,b";
                     pointCursor = 3;
                     pointKbAlpha = true;
                     drawPointKeyboardScreen();
                   }});
  shots.push_back({"05-textinput", []() {
                     currentState = STATE_TEXT_KBD;
                     startTextInput("NEW TASK", "Deep work", TEXT_TARGET_TASK, POMO_NAME_LEN);
                     drawTextKeyboardScreen(true);
                   }});
  shots.push_back({"06-settings", []() { currentState = STATE_SETTINGS; drawSettingsScreen(); }});
  shots.push_back({"06b-settings-led", []() {
                     currentState = STATE_SETTINGS;
                     settingsPageForTest() = 1;
                     drawSettingsScreen();
                     settingsPageForTest() = 0;
                   }});
  shots.push_back({"07-calibration", []() { currentState = STATE_CALIBRATE; drawCalibrationScreen(); }});

  for (auto &s : shots) {
    HostDraw::reset();
    s.draw();
    CHECK(!HostDraw::log().empty());
    HostDraw::dump((std::string("shots/") + s.name + ".txt").c_str());
  }

  // Pomodoro views with content, including the overflowing task list.
  resetPomodoroState();
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    pomoTasks[i].text = String("Task number ") + String(i + 1);
    pomoTasks[i].in_use = 1;
    pomoTasks[i].target = 1 + (i % 3);
    pomoTasks[i].blocks = i % 2;
    pomoTasks[i].done = (i % 4 == 0);
  }
  pomoTasks[2].blocks = 3;
  pomoActiveTask = 2;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    pomoTemplates[i].name = String("Routine ") + String(i + 1);
    pomoTemplates[i].in_use = 1;
    pomoTemplates[i].work = 25 * 60;
    pomoTemplates[i].shortBreak = 5 * 60;
    pomoTemplates[i].longBreak = 15 * 60;
    pomoTemplates[i].cycles = 4;
  }
  for (int d = 0; d < 30; d++) {
    pomoHistory[pomoHistoryCount].in_use = 1;
    pomoHistory[pomoHistoryCount].day = pomoTodayDay() - (29 - d);
    pomoHistory[pomoHistoryCount].minutes = (d * 7) % 95;
    pomoHistory[pomoHistoryCount].blocks = (d % 4);
    pomoHistoryCount++;
  }
  for (int h = 0; h < 24; h++) pomoHourly[h] = (h % 7) == 0 ? 25 : 0;
  pomoDailyGoal = 8;
  pomoApplyMode(MODE_WORK);
  pomoRunning = true;
  pomoSeconds = pomoPhaseTotal - 300;

  struct PShot { const char *name; PomoView view; StatsTab tab; int scroll; };
  PShot pshots[] = {
      {"08-pomo-timer", POMO_VIEW_TIMER, STATS_DAY, 0},
      {"09-pomo-tasks", POMO_VIEW_TASKS, STATS_DAY, 0},
      {"10-pomo-tasks-scrolled", POMO_VIEW_TASKS, STATS_DAY, 0},
      {"11-pomo-presets", POMO_VIEW_PRESETS, STATS_DAY, 0},
      {"12-pomo-presets-scrolled", POMO_VIEW_PRESETS, STATS_DAY, 0},
      {"13-pomo-day", POMO_VIEW_STATS, STATS_DAY, 0},
      {"14-pomo-week", POMO_VIEW_STATS, STATS_WEEK, 0},
      {"15-pomo-month", POMO_VIEW_STATS, STATS_MONTH, 0},
  };
  for (auto &ps : pshots) {
    pomoView = ps.view;
    statsTab = ps.tab;
    pomoScrollReset();
    HostDraw::reset();
    drawPomodoroScreen(true);
    CHECK(!HostDraw::log().empty());
    // The scrolled variant puts the tail of the page on screen.
    if (ps.scroll || std::string(ps.name).find("scrolled") != std::string::npos) {
      pomoScrollY = pomoScrollMax;
      HostDraw::reset();
      drawPomodoroScreen(true);
    }
    HostDraw::dump((std::string("shots/") + ps.name + ".txt").c_str());
    // Nothing may be drawn outside the 320x240 panel.
    for (auto &line : HostDraw::log()) {
      int x, y, w, h;
      if (sscanf(line.c_str(), "fillRect %d %d %d %d", &x, &y, &w, &h) == 4) {
        // Every rectangle must at least touch the panel and must not be bigger
        // than it: nothing may be drawn purely off screen (that would waste SPI
        // traffic) and no call may be larger than the display.
        bool touches = (x + w > 0) && (y + h > 0) && (x < 320) && (y < 240);
        bool fits = (w <= 320) && (h <= 240) && w > 0 && h > 0;
        if (!touches || !fits) {
          failures++;
          printf("  OOB [%s] -> %d %d %d %d\n", ps.name, x, y, w, h);
        }
        checks++;
      }
    }
  }

  // Home once more at the end, so the shot always reflects the final tile row.
  currentState = STATE_HOME;
  HostDraw::reset();
  drawHomeScreen();
  HostDraw::dump("shots/01-home.txt");
  HostDraw::reset();

  // The name keyboard in both modes: the letter page is QWERTY and the number
  // page holds the digits and punctuation.
  currentState = STATE_TEXT_KBD;
  textKbNumeric = false;
  startTextInput("NEW TASK", "Deep work", TEXT_TARGET_TASK, POMO_NAME_LEN);
  HostDraw::reset();
  drawTextKeyboardScreen(true);
  HostDraw::dump("shots/16-text-keyboard.txt");
  textKbNumeric = true;
  HostDraw::reset();
  drawTextKeyboardScreen(true);
  HostDraw::dump("shots/17-text-keyboard-123.txt");
  textKbNumeric = false;
}

int main() {
  system("mkdir -p shots");
  printf("== host checks ==\n");
  testCountdown();
  testTaskList();
  testScrolling();
  testKeyboard();
  testCalibration();
  testEarbuds();
  testLed();
  testPersistence();
  testRendering();

  printf("\n%d checks, %d failures\n", checks, failures);
  if (failures == 0) printf("ALL CHECKS PASSED (0 failures)\n");
  return failures == 0 ? 0 : 1;
}
