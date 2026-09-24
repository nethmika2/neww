#include "TextInput.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "KeyboardApp.h"
#include "PomodoroApp.h"
#include "PomodoroStore.h"
#include "TouchDriver.h"

// ==========================================
// GENERIC TEXT KEYBOARD (task & routine names)
// ==========================================
// Layout: title bar, the name being typed, then five rows of keys.
static const int TXT_BAR_H = 34;    // title strip
static const int TXT_BOX_Y = 34;    // text box top
static const int TXT_ROW_Y = 78;    // first key row
static const int TXT_ROW_H = 32;
static const int TXT_COL_W = 53;
static const int TXT_KEY_H = 28;

static const char* textKeyAt(int row, int col) {
  return textKbNumeric ? text_num_keys[row][col] : text_alpha_keys[row][col];
}

static void drawTextKey(int row, int col);

// Repaints one key after it was pressed.  flashButton() leaves the key in the
// pressed colour, so without this the label of the key you just tapped stays
// hidden until the whole board is redrawn.
static void redrawTextKey(int row, int col) {
  drawTextKey(row, col);
}

static void drawTextKey(int row, int col) {
  String label = String(textKeyAt(row, col));
  int x = (col * TXT_COL_W) + 2, y = TXT_ROW_Y + (row * TXT_ROW_H) + 2;
  uint16_t bg = SURFACE_COLOR;
  uint16_t fg = TEXT_COLOR;
  if (label == "DEL") {
    bg = SURFACE_HI;
    fg = DEL_COLOR;
  } else if (label == "OK") {
    bg = PLOT_COLOR;
    fg = BG_COLOR;
  } else if (label == "abc" || label == "123") {
    bg = SURFACE_HI;
    fg = ACCENT_COLOR;
  } else if (label == "SP") {
    bg = SURFACE_HI;
    fg = MUTED_COLOR;
  }
  drawModernButton(x, y, 49, TXT_KEY_H, RADIUS_SM, bg, false);
  tft.setTextColor(fg);
  // Labels are centred in the 49 px cap (size 1 = 6 px per cell, size 2 = 12).
  if (label == "SP") {
    tft.setTextSize(1);
    tft.setCursor(x + (49 - 30) / 2, y + 11);
    tft.print("SPACE");
    return;
  }
  if (label.length() > 1) {
    tft.setTextSize(1);
    tft.setCursor(x + max(3, (49 - (int)label.length() * 6) / 2), y + 11);
  } else {
    tft.setTextSize(2);
    tft.setCursor(x + (49 - 12) / 2, y + 6);
  }
  tft.print(label);
}

static void updateTextInputBox() {
  tft.fillRect(0, TXT_BOX_Y, 320, TXT_ROW_Y - TXT_BOX_Y, BG_COLOR);
  tft.drawFastHLine(0, TXT_BOX_Y, 320, BTN_OUTLINE);
  if (textInputBuf.length() == 0) {
    tft.setTextSize(1);
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(18, TXT_BOX_Y + 14);
    tft.print(textInputTarget == TEXT_TARGET_TASK ? "e.g. Write report" : "e.g. Deep work");
  }
  // Keep the caret on screen for long names instead of running off the edge.
  const int maxChars = 24;
  int start = max(0, textInputCursor - (maxChars - 3));
  if (start > (int)textInputBuf.length()) start = textInputBuf.length();
  String shown = textInputBuf.substring(start, start + maxChars);
  int shownCursor = constrain(textInputCursor - start, 0, (int)shown.length());
  printPrettyEquation(10, TXT_BOX_Y + 12, shown, shownCursor, TEXT_COLOR, 2);
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(276, TXT_BOX_Y + 16);
  tft.print(String(textInputBuf.length()) + "/" + String(textInputMax));
}

void drawTextKeyboardScreen(bool fullWipe) {
  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    tft.fillRect(0, 0, 320, TXT_BAR_H, SURFACE_COLOR);
    tft.drawFastHLine(0, TXT_BAR_H - 1, 320, BTN_OUTLINE);
    drawModernButton(6, 5, 34, TXT_BAR_H - 10, RADIUS_SM, SURFACE_HI, false);
    drawBackChevron(23, 17, TEXT_COLOR);
    printCentered(textInputTitle, 148, 22, &FreeSansBold9pt7b, TEXT_COLOR);
    drawModernButton(232, 5, 46, TXT_BAR_H - 10, RADIUS_SM, SURFACE_HI, false);
    printCentered(textKbNumeric ? "ABC" : "123", 255, 21, &FreeSans9pt7b, TEXT_COLOR);
    drawModernButton(282, 5, 36, TXT_BAR_H - 10, RADIUS_SM, PLOT_COLOR, false);
    printCentered("OK", 300, 21, &FreeSans9pt7b, BG_COLOR);
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 6; c++) drawTextKey(r, c);
  }
  updateTextInputBox();
}

void startTextInput(const String& title, const String& initial, TextTarget target, int maxLen) {
  textInputTitle = title;
  textInputBuf = initial;
  textInputMax = maxLen;
  textInputCursor = initial.length();
  textInputTarget = target;
  textKbNumeric = false;
  currentState = STATE_TEXT_KBD;
  drawTextKeyboardScreen(true);
}

void cancelTextInput() {
  textInputTarget = TEXT_TARGET_NONE;
  currentState = STATE_POMODORO;
  drawPomodoroScreen(true);
}

static bool commitTextInput() {
  String value = textInputBuf;
  value.trim();
  bool ok = true;
  if (value.length() == 0) {
    showToast("Type a name first");
    ok = false;
  } else if (textInputTarget == TEXT_TARGET_TASK) {
    if (addPomoTask(value) < 0) {
      showToast("To-do list is full");
      ok = false;
    }
  } else if (textInputTarget == TEXT_TARGET_TEMPLATE) {
    if (addPomoTemplate(value) < 0) {
      showToast("Only " + String(MAX_POMO_TEMPLATES) + " routines kept");
      ok = false;
    }
  }
  if (!ok) {
    drawTextKeyboardScreen(true);
    return false;
  }
  textInputTarget = TEXT_TARGET_NONE;
  currentState = STATE_POMODORO;
  drawPomodoroScreen(true);
  return true;
}

void handleTextKeyboardTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (inRect(sx, sy, 0, 0, 40, TXT_BAR_H)) {  // back = cancel
    flashButton(6, 5, 34, TXT_BAR_H - 10, RADIUS_SM);
    cancelTextInput();
    waitTouchRelease();
    return;
  }
  if (inRect(sx, sy, 232, 3, 46, TXT_BAR_H - 6)) {
    flashButton(232, 5, 46, TXT_BAR_H - 10, RADIUS_SM);
    textKbNumeric = !textKbNumeric;
    drawTextKeyboardScreen(true);
    waitTouchRelease();
    return;
  }
  if (inRect(sx, sy, 282, 3, 36, TXT_BAR_H - 6)) {
    flashButton(282, 5, 36, TXT_BAR_H - 10, RADIUS_SM);
    commitTextInput();
    waitTouchRelease();
    return;
  }
  if (sy < TXT_ROW_Y) return;
  int col = sx / TXT_COL_W, row = (sy - TXT_ROW_Y) / TXT_ROW_H;
  if (col < 0 || col >= 6 || row < 0 || row >= 5) return;
  String key = String(textKeyAt(row, col));
  int kx = (col * TXT_COL_W) + 2, ky = TXT_ROW_Y + (row * TXT_ROW_H) + 2;
  flashButton(kx, ky, 49, TXT_KEY_H, RADIUS_SM);

  if (key == "DEL") {
    if (textInputCursor > 0) {
      textInputBuf.remove(textInputCursor - 1, 1);
      textInputCursor--;
    }
  } else if (key == "<-") {
    if (textInputCursor > 0) textInputCursor--;
  } else if (key == "->") {
    if (textInputCursor < (int)textInputBuf.length()) textInputCursor++;
  } else if (key == "abc") {
    textKbNumeric = false;
  } else if (key == "123") {
    textKbNumeric = true;
  } else if (key == "OK") {
    if (commitTextInput()) {
      waitTouchRelease();
      return;
    }
  } else if ((int)textInputBuf.length() < textInputMax) {
    String inserted = (key == "SP") ? String(" ") : key;
    textInputBuf = textInputBuf.substring(0, textInputCursor) + inserted + textInputBuf.substring(textInputCursor);
    textInputCursor += inserted.length();
  }
  if (key == "abc" || key == "123") {
    drawTextKeyboardScreen(true);
  } else {
    updateTextInputBox();
    // Put the key (and, after a mode change from the top bar, the whole board)
    // back the way it was so no key is left in the pressed colour.
    redrawTextKey(row, col);
  }
  waitTouchRelease();
}
