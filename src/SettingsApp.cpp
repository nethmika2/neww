#include "SettingsApp.h"
#include <time.h>
#include <sys/time.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "EarbudControls.h"

// Forward declarations
void drawHomeScreen();
void drawCalibrationScreen();

// ==========================================
// LAYOUT
// ==========================================
// Four toggle cards in a 2x2 grid, then one wide card for the clock.  Cards sit
// on the same 8 px margin grid the other apps use.
static const int CARD_W = 148;
static const int CARD_H = 58;
static const int COL_L = 10;
static const int COL_R = 162;
static const int ROW_1 = 38;
static const int ROW_2 = 102;
static const int CLOCK_CARD_X = 10;
static const int CLOCK_CARD_Y = 166;
static const int CLOCK_CARD_W = 300;
static const int CLOCK_CARD_H = 68;
static const int CLOCK_BTN_Y = 196;
static const int CLOCK_BTN_H = 30;
static const int CLOCK_BTN_W = 70;

static void drawToggleCard(int x, int y, const char* title, const char* const* labels, int active) {
  drawCard(x, y, CARD_W, CARD_H, false, RADIUS_MD);
  drawSectionLabel(title, x + 10, y + 13);
  drawSegmentedControl(x + 10, y + 26, CARD_W - 20, 26, labels, 2, active);
}

void drawSettingsScreen() {
  tft.fillScreen(BG_COLOR);
  drawScreenHeader("SETTINGS", true);

  static const char* const axisLabels[2] = { "NUM", "PI" };
  static const char* const idleLabels[2] = { "CLOCK", "OFF" };
  static const char* const budLabels[2] = { "ON", "OFF" };

  drawToggleCard(COL_L, ROW_1, "X AXIS", axisLabels, xAxisPi ? 1 : 0);
  drawToggleCard(COL_R, ROW_1, "Y AXIS", axisLabels, yAxisPi ? 1 : 0);
  drawToggleCard(COL_L, ROW_2, "IDLE SCREEN", idleLabels, screensaverEnabled ? 0 : 1);
  drawToggleCard(COL_R, ROW_2, "EARBUDS", budLabels, earbudControlsEnabled() ? 0 : 1);
  // The counter is a live diagnostic: it moves as soon as the buds send
  // anything, so a hardware problem can be told apart from a settings problem
  // without a serial monitor.
  printRight("seen " + String(earbudEventCount()), COL_R + CARD_W - 12, ROW_2 + 13, NULL, MUTED_COLOR);

  // Clock card: the time is the headline, the four buttons are the actions.
  drawCard(CLOCK_CARD_X, CLOCK_CARD_Y, CLOCK_CARD_W, CLOCK_CARD_H, false, RADIUS_MD);
  drawSectionLabel(timeSynced ? "CLOCK - SYNCED" : "CLOCK - NOT SYNCED", CLOCK_CARD_X + 10, CLOCK_CARD_Y + 14);
  printCentered(getTimeString(), 262, CLOCK_CARD_Y + 24, &FreeSansBold18pt7b, timeSynced ? TEXT_COLOR : MUTED_COLOR);

  int btnX = CLOCK_CARD_X + 6;
  const int gap = 4;
  drawModernButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM, SURFACE_HI, false);
  printCentered("+15m", btnX + (CLOCK_BTN_W / 2), CLOCK_BTN_Y + 20, &FreeSans9pt7b, TEXT_COLOR);
  btnX += CLOCK_BTN_W + gap;
  drawModernButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM, PLOT_COLOR, false);
  printCentered("SYNC", btnX + (CLOCK_BTN_W / 2), CLOCK_BTN_Y + 20, &FreeSans9pt7b, TEXT_COLOR);
  btnX += CLOCK_BTN_W + gap;
  drawModernButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM, autoSyncBoot ? ACCENT_COLOR : SURFACE_HI, false);
  printCentered(autoSyncBoot ? "AUTO ON" : "AUTO OFF", btnX + (CLOCK_BTN_W / 2), CLOCK_BTN_Y + 20, &FreeSans9pt7b, autoSyncBoot ? BG_COLOR : TEXT_COLOR);
  btnX += CLOCK_BTN_W + gap;
  drawModernButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM, SURFACE_HI, false);
  printCentered("TOUCH CAL", btnX + (CLOCK_BTN_W / 2), CLOCK_BTN_Y + 20, &FreeSans9pt7b, TEXT_COLOR);
}

void handleSettingsTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(6, 3, 34, 24, RADIUS_SM);
    currentState = STATE_HOME;
    drawHomeScreen();
    return;
  }

  // X axis
  if (inRect(sx, sy, COL_L + 10, ROW_1 + 24, CARD_W - 20, 26)) {
    bool pi = sx > COL_L + (CARD_W / 2);
    flashButton(COL_L + 10, ROW_1 + 24, CARD_W - 20, 26, RADIUS_SM);
    xAxisPi = pi;
    prefs.putBool("xpi", xAxisPi);
    drawSettingsScreen();
    return;
  }
  // Y axis
  if (inRect(sx, sy, COL_R + 10, ROW_1 + 24, CARD_W - 20, 26)) {
    bool pi = sx > COL_R + (CARD_W / 2);
    flashButton(COL_R + 10, ROW_1 + 24, CARD_W - 20, 26, RADIUS_SM);
    yAxisPi = pi;
    prefs.putBool("ypi", yAxisPi);
    drawSettingsScreen();
    return;
  }
  // Idle screen behaviour
  if (inRect(sx, sy, COL_L + 10, ROW_2 + 24, CARD_W - 20, 26)) {
    bool clock = sx <= COL_L + (CARD_W / 2);
    flashButton(COL_L + 10, ROW_2 + 24, CARD_W - 20, 26, RADIUS_SM);
    screensaverEnabled = clock;
    prefs.putBool("screensaver", screensaverEnabled);
    drawSettingsScreen();
    return;
  }
  // Earbud transport buttons
  if (inRect(sx, sy, COL_R + 10, ROW_2 + 24, CARD_W - 20, 26)) {
    bool on = sx <= COL_R + (CARD_W / 2);
    flashButton(COL_R + 10, ROW_2 + 24, CARD_W - 20, 26, RADIUS_SM);
    earbudControlsSetEnabled(on);
    showToast(on ? "Earbud buttons on" : "Earbud buttons off");
    drawSettingsScreen();
    return;
  }

  int btnX = CLOCK_CARD_X + 6;
  const int gap = 4;
  if (inRect(sx, sy, btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H)) {
    flashButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM);
    time_t now;
    time(&now);
    now += 15 * 60;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    drawSettingsScreen();
    return;
  }
  btnX += CLOCK_BTN_W + gap;
  if (inRect(sx, sy, btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H)) {
    flashButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM);
    syncTimeNTP(true);
    drawSettingsScreen();
    return;
  }
  btnX += CLOCK_BTN_W + gap;
  if (inRect(sx, sy, btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H)) {
    flashButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM);
    autoSyncBoot = !autoSyncBoot;
    prefs.putBool("autosync", autoSyncBoot);
    drawSettingsScreen();
    return;
  }
  btnX += CLOCK_BTN_W + gap;
  if (inRect(sx, sy, btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H)) {
    flashButton(btnX, CLOCK_BTN_Y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM);
    currentState = STATE_CALIBRATE;
    calibStep = 0;
    calibFailCount = 0;
    drawCalibrationScreen();
    return;
  }
}
