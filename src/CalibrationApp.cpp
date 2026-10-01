#include "CalibrationApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TouchDriver.h"

// Forward declaration
void drawSettingsScreen();

// ==========================================
// LAYOUT
// ==========================================
// The targets sit on an inset rectangle so the crosshair is fully on screen;
// the fit maps that rectangle onto the same pixel coordinates, so everything
// outside it (the real 0..320 / 0..240 panel edge) is extrapolated.
static const int CAL_INSET = 24;
static const int CAL_STEP_COUNT = 4;
static const int CAL_CANCEL_X = 96;
static const int CAL_CANCEL_Y = 190;
static const int CAL_CANCEL_W = 128;
static const int CAL_CANCEL_H = 34;

static const char* const CAL_NAMES[CAL_STEP_COUNT] = {
  "TOP LEFT", "TOP RIGHT", "BOTTOM RIGHT", "BOTTOM LEFT"
};

// The raw sample as seen by the legacy min/max mapping (which may have the
// axes swapped), used to keep those values meaningful too.
static int axisX(const TS_Point& p) { return touch_swap_xy ? p.y : p.x; }
static int axisY(const TS_Point& p) { return touch_swap_xy ? p.x : p.y; }

static void calTargetXY(int step, int& x, int& y) {
  int rightX = 320 - CAL_INSET, bottomY = 240 - CAL_INSET;
  switch (step) {
    case 0: x = CAL_INSET; y = CAL_INSET; break;
    case 1: x = rightX; y = CAL_INSET; break;
    case 2: x = rightX; y = bottomY; break;
    default: x = CAL_INSET; y = bottomY; break;
  }
}

void drawCalibrationScreen() {
  // No top bar: the instructions live in the middle of the screen so the four
  // corners stay completely clear for the targets (that is also what makes the
  // screen instantly recognisable as a calibration step).
  tft.fillScreen(BG_COLOR);
  // A card in the middle keeps the four corners free for the targets while the
  // instructions stay readable (the copy is short enough for 320 px).
  drawCard(60, 74, 200, 92, false, RADIUS_MD);
  printCentered("TOUCH CALIBRATION", 160, 96, &FreeSansBold9pt7b, TEXT_COLOR);
  printCentered(String("Step ") + String(calibStep + 1) + " of " + String(CAL_STEP_COUNT),
                160, 116, &FreeSans9pt7b, MUTED_COLOR);
  printCentered(String("Tap the ") + CAL_NAMES[calibStep] + " target", 160, 136, &FreeSans9pt7b, TEXT_COLOR);
  printCentered("Hold the screen steady", 160, 154, NULL, MUTED_COLOR);

  // Progress dots so the user knows how much is left.
  int spacing = 26;
  int startX = 160 - ((CAL_STEP_COUNT - 1) * spacing) / 2;
  for (int i = 0; i < CAL_STEP_COUNT; i++) {
    int cx = startX + (i * spacing);
    if (i < calibStep) tft.fillCircle(cx, 176, 4, PLOT_COLOR);
    else if (i == calibStep) tft.fillCircle(cx, 176, 4, ACCENT_COLOR);
    else tft.drawCircle(cx, 176, 4, BTN_OUTLINE);
  }

  int tx, ty;
  calTargetXY(calibStep, tx, ty);
  drawCrosshairTarget(tx, ty, 10, POINT_COLOR);

  drawModernButton(CAL_CANCEL_X, CAL_CANCEL_Y, CAL_CANCEL_W, CAL_CANCEL_H, RADIUS_MD, SURFACE_HI, false);
  printCentered("CANCEL", CAL_CANCEL_X + (CAL_CANCEL_W / 2), CAL_CANCEL_Y + 23, &FreeSans9pt7b, TEXT_COLOR);
}

// ==========================================
// FIT
// ==========================================
// Least squares solve of  screen = a*raw_x + b*raw_y + c  through the four
// samples.  A 3x3 normal equation system is solved with Cramer's rule, which
// is plenty for four points and keeps the firmware free of a matrix library.
static bool solveAffine(const double rawX[4], const double rawY[4], const double screen[4], double out[3]) {
  double Sxx = 0, Sxy = 0, Syy = 0, Sx = 0, Sy = 0;
  double Tx = 0, Ty = 0, T = 0;
  for (int i = 0; i < 4; i++) {
    Sxx += rawX[i] * rawX[i];
    Sxy += rawX[i] * rawY[i];
    Syy += rawY[i] * rawY[i];
    Sx += rawX[i];
    Sy += rawY[i];
    Tx += screen[i] * rawX[i];
    Ty += screen[i] * rawY[i];
    T += screen[i];
  }
  double m[3][3] = { { Sxx, Sxy, Sx }, { Sxy, Syy, Sy }, { Sx, Sy, 4.0 } };
  double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  // A collapsed or duplicated set of samples makes the system singular.
  if (fabs(det) < 1e-6) return false;
  double rhs[3] = { Tx, Ty, T };
  for (int k = 0; k < 3; k++) {
    double mk[3][3];
    for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++) mk[a][b] = (b == k) ? rhs[a] : m[a][b];
    double dk = mk[0][0] * (mk[1][1] * mk[2][2] - mk[1][2] * mk[2][1])
              - mk[0][1] * (mk[1][0] * mk[2][2] - mk[1][2] * mk[2][0])
              + mk[0][2] * (mk[1][0] * mk[2][1] - mk[1][1] * mk[2][0]);
    out[k] = dk / det;
  }
  for (int k = 0; k < 3; k++)
    if (isnan(out[k]) || isinf(out[k])) return false;
  return true;
}

// The four raw samples must really sit in four different corners.  Without
// this check a few off-target taps would store a wildly wrong mapping and the
// device would become very hard to use.
static bool validateQuad() {
  // Resolve the axis pair first: on a swapped panel the top *left* sample does
  // not share its raw x with the bottom left one, so the column/row tests below
  // only make sense once the axes have been mapped.
  touch_swap_xy = (abs(calTR.x - calTL.x) <= abs(calTR.y - calTL.y));
  double leftX = (axisX(calTL) + axisX(calBL)) / 2.0, rightX = (axisX(calTR) + axisX(calBR)) / 2.0;
  double topY = (axisY(calTL) + axisY(calTR)) / 2.0, bottomY = (axisY(calBL) + axisY(calBR)) / 2.0;
  double dx = fabs(rightX - leftX), dy = fabs(bottomY - topY);
  // The raw axis sits somewhere in a 0..4095 range, so require a real spread.
  if (dx < 350 || dy < 300) return false;
  if (fabs(axisX(calTL) - axisX(calBL)) > dx * 0.7) return false;  // left pair not a column
  if (fabs(axisX(calTR) - axisX(calBR)) > dx * 0.7) return false;
  if (fabs(axisY(calTL) - axisY(calTR)) > dy * 0.7) return false;  // top pair not a row
  if (fabs(axisY(calBL) - axisY(calBR)) > dy * 0.7) return false;
  return true;
}

bool fitTouchCalibration() {
  if (!validateQuad()) return false;
  double rawX[4] = { (double)calTL.x, (double)calTR.x, (double)calBR.x, (double)calBL.x };
  double rawY[4] = { (double)calTL.y, (double)calTR.y, (double)calBR.y, (double)calBL.y };
  double sx[4], sy[4];
  for (int i = 0; i < 4; i++) {
    int tx, ty;
    calTargetXY(i, tx, ty);
    sx[i] = tx;
    sy[i] = ty;
  }
  double cx[3], cy[3];
  if (!solveAffine(rawX, rawY, sx, cx)) return false;
  if (!solveAffine(rawX, rawY, sy, cy)) return false;
  // Sanity check the result: every sample has to land back on its target.  A
  // fit that fails this would map touches to the wrong part of the screen, so
  // it is rejected instead of being stored.
  for (int i = 0; i < 4; i++) {
    double ex = cx[0] * rawX[i] + cx[1] * rawY[i] + cx[2];
    double ey = cy[0] * rawX[i] + cy[1] * rawY[i] + cy[2];
    if (fabs(ex - sx[i]) > 12.0 || fabs(ey - sy[i]) > 12.0) return false;
  }
  for (int i = 0; i < 3; i++) {
    tcalX[i] = (float)cx[i];
    tcalY[i] = (float)cy[i];
  }
  // Keep the legacy min/max values in step: they are written as well so an
  // older build (or a firmware downgrade) still maps sanely.  touch_swap_xy
  // was already resolved by validateQuad().
  int leftX = (axisX(calTL) + axisX(calBL)) / 2;
  int rightX = (axisX(calTR) + axisX(calBR)) / 2;
  int topY = (axisY(calTL) + axisY(calTR)) / 2;
  int bottomY = (axisY(calBL) + axisY(calBR)) / 2;
  int rangeX = rightX - leftX;
  int rangeY = bottomY - topY;
  if (rangeX != 0) {
    if (rangeX < 0) {
      int t = leftX;
      leftX = rightX;
      rightX = t;
      rangeX = -rangeX;
    }
    touch_x_min = leftX - (rangeX * CAL_INSET / (320 - 2 * CAL_INSET));
    touch_x_max = rightX + (rangeX * CAL_INSET / (320 - 2 * CAL_INSET));
  }
  if (rangeY != 0) {
    if (rangeY < 0) {
      int t = topY;
      topY = bottomY;
      bottomY = t;
      rangeY = -rangeY;
    }
    touch_y_min = topY - (rangeY * CAL_INSET / (240 - 2 * CAL_INSET));
    touch_y_max = bottomY + (rangeY * CAL_INSET / (240 - 2 * CAL_INSET));
  }
  touchCalibrated = true;
  return true;
}

void saveTouchCalibration() {
  prefs.putBool("calok", touchCalibrated);
  for (int i = 0; i < 3; i++) {
    prefs.putFloat(("cax" + String(i)).c_str(), tcalX[i]);
    prefs.putFloat(("cay" + String(i)).c_str(), tcalY[i]);
  }
  prefs.putBool("swap_xy", touch_swap_xy);
  prefs.putInt("x_min", touch_x_min);
  prefs.putInt("x_max", touch_x_max);
  prefs.putInt("y_min", touch_y_min);
  prefs.putInt("y_max", touch_y_max);
}

void loadTouchCalibration() {
  bool ok = prefs.getBool("calok", false);
  float cx[3], cy[3];
  for (int i = 0; i < 3; i++) {
    cx[i] = prefs.getFloat(("cax" + String(i)).c_str(), NAN);
    cy[i] = prefs.getFloat(("cay" + String(i)).c_str(), NAN);
  }
  // A half written record falls back to the axis mapping rather than mapping
  // every touch to the top left corner.
  for (int i = 0; i < 3; i++) {
    if (isnan(cx[i]) || isnan(cy[i])) ok = false;
  }
  if (!ok) {
    touchCalibrated = false;
    return;
  }
  for (int i = 0; i < 3; i++) {
    tcalX[i] = cx[i];
    tcalY[i] = cy[i];
  }
  touchCalibrated = true;
}

// ==========================================
// TOUCH HANDLING
// ==========================================
// A tap is a cancel request when the panel maps it near the centre of the
// screen *and* the raw sample is close to the middle of the touch range, so a
// badly calibrated panel can still reach the way out.
static bool cancelRequested(TS_Point p, int sx, int sy) {
  if (!inRect(sx, sy, CAL_CANCEL_X, CAL_CANCEL_Y, CAL_CANCEL_W, CAL_CANCEL_H)) return false;
  return abs(p.x - 2048) < 1100 && abs(p.y - 2048) < 1100;
}

static void backToSettings() {
  currentState = STATE_SETTINGS;
  drawSettingsScreen();
}

void handleCalibrationTouch(bool touched, TS_Point p, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (cancelRequested(p, sx, sy)) {
    calibStep = 0;
    calibFailCount = 0;
    showToast("Calibration cancelled");
    backToSettings();
    return;
  }
  if (calibStep == 0) calTL = p;
  else if (calibStep == 1) calTR = p;
  else if (calibStep == 2) calBR = p;
  else calBL = p;
  calibStep++;
  if (calibStep < CAL_STEP_COUNT) {
    drawCalibrationScreen();
    return;
  }
  calibStep = 0;
  if (!fitTouchCalibration()) {
    // Two taps landed on the same spot (or nearly so): start over instead of
    // saving a mapping that would make the screen unusable.
    calibFailCount++;
    if (calibFailCount >= 3) {
      calibFailCount = 0;
      showToast("Calibration failed - keeping old");
      backToSettings();
      return;
    }
    showToast("Corners too close - try again");
    drawCalibrationScreen();
    return;
  }
  calibFailCount = 0;
  saveTouchCalibration();
  showToast("Touch calibrated");
  backToSettings();
}
