#include "PomodoroApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"

// Forward declaration from HomeApp
void drawHomeScreen();

void drawPomodoroScreen(bool fullWipe) {
  static bool lastPomoRunning = !pomoRunning;
  static int lastAngle = -1;
  static String lastTimeStr = "";
  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
    drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
    drawBackChevron(20, 15, TEXT_COLOR);
    tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);
    String title = "WORK SESSION";
    if (pomoMode == MODE_SHORT_BREAK) title = "SHORT BREAK";
    if (pomoMode == MODE_LONG_BREAK) title = "LONG BREAK";
    printCentered(title, 160, 20, &FreeSansBold9pt7b, TEXT_COLOR);
    printCentered(getTimeString(), 285, 20, &FreeSans9pt7b, MUTED_COLOR);
    int dotSpacing = 30, startX = 160 - (dotSpacing * 1.5);
    for (int i = 0; i < POMOS_BEFORE_LONG; i++) {
      uint16_t dotColor = (i < pomodorosCompleted) ? DEL_COLOR : SURFACE_HI;
      tft.fillCircle(startX + (i * dotSpacing), 45, 5, dotColor);
    }
    drawModernButton(10, 190, 90, 40, RADIUS_MD, SURFACE_HI, true);
    printCentered("WORK", 55, 215, &FreeSans9pt7b, TEXT_COLOR);
    drawModernButton(115, 190, 90, 40, RADIUS_MD, SURFACE_HI, true);
    printCentered("S. BRK", 160, 215, &FreeSans9pt7b, TEXT_COLOR);
    drawModernButton(220, 190, 90, 40, RADIUS_MD, SURFACE_HI, true);
    printCentered("L. BRK", 265, 215, &FreeSans9pt7b, TEXT_COLOR);
    lastAngle = -1;
    lastTimeStr = "";
  }
  if (fullWipe || lastPomoRunning != pomoRunning) {
    drawModernButton(20, 100, 70, 45, RADIUS_MD, pomoRunning ? SURFACE_COLOR : PLOT_COLOR, true);
    printCentered(pomoRunning ? "PAUSE" : "START", 55, 128, &FreeSansBold9pt7b, TEXT_COLOR);
    drawModernButton(230, 100, 70, 45, RADIUS_MD, DEL_COLOR, true);
    printCentered("RESET", 265, 128, &FreeSansBold9pt7b, TEXT_COLOR);
    lastPomoRunning = pomoRunning;
  }
  uint16_t timerColor = DEL_COLOR;
  int totalTime = WORK_TIME;
  if (pomoMode == MODE_SHORT_BREAK) {
    timerColor = FUNC_COLOR;
    totalTime = SHORT_BREAK_TIME;
  }
  if (pomoMode == MODE_LONG_BREAK) {
    timerColor = PLOT_COLOR;
    totalTime = LONG_BREAK_TIME;
  }
  const int cx = 160, cy = 115, r = 50, thick = 6;
  float pct = 1.0 - ((float)pomoSeconds / (float)totalTime);
  int endAngle = constrain((int)(pct * 360), 0, 360);
  String timeStr = formatTime(pomoSeconds);
  if (fullWipe || endAngle != lastAngle || timeStr != lastTimeStr) {
    if (lastTimeStr.length()) printCentered(lastTimeStr, cx, cy + 10, &FreeSansBold18pt7b, BG_COLOR);
    tft.startWrite();
    for (int a = 0; a < 360; a++) {
      uint16_t col = (a < endAngle) ? timerColor : SURFACE_COLOR;
      float rad = (a - 90) * PI / 180.0;
      float c = cos(rad), s = sin(rad);
      for (int i = 0; i < thick; i++) tft.writePixel(cx + (int)lroundf(c * (r - i)), cy + (int)lroundf(s * (r - i)), col);
    }
    tft.endWrite();
    printCentered(timeStr, cx, cy + 10, &FreeSansBold18pt7b, TEXT_COLOR);
    lastAngle = endAngle;
    lastTimeStr = timeStr;
  }
}

void handlePomodoroTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(0, 0, 40, 30, 0);
    currentState = STATE_HOME;
    drawHomeScreen();
  } else if (inRect(sx, sy, 20, 100, 70, 45)) {
    flashButton(20, 100, 70, 45, RADIUS_MD);
    pomoRunning = !pomoRunning;
    if (pomoRunning) lastPomoTick = millis();
    drawPomodoroScreen(false);
  } else if (inRect(sx, sy, 230, 100, 70, 45)) {
    flashButton(230, 100, 70, 45, RADIUS_MD);
    pomoRunning = false;
    if (pomoMode == MODE_WORK) pomoSeconds = WORK_TIME;
    else if (pomoMode == MODE_SHORT_BREAK) pomoSeconds = SHORT_BREAK_TIME;
    else pomoSeconds = LONG_BREAK_TIME;
    drawPomodoroScreen(false);
  } else if (inRect(sx, sy, 10, 190, 90, 40)) {
    flashButton(10, 190, 90, 40, RADIUS_MD);
    pomoMode = MODE_WORK;
    pomoSeconds = WORK_TIME;
    pomoRunning = false;
    drawPomodoroScreen(true);
  } else if (inRect(sx, sy, 115, 190, 90, 40)) {
    flashButton(115, 190, 90, 40, RADIUS_MD);
    pomoMode = MODE_SHORT_BREAK;
    pomoSeconds = SHORT_BREAK_TIME;
    pomoRunning = false;
    drawPomodoroScreen(true);
  } else if (inRect(sx, sy, 220, 190, 90, 40)) {
    flashButton(220, 190, 90, 40, RADIUS_MD);
    pomoMode = MODE_LONG_BREAK;
    pomoSeconds = LONG_BREAK_TIME;
    pomoRunning = false;
    drawPomodoroScreen(true);
  }
}
