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
      }
    } else if (funcs[i].type == EQ_PARAMETRIC && funcs[i].exprY) {
      int prev_sx = -1000, prev_sy = -1000;
      for (math_t = -PI * 2; math_t <= PI * 2; math_t += 0.02) {
        double px = te_eval(funcs[i].exprX);
        double py = te_eval(funcs[i].exprY);
        if (isnan(px) || isnan(py)) continue;
        int sx = worldXToScreen(px), sy = worldYToScreen(py);
        if (prev_sx != -1000 && prev_sy != -1000) tft.drawLine(prev_sx, prev_sy, sx, sy, funcs[i].color);
        prev_sx = sx;
        prev_sy = sy;
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

      math_y = screenYToWorld(topY);
      for (int c = 0; c < cols; c++) {
        math_x = screenXToWorld(c * step);
        prev_row[c] = te_eval(funcs[i].exprX);
      }

      tft.startWrite();
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

  if (traceActive && traceSlot >= 0 && funcs[traceSlot].exprX) {
    int tsx = worldXToScreen(traceWorldX), tsy = worldYToScreen(traceWorldY);
    if (tsy >= topY + 5 && tsy <= 235) {
      tft.fillCircle(tsx, tsy, 5, TEXT_COLOR);
      tft.fillCircle(tsx, tsy, 3, funcs[traceSlot].color);
      String label = "x:" + String(traceWorldX, 2) + " y:" + String(traceWorldY, 2);
      int boxW = 12 + label.length() * 6;
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
      tft.fillRoundRect(40, 205, 240, 6, 3, BG_COLOR);
      float pct = (sliders[activeVarIdx].value - sliders[activeVarIdx].min_val) / (sliders[activeVarIdx].max_val - sliders[activeVarIdx].min_val);
      int kx = 40 + (int)(pct * 240);
      tft.fillCircle(kx, 208, 8, TEXT_COLOR);
      tft.fillCircle(kx, 208, 4, PLOT_COLOR);
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
  for (int i = 0; i < NUM_FUNCS; i++) {
    int x = 40 + (i * 60);
    uint16_t tabBg = funcs[i].visible ? SURFACE_HI : BG_COLOR;
    tft.fillRect(x, 0, 60, 30, tabBg);
    tft.drawFastVLine(x, 0, 30, BTN_OUTLINE);
    if (funcs[i].visible) tft.fillCircle(x + 8, 15, 4, funcs[i].color);
    else tft.drawCircle(x + 8, 15, 4, MUTED_COLOR);
    if (funcs[i].input.length() > 0 && funcs[i].exprX == nullptr) {
      tft.setTextColor(DEL_COLOR);
      tft.setTextSize(1);
      tft.setCursor(x + 50, 2);
      tft.print("!");
    }
    String truncEq = funcs[i].input.length() <= 5 ? funcs[i].input : funcs[i].input.substring(0, 4) + ".";
    printPrettyEquation(x + 14, 11, funcs[i].input.length() == 0 ? "tap" : truncEq, -1, funcs[i].visible ? TEXT_COLOR : MUTED_COLOR, 1);
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
          varPanelOpen = false;
          drawGraphScreen(true);
          touchActive = false;
          return;
        }
        if (activeVarIdx >= 0) {
          if (inRect(sx, sy, 20, 165, 30, 25)) {
            flashButton(20, 165, 30, 25, 4);
            do { activeVarIdx = (activeVarIdx - 1 + NUM_CUSTOM_VARS) % NUM_CUSTOM_VARS; } while (!sliders[activeVarIdx].in_use);
            drawGraphScreen(true);
            touchActive = false;
            return;
          }
          if (inRect(sx, sy, 235, 165, 30, 25)) {
            flashButton(235, 165, 30, 25, 4);
            do { activeVarIdx = (activeVarIdx + 1) % NUM_CUSTOM_VARS; } while (!sliders[activeVarIdx].in_use);
            drawGraphScreen(true);
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
      } else if (tabsVisible && sy < 30 && sx > 40 && sx < 240) {
        handleTabTouch(sx, sy);
        touchActive = false;
        delay(200);
        return;
      } else if (inRect(sx, sy, 10, 195, 50, 40)) {
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
      if (varPanelOpen && sy > 195 && sy < 225 && sx > 30 && sx < 290) {
        float pct = constrain((float)(sx - 40) / 240.0, 0.0, 1.0);
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
  int idx = (sx - 40) / 60;
  if (idx < 0 || idx >= NUM_FUNCS) return;
  if (sx - 40 - (idx * 60) < 15 && sy < 20) {
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
    zoom *= factor;
    if (zoom < 0.5) zoom = 0.5;
    if (zoom > 4000) zoom = 4000;
    traceActive = false;
    needsFullWipe = true;
    drawGraphScreen(false);
    delay(20);
  }
  drawGraphScreen(true);
}
