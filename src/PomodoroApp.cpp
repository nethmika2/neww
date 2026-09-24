#include "PomodoroApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "PomodoroStore.h"
#include "TextInput.h"

// Forward declaration from HomeApp
void drawHomeScreen();

// ==========================================
// TIMER SCREEN LAYOUT
// ==========================================
// Everything is placed on an 8 px margin grid: the routine picker and the
// cycle dots share the first row, the ring owns the middle and the task chip
// plus the transport buttons sit in the two bottom rows.
static const int POMO_MODE_X = 8;
static const int POMO_MODE_Y = 38;
static const int POMO_MODE_W = 196;
static const int POMO_MODE_H = 26;
static const int POMO_DOTS_CX = 258;
static const int POMO_DOTS_CY = 51;
static const int POMO_RING_CX = 160;
static const int POMO_RING_CY = 120;
static const int POMO_RING_R = 44;
static const int POMO_RING_THICK = 6;
static const int POMO_CHIP_X = 8;
static const int POMO_CHIP_Y = 170;
static const int POMO_CHIP_W = 304;
static const int POMO_CHIP_H = 20;
static const int POMO_BTN_Y = 194;
static const int POMO_BTN_H = 38;

static const char* const POMO_TABS[POMO_VIEW_COUNT] = { "TIMER", "TASKS", "PRESET", "STATS" };

String pomoModeTitle() {
  if (pomoMode == MODE_SHORT_BREAK) return "SHORT BREAK";
  if (pomoMode == MODE_LONG_BREAK) return "LONG BREAK";
  return "WORK SESSION";
}

// Focus is the accent colour, breaks are green/violet.  Red is reserved for
// destructive controls, so the ring never suggests something went wrong.
uint16_t pomoModeColor() {
  if (pomoMode == MODE_SHORT_BREAK) return FUNC_COLOR;
  if (pomoMode == MODE_LONG_BREAK) return PLOT_COLOR;
  return ACCENT_COLOR;
}

// ==========================================
// SHARED TOP BAR
// ==========================================
void drawPomoTopBar() {
  tft.fillRect(0, 0, 320, POMO_TAB_H, SURFACE_COLOR);
  drawModernButton(6, 3, 34, POMO_TAB_H - 6, RADIUS_SM, SURFACE_HI, false);
  drawBackChevron(23, POMO_TAB_H / 2, TEXT_COLOR);
  for (int i = 0; i < POMO_VIEW_COUNT; i++) {
    int x = POMO_TAB_X + (i * POMO_TAB_W);
    bool active = ((int)pomoView == i);
    tft.fillRect(x, 0, POMO_TAB_W, POMO_TAB_H, active ? SURFACE_HI : SURFACE_COLOR);
    tft.drawFastVLine(x, 0, POMO_TAB_H, BTN_OUTLINE);
    if (active) tft.fillRect(x, POMO_TAB_H - 3, POMO_TAB_W, 3, ACCENT_COLOR);
    printCentered(POMO_TABS[i], x + (POMO_TAB_W / 2), 20, &FreeSansBold9pt7b, active ? TEXT_COLOR : MUTED_COLOR);
  }
  tft.drawFastHLine(0, POMO_TAB_H, 320, BTN_OUTLINE);
}

int pomoTopBarTab(int sx) {
  if (sx < POMO_TAB_X) return -1;
  int idx = (sx - POMO_TAB_X) / POMO_TAB_W;
  return (idx >= 0 && idx < POMO_VIEW_COUNT) ? idx : -1;
}

void pomoSwitchView(PomoView view) {
  if (pomoView == view) return;
  pomoView = view;
  pomoScrollReset();
  drawPomodoroScreen(true);
}

// ==========================================
// TIMER VIEW
// ==========================================
void pomoStartTiming() {
  // Anchor the countdown to an absolute deadline; the remaining seconds are
  // derived from it every time the loop runs.
  pomoDeadlineMs = millis() + ((unsigned long)pomoSeconds * 1000UL);
  lastPomoTick = millis();
}

void pomoPauseTiming() {
  if (!pomoRunning) return;
  long remaining = (long)(pomoDeadlineMs - millis());
  pomoSeconds = remaining > 0 ? (int)((remaining + 999) / 1000) : 0;
}

void pomoExtendPhase(int seconds) {
  pomoSeconds += seconds;
  pomoPhaseTotal += seconds;
  if (pomoRunning) pomoDeadlineMs += (unsigned long)seconds * 1000UL;
}

void pomoApplyMode(PomoMode mode) {
  pomoMode = mode;
  // Shortening the cycle length must never leave the dot row over-full.
  if (pomodorosCompleted >= pomoLongEvery) pomodorosCompleted = pomoLongEvery - 1;
  int total = pomoWorkTime;
  if (mode == MODE_SHORT_BREAK) total = pomoShortTime;
  else if (mode == MODE_LONG_BREAK) total = pomoLongTime;
  if (total < 60) total = 60;
  pomoPhaseTotal = total;
  pomoSeconds = total;
  pomoStartTiming();
}

void pomoSaveTimerState() {
  savePomoSettings();
}

void pomoLoadTimerState() {
  PomoMode mode = (PomoMode)constrain(prefs.getInt("pomomode", (int)MODE_WORK), 0, 2);
  int total = prefs.getInt("pomotot", 0);
  int left = prefs.getInt("pomoleft", -1);
  if (left < 0) return;  // nothing stored yet: keep the configured lengths
  int want = pomoWorkTime;
  if (mode == MODE_SHORT_BREAK) want = pomoShortTime;
  else if (mode == MODE_LONG_BREAK) want = pomoLongTime;
  if (total < 60) total = want;
  if (left > total) left = total;
  pomoMode = mode;
  pomoPhaseTotal = total;
  pomoSeconds = left;
  pomodorosCompleted = constrain(prefs.getInt("pomocnt", 0), 0, MAX_CYCLES);
  // Restored paused: the user decides whether to continue the session.
  pomoRunning = false;
  pomoStartTiming();
}

PomoMode pomoPhaseAfter(PomoMode finished) {
  if (finished != MODE_WORK) return MODE_WORK;
  pomodorosCompleted++;
  if (pomodorosCompleted >= pomoLongEvery) {
    pomodorosCompleted = 0;
    return MODE_LONG_BREAK;
  }
  return MODE_SHORT_BREAK;
}

void pomoSetMode(PomoMode mode) {
  pomoRunning = false;
  pomoApplyMode(mode);
  pomoSaveTimerState();
  drawPomodoroScreen(true);
}

void drawPomoModeSelector() {
  static const char* const labels[3] = { "WORK", "SHORT", "LONG" };
  drawSegmentedControl(POMO_MODE_X, POMO_MODE_Y, POMO_MODE_W, POMO_MODE_H, labels, 3, (int)pomoMode);
}

void drawPomoProgressDots() {
  int n = constrain(pomoLongEvery, MIN_CYCLES, MAX_CYCLES);
  int spacing = min(13, 96 / n);
  int startX = POMO_DOTS_CX - ((n - 1) * spacing) / 2;
  for (int i = 0; i < n; i++) {
    int cx = startX + (i * spacing);
    if (i < pomodorosCompleted) tft.fillCircle(cx, POMO_DOTS_CY, 4, ACCENT_COLOR);
    else {
      tft.drawCircle(cx, POMO_DOTS_CY, 4, BTN_OUTLINE);
      tft.drawCircle(cx, POMO_DOTS_CY, 3, BTN_OUTLINE);
    }
  }
  // The label makes it obvious that the dots belong to the current cycle.
  printCentered(String(pomodorosCompleted) + "/" + String(n), POMO_DOTS_CX, POMO_DOTS_CY + 20, NULL, MUTED_COLOR);
}

void drawPomoTaskChip() {
  drawCard(POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H, false, RADIUS_SM);
  // Today's goal lives on the right so the chip carries both the current task
  // and the progress of the day.
  int goal = constrain(pomoDailyGoal, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
  int done = pomoStatBlocks(pomoTodayDay());
  String goalText = String(min(done, goal)) + "/" + String(goal) + " today";
  tft.setTextSize(1);
  tft.setTextColor(done >= goal ? PLOT_COLOR : MUTED_COLOR);
  tft.setCursor(POMO_CHIP_X + POMO_CHIP_W - 8 - (goalText.length() * 6), POMO_CHIP_Y + 8);
  tft.print(goalText);

  String label = pomoActiveTaskLabel();
  int maxChars = 30;
  if (label.length() == 0) label = "No task selected - tap TASKS";
  if ((int)label.length() > maxChars) label = label.substring(0, maxChars - 2) + "..";
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(pomoActiveTask < 0 ? MUTED_COLOR : TEXT_COLOR);
  tft.setCursor(POMO_CHIP_X + 10, POMO_CHIP_Y + 15);
  tft.print(label);
  tft.setFont(NULL);
}

void drawPomoTimerView(bool fullWipe) {
  static bool lastRunning = !pomoRunning;
  static int lastAngle = -1;
  static int lastMode = -1;
  static String lastTimeStr = "";
  static String lastChip = "";

  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    drawPomoTopBar();
    lastAngle = -1;
    lastTimeStr = "";
    lastMode = -1;
    lastChip = "";
  }
  if (fullWipe || lastMode != (int)pomoMode) {
    drawPomoModeSelector();
    drawPomoProgressDots();
    lastMode = (int)pomoMode;
  }

  // "+5 MIN": a plain, visible way to stretch the phase without restarting it.
  // It lives in the empty strip beside the ring, so it costs two primitives.
  if (fullWipe) {
    drawModernButton(14, 96, 76, 26, RADIUS_SM, SURFACE_COLOR, false);
    printCentered("+5 MIN", 52, 113, &FreeSans9pt7b, TEXT_COLOR);
  }

  // Start / pause only has to be repainted when the state really changes.
  if (fullWipe || lastRunning != pomoRunning) {
    uint16_t startBg = pomoRunning ? SURFACE_HI : PLOT_COLOR;
    drawModernButton(8, POMO_BTN_Y, 140, POMO_BTN_H, RADIUS_MD, startBg, true);
    printCentered(pomoRunning ? "PAUSE" : "START", 78, POMO_BTN_Y + 25, &FreeSansBold9pt7b, TEXT_COLOR);
    drawModernButton(154, POMO_BTN_Y, 70, POMO_BTN_H, RADIUS_MD, SURFACE_COLOR, true);
    printCentered("SKIP", 189, POMO_BTN_Y + 25, &FreeSans9pt7b, TEXT_COLOR);
    drawModernButton(230, POMO_BTN_Y, 82, POMO_BTN_H, RADIUS_MD, DEL_COLOR, true);
    printCentered("RESET", 271, POMO_BTN_Y + 25, &FreeSansBold9pt7b, TEXT_COLOR);
    lastRunning = pomoRunning;
  }

  uint16_t timerColor = pomoModeColor();
  int totalTime = pomoPhaseTotal > 0 ? pomoPhaseTotal : 1;
  float pct = 1.0f - ((float)pomoSeconds / (float)totalTime);
  pct = constrain(pct, 0.0f, 1.0f);
  int endAngle = constrain((int)(pct * 360), 0, 360);
  String timeStr = formatTime(pomoSeconds);
  if (fullWipe || endAngle != lastAngle || timeStr != lastTimeStr) {
    if (lastTimeStr.length()) printCentered(lastTimeStr, POMO_RING_CX, POMO_RING_CY + 14, &FreeSansBold24pt7b, BG_COLOR);
    tft.startWrite();
    for (int a = 0; a < 360; a++) {
      uint16_t col = (a < endAngle) ? timerColor : SURFACE_COLOR;
      float rad = (a - 90) * PI / 180.0;
      float c = cos(rad), s = sin(rad);
      for (int i = 0; i < POMO_RING_THICK; i++)
        tft.writePixel(POMO_RING_CX + (int)lroundf(c * (POMO_RING_R - i)), POMO_RING_CY + (int)lroundf(s * (POMO_RING_R - i)), col);
    }
    tft.endWrite();
    printCentered(timeStr, POMO_RING_CX, POMO_RING_CY + 14, &FreeSansBold24pt7b, TEXT_COLOR);
    printCentered(pomoModeTitle(), POMO_RING_CX, POMO_RING_CY - 22, &FreeSans9pt7b, MUTED_COLOR);
    lastAngle = endAngle;
    lastTimeStr = timeStr;
  }

  String chip = pomoActiveTaskLabel() + "|" + String(pomoStatBlocks(pomoTodayDay())) + "/" + String(pomoDailyGoal);
  if (fullWipe || chip != lastChip) {
    drawPomoTaskChip();
    lastChip = chip;
  }
}

// ==========================================
// COUNTDOWN
// ==========================================
// Called from the main loop.  The remaining time is derived from the phase
// deadline rather than decremented once per second, so a stalled loop (audio
// refill, SD access, a slow redraw, a wake up from a long pause) can never make
// the countdown drift: the next pass simply jumps to the value the clock says
// it should show, and a phase that was already over ends immediately.
void pomoTick() {
  if (!pomoRunning) {
    lastPomoTick = millis();
    return;
  }
  unsigned long now = millis();
  long remaining = (long)(pomoDeadlineMs - now);
  int wantSeconds = remaining > 0 ? (int)((remaining + 999) / 1000) : 0;
  if (wantSeconds < 0) wantSeconds = 0;
  bool changed = (wantSeconds != pomoSeconds);
  pomoSeconds = wantSeconds;
  if (pomoSeconds > 0) {
    if (changed && currentState == STATE_POMODORO && pomoView == POMO_VIEW_TIMER && displayActive())
      drawPomodoroScreen(false);
    return;
  }

  // The phase is over: credit the focus block, line up the next phase and say
  // what happened (the display wakes up for the announcement).
  String msg = pomoCompletePhase(true);
  setScreenPower(true);
  if (currentState == STATE_POMODORO) {
    // The toast paints over the screen, so the timer view is redrawn after it
    // while the other views only need the single pass.
    if (pomoView == POMO_VIEW_TIMER) showToast(msg);
    drawPomodoroScreen(true);
  } else if (displayActive()) {
    // The phase ended while another app was open: announce it there too.
    showToast(msg);
    redrawCurrentScreen();
  }
  lastPomoTick = now;
}

// ==========================================
// PHASE HANDLING
// ==========================================
String pomoCompletePhase(bool natural) {
  int elapsed = pomoPhaseTotal - pomoSeconds;
  if (elapsed < 0) elapsed = 0;
  PomoMode finished = pomoMode;
  pomoRunning = false;
  if (finished == MODE_WORK) {
    if (natural) {
      // A block that ran to the end counts towards the goal and the streak.
      pomoRecordWorkBlock(pomoPhaseTotal, true);
      pomoCreditActiveTask();
    } else if (elapsed >= 60) {
      // Skipped early: the focused minutes are still worth keeping, but the
      // block itself does not count as completed.
      pomoRecordWorkBlock(elapsed, false);
    }
  }
  if (finished == MODE_WORK) {
    bool longBreak = (pomodorosCompleted + 1) >= pomoLongEvery;
    pomoApplyMode(pomoPhaseAfter(finished));
    String msg = longBreak ? "Long break " : "Short break ";
    msg += String(pomoPhaseTotal / 60) + "m - ";
    msg += longBreak ? "cycle complete" : "nice work";
    pomoSaveTimerState();
    if (pomoAutoStart) {
      pomoRunning = true;
      pomoStartTiming();
    }
    return msg;
  }
  pomoApplyMode(MODE_WORK);
  pomoSaveTimerState();
  if (pomoAutoStart) {
    pomoRunning = true;
    pomoStartTiming();
  }
  return "Break over - back to work";
}

void pomoSkipPhase() {
  pomoRunning = false;
  pomoCompletePhase(false);
  setScreenPower(true);
  drawPomodoroScreen(true);
}

void pomoScrollReset() {
  pomoScrollY = 0;
  pomoScrollMax = 0;
}

// ==========================================
// SCREEN DISPATCH & TOUCH
// ==========================================
void drawPomodoroScreen(bool fullWipe) {
  if (pomoView == POMO_VIEW_TASKS) {
    if (fullWipe) drawPomoTasksView();
    return;
  }
  if (pomoView == POMO_VIEW_PRESETS) {
    if (fullWipe) drawPomoPresetsView();
    return;
  }
  if (pomoView == POMO_VIEW_STATS) {
    if (fullWipe) drawPomoStatsView();
    return;
  }
  drawPomoTimerView(fullWipe);
}

void handlePomodoroTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (sy < POMO_TAB_H) {
    if (sx < POMO_TAB_X) {
      flashButton(6, 3, 34, POMO_TAB_H - 6, RADIUS_SM);
      currentState = STATE_HOME;
      drawHomeScreen();
      return;
    }
    int tab = pomoTopBarTab(sx);
    if (tab >= 0 && tab != (int)pomoView) {
      flashButton(POMO_TAB_X + (tab * POMO_TAB_W), 0, POMO_TAB_W, POMO_TAB_H, 0);
      pomoSwitchView((PomoView)tab);
    }
    return;
  }

  if (pomoView == POMO_VIEW_TASKS) {
    handlePomoTasksTouch(sx, sy);
    return;
  }
  if (pomoView == POMO_VIEW_PRESETS) {
    handlePomoPresetsTouch(sx, sy);
    return;
  }
  if (pomoView == POMO_VIEW_STATS) {
    handlePomoStatsTouch(sx, sy);
    return;
  }

  // ---- timer view ----
  if (inRect(sx, sy, POMO_MODE_X, POMO_MODE_Y, POMO_MODE_W, POMO_MODE_H)) {
    // The segmented control is one pill, so the tap is mapped to a third of it
    // instead of three separate buttons.
    int segW = (POMO_MODE_W - 4) / 3;
    int i = constrain((sx - (POMO_MODE_X + 2)) / segW, 0, 2);
    flashButton(POMO_MODE_X + 2 + (i * segW), POMO_MODE_Y + 2, segW, POMO_MODE_H - 4, (POMO_MODE_H - 4) / 2);
    PomoMode want = (i == 0) ? MODE_WORK : (i == 1 ? MODE_SHORT_BREAK : MODE_LONG_BREAK);
    if (want != pomoMode) pomoSetMode(want);
    else drawPomodoroScreen(true);
    return;
  }
  if (inRect(sx, sy, POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H)) {
    flashButton(POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H, RADIUS_SM);
    pomoSwitchView(POMO_VIEW_TASKS);
    return;
  }
  if (inRect(sx, sy, 8, POMO_BTN_Y, 140, POMO_BTN_H)) {
    flashButton(8, POMO_BTN_Y, 140, POMO_BTN_H, RADIUS_MD);
    if (pomoRunning) pomoPauseTiming();
    else {
      pomoStartTiming();
      setScreenPower(true);
    }
    pomoRunning = !pomoRunning;
    pomoSaveTimerState();
    drawPomodoroScreen(true);
    return;
  }
  if (inRect(sx, sy, 154, POMO_BTN_Y, 70, POMO_BTN_H)) {
    flashButton(154, POMO_BTN_Y, 70, POMO_BTN_H, RADIUS_MD);
    pomoSkipPhase();
    return;
  }
  if (inRect(sx, sy, 230, POMO_BTN_Y, 82, POMO_BTN_H)) {
    flashButton(230, POMO_BTN_Y, 82, POMO_BTN_H, RADIUS_MD);
    // Reset = back to the top of the phase; a running countdown keeps running
    // from the full length instead of being silently paused.
    pomoSeconds = pomoPhaseTotal;
    pomoStartTiming();
    pomoSaveTimerState();
    drawPomodoroScreen(true);
    return;
  }
  // A running timer can be topped up without restarting the phase.
  if (inRect(sx, sy, 14, 96, 76, 26)) {
    flashButton(14, 96, 76, 26, RADIUS_SM);
    pomoExtendPhase(300);
    pomoSaveTimerState();
    drawPomodoroScreen(true);
    return;
  }
}
