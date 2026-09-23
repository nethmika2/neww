#pragma once
#include <Arduino.h>
#include "Types.h"

// ==========================================
// TOUCH CALIBRATION
// ==========================================
// Four corner targets are sampled and an affine transform per axis is fitted
// through them, which is noticeably more accurate than the old two point
// min/max mapping because rotation, shear and a swapped axis pair are all
// accounted for.

void drawCalibrationScreen();
// p is the raw sample (used for the fit), sx/sy the currently mapped position
// (only used to recognise the cancel button on a badly calibrated panel).
void handleCalibrationTouch(bool touched, TS_Point p, int sx, int sy);

void loadTouchCalibration();
void saveTouchCalibration();
bool fitTouchCalibration();  // exposed for diagnostics/tests
