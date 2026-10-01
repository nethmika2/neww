#include "DisplayUtils.h"
#include "Led.h"
#include "TimeService.h"
#include <string.h>

// Forward declarations for apps redrawn on wake
void drawHomeScreen();
void drawGraphScreen(bool fullWipe);
void drawKeyboardScreen(const char* keys[5][6]);
void drawPointKeyboardScreen();
void drawSettingsScreen();
void drawMusicScreen(bool fullWipe);
void drawMusicList();
void drawCalibrationScreen();
void drawPomodoroScreen(bool fullWipe);
void drawTextKeyboardScreen(bool fullWipe);

bool displayActive() {
  return screenOn && !screensaverActive;
}

bool inRect(int px, int py, int rx, int ry, int rw, int rh) {
  return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
}

void redrawCurrentScreen() {
  if (currentState == STATE_HOME) drawHomeScreen();
  else if (currentState == STATE_GRAPH) drawGraphScreen(true);
  else if (currentState == STATE_MAIN_KBD) drawKeyboardScreen(main_keys);
  else if (currentState == STATE_FUNC_KBD) drawKeyboardScreen(func_keys);
  else if (currentState == STATE_VAR_KBD) drawKeyboardScreen(var_keys);
  else if (currentState == STATE_POINT_KBD) drawPointKeyboardScreen();
  else if (currentState == STATE_SETTINGS) drawSettingsScreen();
  else if (currentState == STATE_MUSIC) drawMusicScreen(true);
  else if (currentState == STATE_MUSIC_LIST) drawMusicList();
  else if (currentState == STATE_CALIBRATE) drawCalibrationScreen();
  else if (currentState == STATE_POMODORO) drawPomodoroScreen(true);
  else if (currentState == STATE_TEXT_KBD) drawTextKeyboardScreen(true);
}

// The backlight PWM itself lives in Led.cpp, together with the RGB LED's; this
// file only decides which duty each state wants.

// One place decides what "the screen is not needed" means: which load stays on
// so a USB power bank keeps supplying the board.
static void screenGoesDark() {
  screensaverActive = false;
  screenOn = false;
  setBacklight(0);          // updateStatusLed() takes over the keep-awake light
}

void setScreenPower(bool on) {
  if (on) {
    if (!screenOn || screensaverActive) {
      setBacklight(TFT_BL_FULL_DUTY);
      screenOn = true;
      screensaverActive = false;
      redrawCurrentScreen();
    }
    lastActivityTime = millis();
  } else {
    if (idleMode == IDLE_CLOCK) {
      // The clock is drawn on the panel, so it needs the panel lit and the LED
      // is not doing any work.
      setBacklight(TFT_BL_FULL_DUTY);
      screensaverActive = true;
      lastSaverTick = millis();
      randomSeed(millis());
      clockX = random(60, 260);
      clockY = random(70, 190);
      drawScreensaver();
    } else if (idleMode == IDLE_DIM) {
      // Not fully dark: a low backlight duty is a bigger, steadier load than
      // the LED alone, and the panel stays almost black.
      screensaverActive = false;
      screenOn = false;
      setBacklight(TFT_BL_DIM_DUTY);
    } else {
      screenGoesDark();
    }
  }
}

// Called every loop: the screensaver still hands over to the dark state (and the
// LED) after the long saver timeout.
void updateIdleScreen() {
  if (currentState == STATE_CALIBRATE) return;
  unsigned long idle = millis() - lastActivityTime;
  if (displayActive() && idle > SCREEN_TIMEOUT_MS) {
    setScreenPower(false);
  } else if (screensaverActive && !pomoRunning && idle > SCREEN_TIMEOUT_MS + SAVER_OFF_MS) {
    screenGoesDark();
  }
}

void drawScreensaver() {
  tft.fillScreen(0x0000);
  int h, m;
  String t = getClock(h, m) ? getTimeString() : "--:--";
  printCentered(t, clockX, clockY, &FreeSansBold24pt7b, 0x4208);
}

void updateScreensaver() {
  if (!screensaverActive) return;
  static int driftX = 1, driftY = 1;
  clockX += driftX;
  clockY += driftY;
  if (clockX < 50 || clockX > 270) driftX = -driftX;
  if (clockY < 60 || clockY > 200) driftY = -driftY;
  static unsigned long lastJump = 0;
  if (millis() - lastJump > 30000) {
    lastJump = millis();
    clockX = random(60, 260);
    clockY = random(70, 190);
    drawScreensaver();
  }
}

uint16_t brighten565(uint16_t c, int amt) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = min(31, r + amt);
  g = min(63, g + amt * 2);
  b = min(31, b + amt);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void printCentered(String text, int centerX, int baselineY, const GFXfont* font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text.c_str(), 0, baselineY, &x1, &y1, &w, &h);
  tft.setCursor(centerX - (int)(w / 2), baselineY);
  tft.print(text);
  tft.setFont(NULL);
}

// Right aligned text: used for counters that sit against the 8 px margin, so a
// widening number grows to the left instead of running off the panel.
void printRight(String text, int rightX, int baselineY, const GFXfont* font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text.c_str(), 0, baselineY, &x1, &y1, &w, &h);
  int x = rightX - (int)w;
  tft.setCursor(x < 0 ? 0 : x, baselineY);
  tft.print(text);
  tft.setFont(NULL);
}

void drawModernButton(int x, int y, int w, int h, int r, uint16_t bg, bool shadow) {
  if (shadow) tft.fillRoundRect(x + 2, y + 2, w, h, r, SHADOW_COLOR);
  tft.fillRoundRect(x, y, w, h, r, bg);
  tft.drawRoundRect(x, y, w, h, r, BTN_OUTLINE);
  tft.drawFastHLine(x + r, y + 1, max(0, w - 2 * r), brighten565(bg, 3));
}

// ==========================================
// SHARED LAYOUT PIECES
// ==========================================
// Every app screen starts with the same 30 px title bar: a hairline at the
// bottom, an optional back chevron on the left and the title centred.  Keeping
// it in one place is what makes the screens look like parts of one product.
void drawScreenHeader(const char* title, bool showBack) {
  tft.fillRect(0, 0, 320, 34, SURFACE_COLOR);
  tft.drawFastHLine(0, 34, 320, BTN_OUTLINE);
  if (showBack) {
    drawModernButton(6, 5, 34, 24, RADIUS_SM, SURFACE_HI, false);
    drawBackChevron(23, 17, TEXT_COLOR);
  }
  if (title && title[0]) printCentered(title, showBack ? 168 : 160, 25, &FreeSansBold12pt7b, TEXT_COLOR);
}

// A square tile used for the home screen and for icon buttons: a faint tint of
// the accent colour instead of a saturated fill, which keeps the icons legible
// while the canvas stays calm.
void drawIconTile(int x, int y, int w, int h, int radius, uint16_t tint) {
  tft.fillRoundRect(x, y, w, h, radius, SURFACE_COLOR);
  tft.drawRoundRect(x, y, w, h, radius, brighten565(tint, -3));
}

// Status pill: "connected", "searching", a counter.  Small, quiet, aligned.
void drawPanel(int x, int y, int w, int h, const char* title) {
  drawCard(x, y, w, h, false, RADIUS_MD);
  if (title && title[0]) drawSectionLabel(title, x + 12, y + 8);
}

void drawStatusPill(int x, int y, int w, const char* text, uint16_t dotColor, uint16_t textColor) {
  int h = 18;
  tft.fillRoundRect(x, y, w, h, h / 2, SURFACE_COLOR);
  tft.drawRoundRect(x, y, w, h, h / 2, BTN_OUTLINE);
  if (dotColor) tft.fillCircle(x + 11, y + (h / 2), 3, dotColor);
  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.setCursor(x + (dotColor ? 20 : 10), y + 6);
  tft.print(text);
}

void drawCard(int x, int y, int w, int h, bool active, int radius) {
  tft.fillRoundRect(x, y, w, h, radius, active ? SURFACE_HI : SURFACE_COLOR);
  tft.drawRoundRect(x, y, w, h, radius, active ? ACCENT_COLOR : BTN_OUTLINE);
}

// Small caption with a hairline rule running to the right margin.  It costs a
// single line and turns a bare caption into a section divider, which is what
// makes the longer pages readable at a glance.
void drawSectionLabel(const char* text, int x, int y) {
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(x, y);
  tft.print(text);
  int w = (int)strlen(text) * 6;
  int ruleX = x + w + 8;
  if (ruleX < 306) tft.drawFastHLine(ruleX, y + 3, 312 - ruleX, BTN_OUTLINE);
}

// A pill shaped switch: the active half is filled with the accent colour and
// the labels are painted on top, which reads much cleaner than N separate
// buttons while costing the same number of primitives.
void drawSegmentedControl(int x, int y, int w, int h, const char* const* labels, int count,
                          int activeIndex, const GFXfont* font) {
  if (count < 1) return;
  tft.fillRoundRect(x, y, w, h, h / 2, SURFACE_COLOR);
  tft.drawRoundRect(x, y, w, h, h / 2, BTN_OUTLINE);
  int segW = (w - 4) / count;
  for (int i = 0; i < count; i++) {
    int sx = x + 2 + (i * segW);
    int sw = (i == count - 1) ? (x + w - 2 - sx) : segW;
    bool active = (i == activeIndex);
    if (active) {
      tft.fillRoundRect(sx, y + 2, sw, h - 4, (h - 4) / 2, ACCENT_COLOR);
      tft.drawRoundRect(sx, y + 2, sw, h - 4, (h - 4) / 2, brighten565(ACCENT_COLOR, 4));
    }
    // GFX faces are positioned by their baseline; the built-in 5x7 font is
    // anchored at the top left of its cell, so it needs the other offset.
    int labelY = (h / 2) + (font ? 6 : -4);
    printCentered(labels[i], sx + (sw / 2), y + labelY, font, active ? BG_COLOR : MUTED_COLOR);
  }
}

void drawProgressBar(int x, int y, int w, int h, float pct, uint16_t color) {
  pct = constrain(pct, 0.0f, 1.0f);
  tft.fillRoundRect(x, y, w, h, h / 2, SURFACE_COLOR);
  int fillW = (int)(pct * (w - 4));
  if (fillW > 0) tft.fillRoundRect(x + 2, y + 2, fillW, h - 4, (h - 4) / 2, color);
}

void drawNoteIcon(int cx, int cy, int size, uint16_t color) {
  int head = size / 3;
  tft.fillCircle(cx - head, cy + head, head, color);
  tft.fillRect(cx + size / 6 - 1, cy - size / 2, 2, size, color);
  tft.fillRect(cx + size / 6 - 1, cy - size / 2, size / 2, 2, color);
}

void drawCrosshairTarget(int cx, int cy, int r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  tft.drawCircle(cx, cy, r - 1, color);
  tft.fillCircle(cx, cy, 4, color);
  tft.drawFastHLine(cx - r - 8, cy, 8, color);
  tft.drawFastHLine(cx + r + 1, cy, 8, color);
  tft.drawFastVLine(cx, cy - r - 8, 8, color);
  tft.drawFastVLine(cx, cy + r + 1, 8, color);
}

void flashButton(int x, int y, int w, int h, int r) {
  tft.fillRoundRect(x, y, w, h, r, PRESS_COLOR);
  delay(35);
}

// Announcement that paints over the middle of the screen for a moment.  A
// neutral card with an accent edge reads as information, not as an error.
void showToast(String msg) {
  tft.fillRoundRect(22, 92, 276, 40, RADIUS_MD, SURFACE_HI);
  tft.drawRoundRect(22, 92, 276, 40, RADIUS_MD, BTN_OUTLINE);
  tft.fillRoundRect(22, 92, 4, 40, 2, ACCENT_COLOR);
  printCentered(msg, 162, 117, &FreeSans9pt7b, TEXT_COLOR);
  delay(900);
}

String formatTime(uint32_t totalSeconds) {
  int m = totalSeconds / 60, s = totalSeconds % 60;
  String res = "";
  if (m < 10) res += "0";
  res += m;
  res += ":";
  if (s < 10) res += "0";
  res += s;
  return res;
}

void drawGearIcon(int cx, int cy, int r, uint16_t color) {
  tft.fillCircle(cx, cy, r, color);
  tft.fillCircle(cx, cy, r / 2, BG_COLOR);
  for (int a = 0; a < 360; a += 45) {
    float rad = a * PI / 180.0;
    tft.drawLine(cx + (int)(cos(rad) * (r + 2)), cy + (int)(sin(rad) * (r + 2)), cx + (int)(cos(rad) * (r + 5)), cy + (int)(sin(rad) * (r + 5)), color);
  }
}

void drawClockIcon(int cx, int cy, int r, uint16_t color) {
  tft.drawCircle(cx, cy, r, color);
  tft.drawCircle(cx, cy, r - 1, color);
  tft.drawLine(cx, cy, cx, cy - r / 2, color);
  tft.drawLine(cx, cy, cx + r / 2 - 2, cy, color);
}

void drawChevron(int cx, int cy, bool pointUp, uint16_t color) {
  if (pointUp) tft.fillTriangle(cx - 6, cy + 3, cx + 6, cy + 3, cx, cy - 4, color);
  else tft.fillTriangle(cx - 6, cy - 3, cx + 6, cy - 3, cx, cy + 4, color);
}

void drawBackChevron(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx + 5, cy - 7, cx + 5, cy + 7, cx - 6, cy, color);
}

void drawListIcon(int cx, int cy, uint16_t color) {
  for (int i = -1; i <= 1; i++) tft.fillRect(cx - 8, cy + i * 6 - 1, 16, 3, color);
}

void drawPlusIcon(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 8, cy - 2, 16, 4, color);
  tft.fillRect(cx - 2, cy - 8, 4, 16, color);
}

void drawMinusIcon(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 8, cy - 2, 16, 4, color);
}

// A trash can, not a minus: list rows use a minus for "fewer", so the delete
// affordance has to look like something else or the two read alike.
void drawTrashIcon(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 3, cy - 8, 6, 2, color);        // handle
  tft.fillRect(cx - 6, cy - 6, 13, 2, color);       // lid
  tft.drawRect(cx - 5, cy - 4, 11, 10, color);      // body
  tft.drawFastVLine(cx - 2, cy - 2, 6, color);      // ribs
  tft.drawFastVLine(cx + 2, cy - 2, 6, color);
}

int drawRadical(int x, int y, int size, uint16_t color) {
  if (size == 1) {
    tft.drawLine(x, y + 4, x + 2, y + 7, color);
    tft.drawLine(x + 2, y + 7, x + 4, y, color);
    tft.drawFastHLine(x + 4, y, 4, color);
    return 9;
  }
  tft.drawLine(x, y + 9, x + 3, y + 15, color);
  tft.drawLine(x + 1, y + 9, x + 4, y + 15, color);
  tft.drawLine(x + 3, y + 15, x + 8, y, color);
  tft.drawLine(x + 4, y + 15, x + 9, y, color);
  tft.drawFastHLine(x + 8, y, 7, color);
  tft.drawFastHLine(x + 8, y + 1, 7, color);
  return 16;
}

void drawWaveIcon(int cx, int cy, int halfW, int amp, uint16_t color) {
  int prevX = cx - halfW, prevY = cy;
  for (int i = -halfW; i <= halfW; i += 2) {
    int x = cx + i, y = cy - (int)(amp * sin((i + halfW) * PI / halfW * 1.5));
    tft.drawLine(prevX, prevY, x, y, color);
    tft.drawLine(prevX, prevY + 1, x, y + 1, color);
    prevX = x;
    prevY = y;
  }
}

void drawPlayIcon(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx - 5, cy - 7, cx - 5, cy + 7, cx + 7, cy, color);
}

void drawPauseIcon(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 6, cy - 6, 4, 12, color);
  tft.fillRect(cx + 2, cy - 6, 4, 12, color);
}

void drawSkipFwdIcon(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx - 6, cy - 6, cx - 6, cy + 6, cx + 3, cy, color);
  tft.fillRect(cx + 3, cy - 6, 3, 12, color);
}

void drawSkipRevIcon(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx + 6, cy - 6, cx + 6, cy + 6, cx - 3, cy, color);
  tft.fillRect(cx - 6, cy - 6, 3, 12, color);
}
