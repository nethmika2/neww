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
static const int POMO_MODE_Y = 34;
static const int POMO_MODE_H = 24;
static const int POMO_DOTS_Y = 70;
static const int POMO_RING_CX = 160;
static const int POMO_RING_CY = 118;
static const int POMO_RING_R = 44;
static const int POMO_RING_THICK = 6;
static const int POMO_CHIP_X = 8;
static const int POMO_CHIP_Y = 168;
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

uint16_t pomoModeColor() {
  if (pomoMode == MODE_SHORT_BREAK) return FUNC_COLOR;
  if (pomoMode == MODE_LONG_BREAK) return PLOT_COLOR;
  return DEL_COLOR;
}

// ==========================================
// SHARED TOP BAR
// ==========================================
void drawPomoTopBar() {
  tft.fillRect(0, 0, 320, POMO_TAB_H, SURFACE_COLOR);
  drawModernButton(0, 0, 40, POMO_TAB_H, 0, DEL_COLOR, false);
  drawBackChevron(20, POMO_TAB_H / 2, TEXT_COLOR);
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
  drawPomodoroScreen(true);
}

// ==========================================
// TIMER VIEW
// ==========================================
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
  lastPomoTick = millis();
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
  drawPomodoroScreen(true);
}

void drawPomoModeSelector() {
  static const char* const labels[3] = { "WORK", "SHORT", "LONG" };
  static const PomoMode modes[3] = { MODE_WORK, MODE_SHORT_BREAK, MODE_LONG_BREAK };
  static const uint16_t colors[3] = { DEL_COLOR, FUNC_COLOR, PLOT_COLOR };
  for (int i = 0; i < 3; i++) {
    int x = 8 + (i * 103);
    bool active = (pomoMode == modes[i]);
    drawModernButton(x, POMO_MODE_Y, 96, POMO_MODE_H, RADIUS_SM, active ? colors[i] : SURFACE_COLOR, false);
    printCentered(labels[i], x + 48, POMO_MODE_Y + 17, &FreeSans9pt7b, active ? BG_COLOR : TEXT_COLOR);
  }
}

void drawPomoProgressDots() {
  int n = constrain(pomoLongEvery, MIN_CYCLES, MAX_CYCLES);
  int spacing = min(26, 190 / n);
  int startX = 160 - ((n - 1) * spacing) / 2;
  for (int i = 0; i < n; i++) {
    int cx = startX + (i * spacing);
    if (i < pomodorosCompleted) tft.fillCircle(cx, POMO_DOTS_Y, 5, DEL_COLOR);
    else tft.drawCircle(cx, POMO_DOTS_Y, 5, SURFACE_HI);
  }
  // The current time keeps the timer screen useful as a clock too.
  printCentered(getTimeString(), 296, POMO_DOTS_Y + 4, &FreeSans9pt7b, MUTED_COLOR);
  tft.fillRect(258, POMO_DOTS_Y - 10, 2, 20, BG_COLOR);
}

void drawPomoTaskChip() {
  tft.fillRoundRect(POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H, RADIUS_SM, SURFACE_COLOR);
  tft.drawRoundRect(POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H, RADIUS_SM, BTN_OUTLINE);
  String label = pomoActiveTaskLabel();
  if (label.length() == 0) {
    printCentered("No task selected - tap TASKS", 160, POMO_CHIP_Y + 14, &FreeSans9pt7b, MUTED_COLOR);
    return;
  }
  tft.setTextSize(1);
  tft.setTextColor(ACCENT_COLOR);
  tft.setCursor(POMO_CHIP_X + 8, POMO_CHIP_Y + 8);
  tft.print("NOW");
  const int maxChars = 34;
  if ((int)label.length() > maxChars) label = label.substring(0, maxChars - 2) + "..";
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(POMO_CHIP_X + 34, POMO_CHIP_Y + 15);
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
    if (lastTimeStr.length()) printCentered(lastTimeStr, POMO_RING_CX, POMO_RING_CY + 10, &FreeSansBold18pt7b, BG_COLOR);
    tft.startWrite();
    for (int a = 0; a < 360; a++) {
      uint16_t col = (a < endAngle) ? timerColor : SURFACE_COLOR;
      float rad = (a - 90) * PI / 180.0;
      float c = cos(rad), s = sin(rad);
      for (int i = 0; i < POMO_RING_THICK; i++)
        tft.writePixel(POMO_RING_CX + (int)lroundf(c * (POMO_RING_R - i)), POMO_RING_CY + (int)lroundf(s * (POMO_RING_R - i)), col);
    }
    tft.endWrite();
    printCentered(timeStr, POMO_RING_CX, POMO_RING_CY + 10, &FreeSansBold18pt7b, TEXT_COLOR);
    printCentered(pomoModeTitle(), POMO_RING_CX, POMO_RING_CY - 22, &FreeSans9pt7b, MUTED_COLOR);
    lastAngle = endAngle;
    lastTimeStr = timeStr;
  }

  String chip = pomoActiveTaskLabel();
  if (fullWipe || chip != lastChip) {
    drawPomoTaskChip();
    lastChip = chip;
  }
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
    return msg;
  }
  pomoApplyMode(MODE_WORK);
  return "Break over - back to work";
}

void pomoSkipPhase() {
  pomoRunning = false;
  pomoCompletePhase(false);
  setScreenPower(true);
  drawPomodoroScreen(true);
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
      flashButton(0, 0, 40, POMO_TAB_H, 0);
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
  for (int i = 0; i < 3; i++) {
    int x = 8 + (i * 103);
    if (inRect(sx, sy, x, POMO_MODE_Y, 96, POMO_MODE_H)) {
      flashButton(x, POMO_MODE_Y, 96, POMO_MODE_H, RADIUS_SM);
      PomoMode want = (i == 0) ? MODE_WORK : (i == 1 ? MODE_SHORT_BREAK : MODE_LONG_BREAK);
      if (want != pomoMode) pomoSetMode(want);
      else drawPomodoroScreen(true);
      return;
    }
  }
  if (inRect(sx, sy, POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H)) {
    flashButton(POMO_CHIP_X, POMO_CHIP_Y, POMO_CHIP_W, POMO_CHIP_H, RADIUS_SM);
    pomoSwitchView(POMO_VIEW_TASKS);
    return;
  }
  if (inRect(sx, sy, 8, POMO_BTN_Y, 140, POMO_BTN_H)) {
    flashButton(8, POMO_BTN_Y, 140, POMO_BTN_H, RADIUS_MD);
    pomoRunning = !pomoRunning;
    if (pomoRunning) {
      lastPomoTick = millis();
      setScreenPower(true);
    }
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
    pomoRunning = false;
    pomoApplyMode(pomoMode);
    drawPomodoroScreen(true);
    return;
  }
}
