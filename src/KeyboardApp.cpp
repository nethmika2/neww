#include "KeyboardApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "MathEngine.h"
#include "Storage.h"
#include "TouchDriver.h"

// Forward declaration from GraphApp
void drawGraphScreen(bool fullWipe);

void drawSingleKey(int r, int c, const char* keys[5][6]) {
  int x = (c * 53) + 2, y = (40 + (r * 40)) + 2;
  String keyStr = String(keys[r][c]);
  if (keyStr == " ") return;
  // The same key cap rule as the text keyboard: quiet surfaces for input keys,
  // one accent for the primary action, red only for the destructive one.
  uint16_t btn_bg = SURFACE_COLOR;
  uint16_t keyColor = TEXT_COLOR;
  if (keyStr == "PLOT" || keyStr == "ADD") {
    btn_bg = PLOT_COLOR;
    keyColor = BG_COLOR;
  } else if (keyStr == "DEL" || keyStr == "AC") {
    btn_bg = SURFACE_HI;
    keyColor = DEL_COLOR;
  } else if (keyStr == "FUNC" || keyStr == "BACK" || keyStr == "UNDO" || keyStr == "CLR" || keyStr == "VAR") {
    btn_bg = SURFACE_HI;
    keyColor = ACCENT_COLOR;
  } else if (keyStr == "123" || keyStr == "ABC") {
    btn_bg = SURFACE_HI;
    keyColor = ACCENT_COLOR;
  }
  if (btn_bg == SURFACE_COLOR && isVarChar(keyStr[0]) && keyStr.length() == 1) keyColor = VAR_COLOR;
  drawModernButton(x, y, 49, 36, RADIUS_SM, btn_bg, false);
  uint16_t txtColor = keyColor;
  tft.setTextColor(txtColor);
  if (keyStr == "sqrt()") {
    int gx = x + 4, gy = y + 10;
    int w = drawRadical(gx, gy, 2, txtColor);
    tft.setTextSize(2);
    tft.setCursor(gx + w, gy);
    tft.print("()");
    tft.drawFastHLine(gx + w - 1, gy, 26, txtColor);
    tft.drawFastHLine(gx + w - 1, gy + 1, 26, txtColor);
    return;
  }
  // Centre the label in the 49 px cap: one character at text size 2 (12 px per
  // cell), longer labels at size 1 (6 px per cell).  Centring keeps the board
  // tidy and stops wide labels from running off the right hand keys.
  if (keyStr.length() >= 4) {
    tft.setTextSize(1);
    int w = keyStr.length() * 6;
    tft.setCursor(x + max(3, (49 - w) / 2), y + 14);
  } else {
    tft.setTextSize(2);
    int w = keyStr.length() * 12;
    tft.setCursor(x + max(2, (49 - w) / 2), y + 11);
  }
  tft.print(keyStr);
}

void drawKeyboardScreen(const char* keys[5][6]) {
  updateInputBox();
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 6; c++) drawSingleKey(r, c, keys);
}

void handleKeyboardTouch(bool touched, int sx, int sy) {
  if (!touched || sy <= 40) return;
  int col = sx / 53, row = (sy - 40) / 40;
  if (col < 0 || col >= 6 || row < 0 || row >= 5) return;
  const char*(*activeKeys)[6] = (currentState == STATE_MAIN_KBD) ? main_keys : ((currentState == STATE_FUNC_KBD) ? func_keys : var_keys);
  String key = String(activeKeys[row][col]);
  if (key == " ") return;
  String& eq = funcs[activeSlot].input;
  int kx = (col * 53) + 2, ky = (40 + (row * 40)) + 2;
  flashButton(kx, ky, 49, 36, RADIUS_SM);

  if (key == "PLOT") {
    compileSlot(activeSlot);
    funcs[activeSlot].visible = funcs[activeSlot].input.length() > 0;
    refreshActiveVariables();
    saveFunctions();
    currentState = STATE_GRAPH;
    tft.fillScreen(BG_COLOR);
    drawGraphScreen(true);
  } else if (key == "FUNC") {
    currentState = STATE_FUNC_KBD;
    tft.fillScreen(BG_COLOR);
    drawKeyboardScreen(func_keys);
  } else if (key == "VAR") {
    currentState = STATE_VAR_KBD;
    tft.fillScreen(BG_COLOR);
    drawKeyboardScreen(var_keys);
  } else if (key == "BACK") {
    currentState = STATE_MAIN_KBD;
    tft.fillScreen(BG_COLOR);
    drawKeyboardScreen(main_keys);
  } else if (key == "AC") {
    eq = "";
    cursor_idx = 0;
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  } else if (key == "DEL") {
    if (cursor_idx >= 4 && eq.substring(cursor_idx - 4, cursor_idx) == "sqrt") {
      eq.remove(cursor_idx - 4, 4);
      cursor_idx -= 4;
    } else if (cursor_idx > 0) {
      eq.remove(cursor_idx - 1, 1);
      cursor_idx--;
    }
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  } else if (key == "<-") {
    if (cursor_idx >= 4 && eq.substring(cursor_idx - 4, cursor_idx) == "sqrt") cursor_idx -= 4;
    else if (cursor_idx > 0) cursor_idx--;
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  } else if (key == "->") {
    if (eq.startsWith("sqrt", cursor_idx)) cursor_idx += 4;
    else if (cursor_idx < (int)eq.length()) cursor_idx++;
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  } else if (key == "x^2" || key == "^2") {
    eq = eq.substring(0, cursor_idx) + "^2" + eq.substring(cursor_idx);
    cursor_idx += 2;
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  } else if (key.endsWith("()")) {
    eq = eq.substring(0, cursor_idx) + key + eq.substring(cursor_idx);
    cursor_idx += (key.length() - 1);
    currentState = STATE_MAIN_KBD;
    tft.fillScreen(BG_COLOR);
    drawKeyboardScreen(main_keys);
  } else {
    eq = eq.substring(0, cursor_idx) + key + eq.substring(cursor_idx);
    cursor_idx += key.length();
    updateInputBox();
    drawSingleKey(row, col, activeKeys);
  }
  waitTouchRelease();
}

void printPrettyEquation(int start_x, int start_y, String eq, int cursor_pos, uint16_t color, int base_size) {
  int cur_x = start_x;
  bool in_power = false;
  int curTop = start_y - (base_size == 2 ? 2 : 0), curBot = start_y + (base_size == 2 ? 12 : 6);
  for (int i = 0; i <= (int)eq.length(); i++) {
    if (i == cursor_pos) {
      tft.drawLine(cur_x, curTop, cur_x, curBot, VAR_COLOR);
      cur_x += 2;
    }
    if (i == (int)eq.length()) break;
    char c = eq[i];
    if (c == '^') {
      in_power = true;
      continue;
    }
    if (in_power && (c == '+' || c == '-' || c == '*' || c == '/' || c == ')' || c == '=' || c == ',')) in_power = false;
    tft.setTextColor(color);
    int sz = in_power ? 1 : base_size;
    int cy = in_power ? start_y - (base_size == 2 ? 4 : 2) : start_y;
    if (eq.startsWith("sqrt", i)) {
      cur_x += drawRadical(cur_x, cy, sz, color);
      if (cursor_pos > i && cursor_pos < i + 4) {
        tft.drawLine(cur_x, curTop, cur_x, curBot, VAR_COLOR);
        cur_x += 2;
      }
      i += 3;
      continue;
    }
    tft.setTextSize(sz);
    tft.setCursor(cur_x, cy);
    tft.print(c);
    cur_x += (sz == 2 ? 12 : 6);
  }
}

void updateInputBox() {
  uint16_t borderColor = funcs[activeSlot].color;
  tft.fillRect(0, 0, 320, 40, BG_COLOR);
  tft.fillRect(0, 0, 320, 3, borderColor);
  tft.drawFastHLine(0, 39, 320, BTN_OUTLINE);
  tft.setCursor(10, 12);
  tft.setTextColor(MUTED_COLOR);
  tft.setTextSize(2);
  tft.print("E" + String(activeSlot + 1) + ":");

  // Keep the caret visible for long equations instead of drawing text past
  // the right edge of the 320 px display.
  const int maxChars = 21;
  int start = max(0, cursor_idx - (maxChars - 3));
  if (start > (int)funcs[activeSlot].input.length()) start = funcs[activeSlot].input.length();
  String shown = funcs[activeSlot].input.substring(start, start + maxChars);
  int shownCursor = constrain(cursor_idx - start, 0, (int)shown.length());
  printPrettyEquation(50, 12, shown, shownCursor, TEXT_COLOR, 2);
}

void drawPointKeyboardScreen() {
  tft.fillScreen(BG_COLOR);
  updatePointInputBox();
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 6; c++) drawSingleKey(r, c, pointKbAlpha ? point_alpha_keys : point_keys);
}

void updatePointInputBox() {
  tft.fillRect(0, 0, 320, 40, SURFACE_COLOR);
  tft.fillRect(0, 0, 320, 3, pointKbAlpha ? VAR_COLOR : POINT_COLOR);
  tft.drawFastHLine(0, 39, 320, BTN_OUTLINE);
  tft.setTextSize(2);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(8, 12);
  tft.print("(");
  const int maxChars = 17;
  int start = max(0, pointCursor - (maxChars - 3));
  if (start > (int)pointInput.length()) start = pointInput.length();
  String shown = pointInput.substring(start, start + maxChars);
  int shownCursor = constrain(pointCursor - start, 0, (int)shown.length());
  printPrettyEquation(22, 12, shown, shownCursor, TEXT_COLOR, 2);
  tft.setTextSize(2);
  tft.setTextColor(MUTED_COLOR);
  int closeX = min(22 + (int)shown.length() * 12 + 4, 224);
  tft.setCursor(closeX, 12);
  tft.print(")");

  // Right hand column: a live preview of where the dot will land once the
  // parameter letters have been resolved against the sliders.
  tft.setTextSize(1);
  String preview = "";
  uint16_t previewColor = MUTED_COLOR;
  String left, right;
  te_expr* cx = nullptr;
  te_expr* cy = nullptr;
  bool live = false;
  if (compilePointInput(pointInput, cx, cy, left, right, live)) {
    double vx = te_eval(cx), vy = te_eval(cy);
    te_free(cx);
    te_free(cy);
    if (!isnan(vx) && !isinf(vx) && !isnan(vy) && !isinf(vy)) {
      preview = "= (" + niceNum(vx) + ", " + niceNum(vy) + ")" + (live ? " live" : "");
      previewColor = live ? VAR_COLOR : ACCENT_COLOR;
    }
  }
  if (preview.length() == 0) preview = pointKbAlpha ? "letters: a b c k m n p q" : "Point  x , y";
  tft.setTextColor(previewColor);
  int pw = preview.length() * 6;
  tft.setCursor(constrain(316 - pw, 200, 316), 6);
  tft.print(preview);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(236, 22);
  tft.print(String(numPoints) + " / " + String(MAX_POINTS) + " saved");
}

void handlePointKeyboardTouch(bool touched, int sx, int sy) {
  if (!touched || sy <= 40) return;
  int col = sx / 53, row = (sy - 40) / 40;
  if (col < 0 || col >= 6 || row < 0 || row >= 5) return;
  String key = String(pointKbAlpha ? point_alpha_keys[row][col] : point_keys[row][col]);
  if (key == " ") return;
  String& s = pointInput;
  int kx = (col * 53) + 2, ky = (40 + (row * 40)) + 2;
  flashButton(kx, ky, 49, 36, RADIUS_SM);

  if (key == "ADD") {
    String left, right;
    te_expr* cx = nullptr;
    te_expr* cy = nullptr;
    bool live = false;
    if (!compilePointInput(s, cx, cy, left, right, live)) {
      if (cx) te_free(cx);
      if (cy) te_free(cy);
      showToast(pointKbAlpha ? "e.g. 2m,3  uses sliders" : "Format:  x , y   e.g. 1,3");
      drawPointKeyboardScreen();
    } else if (numPoints >= MAX_POINTS) {
      te_free(cx);
      te_free(cy);
      showToast("Max " + String(MAX_POINTS) + " points (use UNDO)");
      drawPointKeyboardScreen();
    } else {
      int idx = numPoints;
      points[idx].exprX = left;
      points[idx].exprY = right;
      points[idx].compX = cx;
      points[idx].compY = cy;
      points[idx].live = live;
      points[idx].x = te_eval(cx);
      points[idx].y = te_eval(cy);
      numPoints++;
      // A parameter point shares the grapher sliders, so it may bring new
      // variables into the VAR panel.
      refreshActiveVariables();
      savePoints();
      s = "";
      pointCursor = 0;
      pointKbAlpha = false;
      currentState = STATE_GRAPH;
      tft.fillScreen(BG_COLOR);
      drawGraphScreen(true);
    }
  } else if (key == "ABC") {
    pointKbAlpha = true;
    drawPointKeyboardScreen();
  } else if (key == "123") {
    pointKbAlpha = false;
    drawPointKeyboardScreen();
  } else if (key == "BACK") {
    pointKbAlpha = false;
    currentState = STATE_GRAPH;
    tft.fillScreen(BG_COLOR);
    drawGraphScreen(true);
  } else if (key == "AC") {
    s = "";
    pointCursor = 0;
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else if (key == "UNDO") {
    if (numPoints > 0) {
      freePointExprs(numPoints - 1);
      points[numPoints - 1] = PlotPoint();
      numPoints--;
    }
    // Removing a parameter point may release a variable from the VAR panel.
    refreshActiveVariables();
    savePoints();
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else if (key == "CLR") {
    for (int i = 0; i < numPoints; i++) {
      freePointExprs(i);
      points[i] = PlotPoint();
    }
    numPoints = 0;
    refreshActiveVariables();
    savePoints();
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else if (key == "DEL") {
    if (pointCursor > 0) {
      s.remove(pointCursor - 1, 1);
      pointCursor--;
    }
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else if (key == "<-") {
    if (pointCursor > 0) pointCursor--;
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else if (key == "->") {
    if (pointCursor < (int)s.length()) pointCursor++;
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  } else {
    if (s.length() < 24) {
      s = s.substring(0, pointCursor) + key + s.substring(pointCursor);
      pointCursor += key.endsWith("()") ? key.length() - 1 : key.length();
    }
    updatePointInputBox();
    drawSingleKey(row, col, pointKbAlpha ? point_alpha_keys : point_keys);
  }
  waitTouchRelease();
}
