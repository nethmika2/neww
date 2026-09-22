#include "SettingsApp.h"
#include <time.h>
#include <sys/time.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"

// Forward declarations
void drawHomeScreen();
void drawCalibrationScreen();

void drawSettingsScreen() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
  drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
  drawBackChevron(20, 15, TEXT_COLOR);
  printCentered("SETTINGS", 160, 20, &FreeSansBold9pt7b, TEXT_COLOR);
  tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);

  printCentered("X-Axis", 82, 50, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(20, 58, 60, 34, RADIUS_MD, !xAxisPi ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("NUM", 50, 80, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(85, 58, 60, 34, RADIUS_MD, xAxisPi ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("PI", 115, 80, &FreeSans9pt7b, TEXT_COLOR);

  printCentered("Y-Axis", 237, 50, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(175, 58, 60, 34, RADIUS_MD, !yAxisPi ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("NUM", 205, 80, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(240, 58, 60, 34, RADIUS_MD, yAxisPi ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("PI", 270, 80, &FreeSans9pt7b, TEXT_COLOR);

  printCentered("Idle: clock / off", 82, 114, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(20, 122, 60, 34, RADIUS_MD, screensaverEnabled ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("CLOCK", 50, 144, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(85, 122, 60, 34, RADIUS_MD, !screensaverEnabled ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("OFF", 115, 144, &FreeSans9pt7b, TEXT_COLOR);

  printCentered(timeSynced ? "Clock (synced)" : "Clock (not synced)", 237, 114, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(175, 122, 125, 34, RADIUS_MD, SURFACE_HI, true);
  printCentered(getTimeString(), 237, 148, &FreeSansBold18pt7b, timeSynced ? TEXT_COLOR : MUTED_COLOR);

  printCentered("Time:  nudge  |  sync now  |  sync at boot", 160, 178, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(18, 185, 55, 40, RADIUS_MD, SURFACE_COLOR, true);
  printCentered("+15", 45, 210, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(79, 185, 60, 40, RADIUS_MD, PLOT_COLOR, true);
  printCentered("SYNC", 109, 210, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(145, 185, 85, 40, RADIUS_MD, autoSyncBoot ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered(autoSyncBoot ? "AUTO ON" : "AUTO OFF", 187, 210, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(236, 185, 65, 40, RADIUS_MD, F2_COLOR, true);
  printCentered("CALIB", 268, 210, &FreeSans9pt7b, BG_COLOR);
}

void handleSettingsTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(0, 0, 40, 30, 0);
    currentState = STATE_HOME;
    drawHomeScreen();
  } else if (inRect(sx, sy, 20, 58, 60, 34)) {
    flashButton(20, 58, 60, 34, RADIUS_MD);
    xAxisPi = false;
    prefs.putBool("xpi", false);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 85, 58, 60, 34)) {
    flashButton(85, 58, 60, 34, RADIUS_MD);
    xAxisPi = true;
    prefs.putBool("xpi", true);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 175, 58, 60, 34)) {
    flashButton(175, 58, 60, 34, RADIUS_MD);
    yAxisPi = false;
    prefs.putBool("ypi", false);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 240, 58, 60, 34)) {
    flashButton(240, 58, 60, 34, RADIUS_MD);
    yAxisPi = true;
    prefs.putBool("ypi", true);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 20, 122, 60, 34)) {
    flashButton(20, 122, 60, 34, RADIUS_MD);
    screensaverEnabled = true;
    prefs.putBool("screensaver", true);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 85, 122, 60, 34)) {
    flashButton(85, 122, 60, 34, RADIUS_MD);
    screensaverEnabled = false;
    prefs.putBool("screensaver", false);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 18, 185, 55, 40)) {
    flashButton(18, 185, 55, 40, RADIUS_MD);
    time_t now;
    time(&now);
    now += 15 * 60;
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 79, 185, 60, 40)) {
    flashButton(79, 185, 60, 40, RADIUS_MD);
    syncTimeNTP(true);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 145, 185, 85, 40)) {
    flashButton(145, 185, 85, 40, RADIUS_MD);
    autoSyncBoot = !autoSyncBoot;
    prefs.putBool("autosync", autoSyncBoot);
    drawSettingsScreen();
  } else if (inRect(sx, sy, 236, 185, 65, 40)) {
    flashButton(236, 185, 65, 40, RADIUS_MD);
    currentState = STATE_CALIBRATE;
    calibStep = 0;
    drawCalibrationScreen();
  }
}
