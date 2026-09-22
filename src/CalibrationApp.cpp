#include "CalibrationApp.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "TouchDriver.h"

// Forward declaration
void drawSettingsScreen();

void drawCalibrationScreen() {
  tft.fillScreen(BG_COLOR);
  printCentered("TOUCH CALIBRATION", 160, 28, &FreeSansBold9pt7b, TEXT_COLOR);
  printCentered("Step " + String(calibStep + 1) + " of 3", 160, 50, &FreeSans9pt7b, MUTED_COLOR);
  if (calibStep == 0) {
    printCentered("Touch TOP LEFT corner", 160, 120, &FreeSansBold9pt7b, TEXT_COLOR);
    tft.fillCircle(20, 20, 10, DEL_COLOR);
    tft.fillCircle(20, 20, 4, TEXT_COLOR);
  } else if (calibStep == 1) {
    printCentered("Touch TOP RIGHT corner", 160, 120, &FreeSansBold9pt7b, TEXT_COLOR);
    tft.fillCircle(300, 20, 10, DEL_COLOR);
    tft.fillCircle(300, 20, 4, TEXT_COLOR);
  } else if (calibStep == 2) {
    printCentered("Touch BOTTOM RIGHT corner", 160, 120, &FreeSansBold9pt7b, TEXT_COLOR);
    tft.fillCircle(300, 220, 10, DEL_COLOR);
    tft.fillCircle(300, 220, 4, TEXT_COLOR);
  }
}

void handleCalibrationTouch(bool touched, TS_Point p) {
  if (!touched) return;
  waitTouchRelease();
  if (calibStep == 0) {
    calTL = p;
    calibStep++;
    drawCalibrationScreen();
  } else if (calibStep == 1) {
    calTR = p;
    calibStep++;
    drawCalibrationScreen();
  } else if (calibStep == 2) {
    calBR = p;
    touch_swap_xy = (abs(calTR.x - calTL.x) <= abs(calTR.y - calTL.y));
    int tl_x = touch_swap_xy ? calTL.y : calTL.x, tl_y = touch_swap_xy ? calTL.x : calTL.y;
    int br_x = touch_swap_xy ? calBR.y : calBR.x, br_y = touch_swap_xy ? calBR.x : calBR.y;
    int range_x = br_x - tl_x;
    touch_x_min = tl_x - (range_x * 20 / 280);
    touch_x_max = br_x + (range_x * 20 / 280);
    int range_y = br_y - tl_y;
    touch_y_min = tl_y - (range_y * 20 / 200);
    touch_y_max = br_y + (range_y * 20 / 200);
    prefs.putBool("swap_xy", touch_swap_xy);
    prefs.putInt("x_min", touch_x_min);
    prefs.putInt("x_max", touch_x_max);
    prefs.putInt("y_min", touch_y_min);
    prefs.putInt("y_max", touch_y_max);
    currentState = STATE_SETTINGS;
    drawSettingsScreen();
  }
}
