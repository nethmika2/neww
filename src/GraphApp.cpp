#include "GraphApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "MathEngine.h"
#include "Storage.h"
#include "KeyboardApp.h"

// Forward declaration from HomeApp
void drawHomeScreen();

// ==========================================
// GRAPHER SCREEN & RENDERING
// ==========================================
void tryHole(int i, double c, int topY) {
  te_expr* e = funcs[i].exprX;
  math_x = c;
  double fc = te_eval(e);
  if (!isnan(fc) && !isinf(fc)) return;
  double eps = 1e-6 * fmax(1.0, fabs(c));
  math_x = c - eps;
  double fl = te_eval(e);
  math_x = c + eps;
  double fr = te_eval(e);
  if (isnan(fl) || isinf(fl) || isnan(fr) || isinf(fr)) return;
  if (fabs(fl) > 1e9 || fabs(fl - fr) > 1e-3 * fmax(1.0, fabs(fl))) return;
  double y = (fl + fr) / 2.0;
  int sx = worldXToScreen(c), sy = worldYToScreen(y);
  if (sx < 0 || sx > 320 || sy < topY + 4 || sy > 191) return;
  tft.fillCircle(sx, sy, 4, BG_COLOR);
  tft.drawCircle(sx, sy, 4, funcs[i].color);
  tft.drawCircle(sx, sy, 3, funcs[i].color);
}

void checkHoleRange(int i, double step, bool roundDec, double xmin, double xmax, int topY) {
  long k0 = (long)ceil(xmin / step), k1 = (long)floor(xmax / step);
  if (k1 - k0 > 400) return;
  for (long k = k0; k <= k1; k++) {
    double c = k * step;
    if (roundDec) c = round(c * 1e9) / 1e9;
    tryHole(i, c, topY);
  }
}

void drawHoles(int i, int topY) {
  if (funcs[i].type != EQ_EXPLICIT) return;
  double xmin = screenXToWorld(0), xmax = screenXToWorld(320);
  double stepX = xAxisPi ? getPiStep(60.0 / zoom) : getNiceStep(60.0 / zoom);
  double h = stepX / 10.0;
  checkHoleRange(i, h, true, xmin, xmax, topY);
  if (h > 1.0) checkHoleRange(i, 1.0, true, xmin, xmax, topY);
  checkHoleRange(i, PI / 4.0, false, xmin, xmax, topY);
}

void drawPoints(int topY) {
  tft.setFont(NULL);
  tft.setTextSize(1);
  for (int k = 0; k < numPoints; k++) {
    int sx = worldXToScreen(points[k].x), sy = worldYToScreen(points[k].y);
    if (sx < 0 || sx > 320 || sy < topY || sy > 192) continue;
    tft.fillCircle(sx, sy, 4, TEXT_COLOR);
    tft.fillCircle(sx, sy, 2, POINT_COLOR);
    String lbl = "(" + niceNum(points[k].x) + ", " + niceNum(points[k].y) + ")";
    int w = lbl.length() * 6;
    int lx = sx + 7;
    if (lx + w > 318) lx = sx - 7 - w;
    int ly = constrain(sy - 12, topY + 2, 184);
    tft.setTextColor(BG_COLOR);
    tft.setCursor(lx + 1, ly + 1);
    tft.print(lbl);
    tft.setTextColor(POINT_COLOR);
    tft.setCursor(lx, ly);
    tft.print(lbl);
  }
}

// ==========================================
// VARIABLE SLIDER ANIMATION
// ==========================================
// Desmos-style playback: the play button sweeps the active slider between
// its min and max so curves like y = m*x + c can be watched as m varies.
// The value ping-pongs (reversing at the end stops) to avoid a jarring
// wrap jump, and frames are throttled so the ESP32 keeps up with redraws.

// Bottom row of the variable panel: play button, slider track, speed.
static const int SLIDER_PLAY_X = 15;
static const int SLIDER_PLAY_Y = 197;
static const int SLIDER_PLAY_W = 36;
static const int SLIDER_PLAY_H = 25;
static const int SLIDER_TRACK_X = 60;
static const int SLIDER_TRACK_Y = 205;
static const int SLIDER_TRACK_W = 170;
static const int SLIDER_TRACK_H = 6;
static const int SLIDER_SPEED_X = 240;
static const int SLIDER_SPEED_Y = 197;
static const int SLIDER_SPEED_W = 55;
static const int SLIDER_SPEED_H = 25;

// A full min -> max sweep takes ANIM_SWEEP_SEC at 1x; frames are capped.
static const unsigned long ANIM_FRAME_MS = 90;
static const float ANIM_SWEEP_SEC = 8.0;
static const float ANIM_SPEEDS[] = { 0.5f, 1.0f, 2.0f, 4.0f };
static const char* const ANIM_SPEED_LABELS[] = { "0.5x", "1x", "2x", "4x" };
static const int NUM_ANIM_SPEEDS = sizeof(ANIM_SPEEDS) / sizeof(ANIM_SPEEDS[0]);

void toggleVarAnimation() {
  if (activeVarIdx < 0 || activeVarIdx >= NUM_CUSTOM_VARS || !sliders[activeVarIdx].in_use) {
    showToast("No variable to animate");
    return;
  }
  varAnimating = !varAnimating;
  if (varAnimating) {
    // Restart the frame clock and head back into range when starting from
    // an end stop so the first frame moves visibly.
    lastAnimTime = millis();
    if (sliders[activeVarIdx].value >= sliders[activeVarIdx].max_val) animDirection = -1;
    else if (sliders[activeVarIdx].value <= sliders[activeVarIdx].min_val) animDirection = 1;
  } else {
    saveVariables();
  }
  drawGraphScreen(true);
}

void stopVarAnimation() {
  if (!varAnimating) return;
  varAnimating = false;
  saveVariables();
}

void cycleAnimSpeed() {
  animSpeedIdx = (animSpeedIdx + 1) % NUM_ANIM_SPEEDS;
  // Apply the new speed from a clean dt so the knob never jumps.
  lastAnimTime = millis();
  drawGraphScreen(true);
}

void tickVarAnimation() {
  if (!varAnimating || !varPanelOpen) return;
  if (activeVarIdx < 0 || activeVarIdx >= NUM_CUSTOM_VARS || !sliders[activeVarIdx].in_use) {
    // The animated variable went away (equation edited); park the player.
    stopVarAnimation();
    drawGraphScreen(true);
    return;
  }
  unsigned long now = millis();
  if (now - lastAnimTime < ANIM_FRAME_MS) return;
  float dt = (now - lastAnimTime) / 1000.0f;
  lastAnimTime = now;
  // Clamp dt so a slow frame or a wake from sleep can't fling the value.
  if (dt > 0.5f) dt = 0.5f;
  CustomVar& s = sliders[activeVarIdx];
  double range = s.max_val - s.min_val;
  if (range <= 0) return;
  s.value += dt * range / ANIM_SWEEP_SEC * ANIM_SPEEDS[animSpeedIdx] * animDirection;
  if (s.value >= s.max_val) {
    s.value = s.max_val;
    animDirection = -1;
  } else if (s.value <= s.min_val) {
    s.value = s.min_val;
    animDirection = 1;
  }
  // The viewport/grid is static while animating, so skip the full wipe and
  // spend the frame budget on the plot plus the panel redraw.
  drawGraphScreen(false);
}

void drawGraphScreen(bool fullWipe) {
  int topY = tabsVisible ? 31 : 0;
  if (needsFullWipe && !fullWipe) {
    fullWipe = true;
    needsFullWipe = false;
  }

  if (fullWipe) {
    tft.fillRect(0, topY, 320, 240 - topY, BG_COLOR);
  } else {
    for (int i = 0; i < NUM_FUNCS; i++) {
      if (!funcs[i].visible || funcs[i].type != EQ_EXPLICIT) continue;
      for (int x = 1; x <= 320; x++) {
        int y1 = prev_y[i][x - 1], y2 = prev_y[i][x];
        if (y1 != -1000 && y2 != -1000 && !((y1 < topY && y2 < topY) || (y1 > 240 && y2 > 240))) {
          y1 = constrain(y1, topY, 240);
          y2 = constrain(y2, topY, 240);
          tft.drawLine(x - 1, y1, x, y2, BG_COLOR);
          tft.drawLine(x - 1, y1 + 1, x, y2 + 1, BG_COLOR);
        }
      }
    }
    if (old_tsx != -1) {
      tft.fillCircle(old_tsx, old_tsy, 5, BG_COLOR);
      tft.fillRect(4, topY + 4, old_boxW + 2, 20, BG_COLOR);
    }
    if (old_ax >= 0 && old_ax <= 320) tft.drawLine(old_ax, topY, old_ax, 240, BG_COLOR);
    if (old_ay >= topY && old_ay <= 240) tft.drawLine(0, old_ay, 320, old_ay, BG_COLOR);
  }

  int ax = worldXToScreen(0), ay = worldYToScreen(0);
  if (fullWipe) {
    tft.setTextColor(TEXT_COLOR);
    tft.setTextSize(1);
    double stepX = xAxisPi ? getPiStep(60.0 / zoom) : getNiceStep(60.0 / zoom);
    for (double x = ceil((centerWorldX - 160.0 / zoom) / stepX) * stepX; x <= centerWorldX + 160.0 / zoom; x += stepX) {
      int sx = worldXToScreen(x);
      if (sx >= 0 && sx <= 320) {
        if (abs(x) > 0.001) tft.drawFastVLine(sx, topY, 240 - topY, GRID_COLOR);
        tft.drawLine(sx, constrain(ay - 3, topY, 240), sx, constrain(ay + 3, topY, 240), AXIS_COLOR);
        if (abs(x) > 0.001) {
          String lbl = formatTick(x, stepX, xAxisPi);
          tft.setCursor(sx - (lbl.length() * 3), constrain(ay + 6, topY + 5, 225));
          tft.print(lbl);
        }
      }
    }
    double stepY = yAxisPi ? getPiStep(40.0 / zoom) : getNiceStep(40.0 / zoom);
    for (double y = ceil((centerWorldY - 120.0 / zoom) / stepY) * stepY; y <= centerWorldY + 120.0 / zoom; y += stepY) {
      int sy = worldYToScreen(y);
      if (sy >= topY && sy <= 240) {
        if (abs(y) > 0.001) tft.drawFastHLine(0, sy, 320, GRID_COLOR);
        tft.drawLine(constrain(ax - 3, 0, 320), sy, constrain(ax + 3, 0, 320), sy, AXIS_COLOR);
        if (abs(y) > 0.001) {
          String lbl = formatTick(y, stepY, yAxisPi);
          tft.setCursor(constrain(ax + 6, 5, 290), sy - 3);
          tft.print(lbl);
        }
      }
    }
  }

  if (ax >= 0 && ax <= 320) tft.drawLine(ax, topY, ax, 240, AXIS_COLOR);
  if (ay >= topY && ay <= 240) tft.drawLine(0, ay, 320, ay, AXIS_COLOR);
  old_ax = ax;
  old_ay = ay;

  for (int i = 0; i < NUM_FUNCS; i++) {
    if (!funcs[i].visible || !funcs[i].exprX) {
      for (int j = 0; j <= 320; j++) prev_y[i][j] = -1000;
      continue;
    }

    if (funcs[i].type == EQ_EXPLICIT) {
      bool prevValid = false;
      int prevSY = 0;
      for (int sx = 0; sx <= 320; sx++) {
        math_x = screenXToWorld(sx);
        double my = te_eval(funcs[i].exprX);
        int sy = (!isnan(my) && !isinf(my)) ? worldYToScreen(my) : -1000;
        prev_y[i][sx] = sy;
        if (prevValid && sy != -1000 && abs(sy - prevSY) < 80) {
          int y1 = constrain(prevSY, topY, 240), y2 = constrain(sy, topY, 240);
          if (!((prevSY < topY && sy < topY) || (prevSY > 240 && sy > 240))) {
            tft.drawLine(sx - 1, y1, sx, y2, funcs[i].color);
            tft.drawLine(sx - 1, y1 + 1, sx, y2 + 1, funcs[i].color);
          }
        }
        prevSY = sy;
        prevValid = (sy != -1000);
        if ((sx & 63) == 0) yield();
      }
    } else if (funcs[i].type == EQ_PARAMETRIC && funcs[i].exprY) {
      int prev_sx = -1000, prev_sy = -1000;
      int sample = 0;
      for (math_t = -PI * 2; math_t <= PI * 2; math_t += 0.02) {
        double px = te_eval(funcs[i].exprX);
        double py = te_eval(funcs[i].exprY);
        if (isnan(px) || isnan(py) || isinf(px) || isinf(py)) continue;
        int sx = worldXToScreen(px), sy = worldYToScreen(py);
        if (prev_sx != -1000 && prev_sy != -1000) tft.drawLine(prev_sx, prev_sy, sx, sy, funcs[i].color);
        prev_sx = sx;
        prev_sy = sy;
        if ((sample++ & 63) == 0) yield();
      }
    } else if (funcs[i].type == EQ_POINT && funcs[i].exprY) {
      double px = te_eval(funcs[i].exprX);
      double py = te_eval(funcs[i].exprY);
      int sx = worldXToScreen(px), sy = worldYToScreen(py);
      if (sx >= 0 && sx <= 320 && sy >= topY && sy <= 240) {
        tft.fillCircle(sx, sy, 4, TEXT_COLOR);
        tft.fillCircle(sx, sy, 2, funcs[i].color);
      }
    } else if (funcs[i].type == EQ_IMPLICIT) {
      int step = (isPanning || isTracing) ? 6 : 3;
      int cols = 320 / step + 1;
      double prev_row[110];
      double curr_row[110];

      // Implicit plots use writePixel heavily, so keep one SPI transaction
      // for the marching-squares pass.
      tft.startWrite();
      math_y = screenYToWorld(topY);
      for (int c = 0; c < cols; c++) {
        math_x = screenXToWorld(c * step);
        prev_row[c] = te_eval(funcs[i].exprX);
      }

      for (int sy = topY + step; sy <= 240; sy += step) {
        math_y = screenYToWorld(sy);

        math_x = screenXToWorld(0);
        curr_row[0] = te_eval(funcs[i].exprX);

        for (int c = 0; c < cols - 1; c++) {
          int sx = c * step;
          math_x = screenXToWorld(sx + step);
          curr_row[c + 1] = te_eval(funcs[i].exprX);

          double v_cur = prev_row[c];
          double v_right = prev_row[c + 1];
          double v_down = curr_row[c];

          if ((v_cur > 0) != (v_right > 0)) {
            tft.writePixel(sx + step / 2, sy - step, funcs[i].color);
            tft.writePixel(sx + step / 2, sy - step + 1, funcs[i].color);
          }
          if ((v_cur > 0) != (v_down > 0)) {
            tft.writePixel(sx, sy - step / 2, funcs[i].color);
            tft.writePixel(sx + 1, sy - step / 2, funcs[i].color);
          }
        }
        for (int c = 0; c < cols; c++) prev_row[c] = curr_row[c];
      }
      tft.endWrite();
    }
  }

  for (int i = 0; i < NUM_FUNCS; i++)
    if (funcs[i].visible && funcs[i].exprX) drawHoles(i, topY);
  drawPoints(topY);

  if (traceActive && traceSlot >= 0 && traceSlot < NUM_FUNCS && funcs[traceSlot].exprX) {
    int tsx = worldXToScreen(traceWorldX), tsy = worldYToScreen(traceWorldY);
    if (!isnan(traceWorldY) && !isinf(traceWorldY) && tsy >= topY + 5 && tsy <= 235) {
      tft.fillCircle(tsx, tsy, 5, TEXT_COLOR);
      tft.fillCircle(tsx, tsy, 3, funcs[traceSlot].color);
      String label = "x:" + niceNum(traceWorldX) + " y:" + niceNum(traceWorldY);
      int boxW = constrain(12 + label.length() * 6, 70, 310);
      old_boxW = boxW;
      tft.fillRoundRect(5, topY + 5, boxW, 18, RADIUS_SM, BTN_COLOR);
      tft.drawRoundRect(5, topY + 5, boxW, 18, RADIUS_SM, funcs[traceSlot].color);
      tft.setTextColor(TEXT_COLOR);
      tft.setTextSize(1);
      tft.setCursor(9, topY + 10);
      tft.print(label);
      old_tsx = tsx;
      old_tsy = tsy;
    } else old_tsx = -1;
  } else old_tsx = -1;

  if (fullWipe) drawTopBar();

  if (varPanelOpen) {
    tft.fillRect(10, 160, 300, 70, SURFACE_COLOR);
    tft.drawRoundRect(10, 160, 300, 70, RADIUS_MD, BTN_OUTLINE);
    drawModernButton(275, 165, 30, 25, 4, DEL_COLOR, false);
    printCentered("X", 290, 182, &FreeSansBold9pt7b, TEXT_COLOR);
    if (activeVarIdx >= 0 && activeVarIdx < NUM_CUSTOM_VARS) {
      drawModernButton(20, 165, 30, 25, 4, SURFACE_HI, false);
      printCentered("<", 35, 182, &FreeSansBold9pt7b, TEXT_COLOR);
      drawModernButton(235, 165, 30, 25, 4, SURFACE_HI, false);
      printCentered(">", 250, 182, &FreeSansBold9pt7b, TEXT_COLOR);
      printCentered(sliders[activeVarIdx].name + " = " + niceNum(sliders[activeVarIdx].value), 140, 182, &FreeSansBold9pt7b, ACCENT_COLOR);
      uint16_t playBg = varAnimating ? ACCENT_COLOR : SURFACE_HI;
      uint16_t playFg = varAnimating ? BG_COLOR : TEXT_COLOR;
      drawModernButton(SLIDER_PLAY_X, SLIDER_PLAY_Y, SLIDER_PLAY_W, SLIDER_PLAY_H, 4, playBg, false);
      int playCx = SLIDER_PLAY_X + SLIDER_PLAY_W / 2;
      int playCy = SLIDER_PLAY_Y + SLIDER_PLAY_H / 2;
      if (varAnimating) drawPauseIcon(playCx, playCy, playFg);
      else drawPlayIcon(playCx, playCy, playFg);
      tft.fillRoundRect(SLIDER_TRACK_X, SLIDER_TRACK_Y, SLIDER_TRACK_W, SLIDER_TRACK_H, 3, BG_COLOR);
      float pct = (sliders[activeVarIdx].value - sliders[activeVarIdx].min_val) / (sliders[activeVarIdx].max_val - sliders[activeVarIdx].min_val);
      pct = constrain(pct, 0.0f, 1.0f);
      int kx = SLIDER_TRACK_X + (int)(pct * SLIDER_TRACK_W);
      int ky = SLIDER_TRACK_Y + SLIDER_TRACK_H / 2;
      tft.fillCircle(kx, ky, 8, TEXT_COLOR);
      tft.fillCircle(kx, ky, 4, varAnimating ? ACCENT_COLOR : PLOT_COLOR);
      drawModernButton(SLIDER_SPEED_X, SLIDER_SPEED_Y, SLIDER_SPEED_W, SLIDER_SPEED_H, 4, SURFACE_HI, false);
      printCentered(ANIM_SPEED_LABELS[animSpeedIdx], SLIDER_SPEED_X + SLIDER_SPEED_W / 2, SLIDER_SPEED_Y + 17, &FreeSansBold9pt7b, TEXT_COLOR);
    } else printCentered("No variables active", 160, 195, &FreeSans9pt7b, MUTED_COLOR);
  } else {
    drawBottomControls();
  }
}

void drawTopBar() {
  if (tabsVisible) {
    drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
    drawBackChevron(20, 15, TEXT_COLOR);
    drawFunctionTabs();
  } else {
    drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
    drawBackChevron(20, 15, TEXT_COLOR);
  }
  drawModernButton(280, 0, 40, 30, 0, SURFACE_HI, false);
  drawChevron(300, 15, tabsVisible, TEXT_COLOR);
  if (tabsVisible) tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);
}

void drawFunctionTabs() {
  // Six narrow expression tabs fit between the back and collapse buttons.
  // Like Desmos, the colored dot is the visibility switch and the rest of
  // the row opens that expression for editing.
  const int tabW = 240 / NUM_FUNCS;
  for (int i = 0; i < NUM_FUNCS; i++) {
    int x = 40 + (i * tabW);
    uint16_t tabBg = funcs[i].visible ? SURFACE_HI : BG_COLOR;
    tft.fillRect(x, 0, tabW, 30, tabBg);
    tft.drawFastVLine(x, 0, 30, BTN_OUTLINE);
    if (i == activeSlot) tft.drawRect(x + 1, 1, tabW - 2, 28, funcs[i].color);
    if (funcs[i].visible) tft.fillCircle(x + 7, 15, 4, funcs[i].color);
    else tft.drawCircle(x + 7, 15, 4, MUTED_COLOR);
    if (funcs[i].input.length() > 0 && funcs[i].exprX == nullptr) {
      tft.setTextColor(DEL_COLOR);
      tft.setTextSize(1);
      tft.setCursor(x + tabW - 8, 2);
      tft.print("!");
    }
    int maxChars = max(2, (tabW - 14) / 6);
    String truncEq = funcs[i].input;
    if (truncEq.length() > maxChars) truncEq = truncEq.substring(0, maxChars - 1) + ".";
    if (truncEq.length() == 0) truncEq = "+";
    printPrettyEquation(x + 13, 11, truncEq, -1, funcs[i].visible ? TEXT_COLOR : MUTED_COLOR, 1);
  }
}

void drawBottomControls() {
  drawModernButton(10, 195, 50, 40, RADIUS_MD, SURFACE_COLOR, true);
  printCentered("CLR", 35, 220, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(65, 195, 55, 40, RADIUS_MD, SURFACE_COLOR, true);
  printCentered("PNT", 92, 220, &FreeSans9pt7b, TEXT_COLOR);
  bool hasVars = false;
  for (int v = 0; v < NUM_CUSTOM_VARS; v++)
    if (sliders[v].in_use) {
      hasVars = true;
      break;
    }
  drawModernButton(125, 195, 50, 40, RADIUS_MD, hasVars ? ACCENT_COLOR : SURFACE_COLOR, true);
  printCentered("VAR", 150, 220, &FreeSans9pt7b, hasVars ? BG_COLOR : TEXT_COLOR);
  drawModernButton(220, 195, 40, 40, RADIUS_MD, SURFACE_COLOR, true);
  drawPlusIcon(240, 215, TEXT_COLOR);
  drawModernButton(270, 195, 40, 40, RADIUS_MD, SURFACE_COLOR, true);
  drawMinusIcon(290, 215, TEXT_COLOR);
}

void handleGraphTouch(bool touched, int sx, int sy) {
  int topY = tabsVisible ? 31 : 0;
  if (touched) {
    if (!touchActive) {
      touchActive = true;
      touchStartX = sx;
      touchStartY = sy;
      lastTouchX = sx;
      lastTouchY = sy;
      isPanning = false;
      isTracing = false;
      if (varPanelOpen) {
        if (inRect(sx, sy, 275, 165, 30, 25)) {
          flashButton(275, 165, 30, 25, 4);
          stopVarAnimation();
          saveVariables();
          varPanelOpen = false;
          drawGraphScreen(true);
          touchActive = false;
          return;
        }
        if (activeVarIdx >= 0) {
          if (inRect(sx, sy, 20, 165, 30, 25)) {
            flashButton(20, 165, 30, 25, 4);
            stopVarAnimation();
            // Bounded scan: never hang if every variable went out of use.
            for (int n = 0; n < NUM_CUSTOM_VARS; n++) {
              activeVarIdx = (activeVarIdx - 1 + NUM_CUSTOM_VARS) % NUM_CUSTOM_VARS;
              if (sliders[activeVarIdx].in_use) break;
            }
            drawGraphScreen(true);
            touchActive = false;
            return;
          }
          if (inRect(sx, sy, 235, 165, 30, 25)) {
            flashButton(235, 165, 30, 25, 4);
            stopVarAnimation();
            for (int n = 0; n < NUM_CUSTOM_VARS; n++) {
              activeVarIdx = (activeVarIdx + 1) % NUM_CUSTOM_VARS;
              if (sliders[activeVarIdx].in_use) break;
            }
            drawGraphScreen(true);
            touchActive = false;
            return;
          }
          if (inRect(sx, sy, SLIDER_PLAY_X, SLIDER_PLAY_Y, SLIDER_PLAY_W, SLIDER_PLAY_H)) {
            flashButton(SLIDER_PLAY_X, SLIDER_PLAY_Y, SLIDER_PLAY_W, SLIDER_PLAY_H, 4);
            toggleVarAnimation();
            while (ts.touched()) delay(10);
            touchActive = false;
            return;
          }
          if (inRect(sx, sy, SLIDER_SPEED_X, SLIDER_SPEED_Y, SLIDER_SPEED_W, SLIDER_SPEED_H)) {
            flashButton(SLIDER_SPEED_X, SLIDER_SPEED_Y, SLIDER_SPEED_W, SLIDER_SPEED_H, 4);
            cycleAnimSpeed();
            while (ts.touched()) delay(10);
            touchActive = false;
            return;
          }
        }
        return;
      }
      if (inRect(sx, sy, 0, 0, 40, 30)) {
        flashButton(0, 0, 40, 30, 0);
        currentState = STATE_HOME;
        drawHomeScreen();
        while (ts.touched()) delay(10);
        touchActive = false;
        return;
      } else if (inRect(sx, sy, 280, 0, 40, 30)) {
        flashButton(280, 0, 40, 30, 0);
        tabsVisible = !tabsVisible;
        drawGraphScreen(true);
        delay(250);
        touchActive = false;
        return;
      } else if (tabsVisible && sy < 30 && sx >= 40 && sx < 280) {
        handleTabTouch(sx, sy);
        touchActive = false;
        delay(200);
        return;
      } else if (inRect(sx, sy, 10, 195, 50, 40)) {
        // Reset the viewport without disturbing the expression list.
        flashButton(10, 195, 50, 40, RADIUS_MD);
        centerWorldX = 0;
        centerWorldY = 0;
        zoom = 15.0;
        traceActive = false;
        drawGraphScreen(true);
        touchActive = false;
        return;
      } else if (inRect(sx, sy, 65, 195, 55, 40)) {
        flashButton(65, 195, 55, 40, RADIUS_MD);
        pointInput = "";
        pointCursor = 0;
        currentState = STATE_POINT_KBD;
        drawPointKeyboardScreen();
        while (ts.touched()) delay(10);
        touchActive = false;
        return;
      } else if (inRect(sx, sy, 125, 195, 50, 40)) {
        flashButton(125, 195, 50, 40, RADIUS_MD);
        bool hasVars = false;
        for (int v = 0; v < NUM_CUSTOM_VARS; v++)
          if (sliders[v].in_use) {
            hasVars = true;
            if (activeVarIdx == -1) activeVarIdx = v;
          }
        if (hasVars) {
          varPanelOpen = true;
          drawGraphScreen(true);
        } else showToast("Use letters like m,c,t in eq");
        touchActive = false;
        return;
      } else if (inRect(sx, sy, 220, 195, 40, 40)) {
        flashButton(220, 195, 40, 40, RADIUS_MD);
        holdZoom(1.08, 220, 195, 40, 40);
        touchActive = false;
        return;
      } else if (inRect(sx, sy, 270, 195, 40, 40)) {
        flashButton(270, 195, 40, 40, RADIUS_MD);
        holdZoom(1.0 / 1.08, 270, 195, 40, 40);
        touchActive = false;
        return;
      }

      if (sy >= topY && sy <= 195) {
        double wX = screenXToWorld(sx);
        double bestDist = 1e18;
        int bestIdx = -1;
        double bestY = 0;
        for (int i = 0; i < NUM_FUNCS; i++) {
          if (!funcs[i].visible || !funcs[i].exprX || funcs[i].type != EQ_EXPLICIT) continue;
          math_x = wX;
          double y = te_eval(funcs[i].exprX);
          if (isnan(y) || isinf(y)) continue;
          double d = fabs(worldYToScreen(y) - sy);
          if (d < 25) {
            bestDist = d;
            bestIdx = i;
            bestY = y;
          }
        }
        if (bestIdx != -1) {
          isTracing = true;
          traceActive = true;
          traceSlot = bestIdx;
          traceWorldX = wX;
          traceWorldY = bestY;
          drawGraphScreen(false);
          return;
        }
      }
    } else {
      if (varPanelOpen && activeVarIdx >= 0 && activeVarIdx < NUM_CUSTOM_VARS && sy > 195 && sy < 225 && sx > 50 && sx < 240) {
        // A manual drag wins over playback so the knob never fights the finger.
        stopVarAnimation();
        float pct = constrain((float)(sx - SLIDER_TRACK_X) / SLIDER_TRACK_W, 0.0, 1.0);
        sliders[activeVarIdx].value = sliders[activeVarIdx].min_val + (pct * (sliders[activeVarIdx].max_val - sliders[activeVarIdx].min_val));
        if (millis() - lastPanTime > 40) {
          needsFullWipe = true;
          drawGraphScreen(false);
          lastPanTime = millis();
        }
      } else if (isTracing) {
        traceWorldX = screenXToWorld(sx);
        math_x = traceWorldX;
        traceWorldY = te_eval(funcs[traceSlot].exprX);
        if (millis() - lastPanTime > 20) {
          drawGraphScreen(false);
          lastPanTime = millis();
        }
      } else if (!varPanelOpen) {
        int dx = sx - lastTouchX, dy = sy - lastTouchY;
        if (!isPanning && (abs(sx - touchStartX) > PAN_THRESHOLD || abs(sy - touchStartY) > PAN_THRESHOLD)) isPanning = true;
        if (isPanning) {
          centerWorldX -= dx / zoom;
          centerWorldY += dy / zoom;
          traceActive = false;
          lastTouchX = sx;
          lastTouchY = sy;
          needsFullWipe = true;
          if (millis() - lastPanTime > 20) {
            drawGraphScreen(false);
            lastPanTime = millis();
          }
        }
      }
    }
  } else if (touchActive) {
    drawGraphScreen(true);
    touchActive = false;
    isPanning = false;
    isTracing = false;
  }
}

void handleTabTouch(int sx, int sy) {
  const int tabW = 240 / NUM_FUNCS;
  int idx = (sx - 40) / tabW;
  if (idx < 0 || idx >= NUM_FUNCS) return;
  if (sx - 40 - (idx * tabW) < 13 && sy < 22) {
    funcs[idx].visible = !funcs[idx].visible;
    saveFunctions();
    drawGraphScreen(true);
  } else {
    activeSlot = idx;
    cursor_idx = funcs[activeSlot].input.length();
    currentState = STATE_MAIN_KBD;
    tft.fillScreen(BG_COLOR);
    drawKeyboardScreen(main_keys);
  }
}

void holdZoom(double factor, int rx, int ry, int rw, int rh) {
  while (ts.touched()) {
    TS_Point p = ts.getPoint();
    int hw_x = touch_swap_xy ? p.y : p.x, hw_y = touch_swap_xy ? p.x : p.y;
    int px = constrain(map(hw_x, touch_x_min, touch_x_max, 0, 320), 0, 320), py = constrain(map(hw_y, touch_y_min, touch_y_max, 0, 240), 0, 240);
    if (!inRect(px, py, rx, ry, rw, rh)) break;
    // Zoom around the centre of the plot, not the +/- button itself.
    // zoomAt preserves the world coordinate under that centre.
    zoomAt(factor, 160, 120);
    traceActive = false;
    needsFullWipe = true;
    drawGraphScreen(false);
    delay(20);
  }
  drawGraphScreen(true);
}
