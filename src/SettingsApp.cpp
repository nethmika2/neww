#include "SettingsApp.h"
#include <time.h>
#include <sys/time.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "EarbudControls.h"
#include "Led.h"

// Forward declarations
void drawHomeScreen();
void drawCalibrationScreen();

// ==========================================
// LAYOUT
// ==========================================
// Two labelled groups of toggle cards, then the clock panel.  Everything sits
// on the same 8 px margin grid the other apps use, and the group captions use
// the shared section rule so the page has a clear hierarchy.
// Two pages under one tab strip: the device settings, and the on-board RGB LED
// with its patterns.  Everything below the strip sits on the same 8 px grid.
static const int TAB_X = 10;
static const int TAB_Y = 37;
static const int TAB_W = 300;
static const int TAB_H = 22;
static const int PAGE_TOP = 66;

static const int CARD_W = 148;
static const int CARD_H = 46;
static const int COL_L = 10;
static const int COL_R = 162;
static const int GROUP_1_LABEL_Y = 66;
static const int ROW_1 = 74;
static const int GROUP_2_LABEL_Y = 126;
static const int ROW_2 = 134;
static const int CLOCK_CARD_X = 10;
static const int CLOCK_CARD_Y = 188;
static const int CLOCK_CARD_W = 300;
static const int CLOCK_CARD_H = 42;
// The four clock actions form a 2x2 grid on the right of the panel; the time
// and its sync state take the left half.
static const int CLOCK_BTN_X0 = 152;
static const int CLOCK_BTN_X1 = 234;
static const int CLOCK_BTN_Y0 = 192;
static const int CLOCK_BTN_Y1 = 212;
static const int CLOCK_BTN_W = 76;
static const int CLOCK_BTN_H = 18;

// The LED page: three full rows of patterns, colours and brightness, then the
// follow-apps switch beside the brightness.
static const int LED_ROW_1 = 62;
static const int LED_ROW_2 = 106;
static const int LED_ROW_3 = 150;
static const int LED_ROW_4 = 194;
static const int LED_ROW_H = 44;
static const int LED_CARD_H = 40;

static int settingsPage = 0;                 // 0 = device, 1 = LED

int& settingsPageForTest() { return settingsPage; }

static void clockBtnRect(int index, int& x, int& y) {
  x = (index % 2 == 0) ? CLOCK_BTN_X0 : CLOCK_BTN_X1;
  y = (index < 2) ? CLOCK_BTN_Y0 : CLOCK_BTN_Y1;
}

// A caption inside a card: no rule, just the label on the card's own surface.
static void drawFieldLabel(const char* text, int x, int y, uint16_t color) {
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(x, y);
  tft.print(text);
}

// A toggle card is a title line and a two-way switch, nothing else: the title
// sits on the card's top left, the switch fills the lower part.
static void drawSegCard(int x, int y, int w, const char* title, const char* const* labels, int count,
                        int active, int h = CARD_H) {
  drawCard(x, y, w, h, false, RADIUS_MD);
  drawFieldLabel(title, x + 10, y + 6, MUTED_COLOR);
  // Three or more segments leave each label little room, so those use the
  // built-in font; the two-way cards keep the bolder face.
  drawSegmentedControl(x + 10, y + 18, w - 20, h - 20, labels, count, active,
                       count > 2 ? NULL : &FreeSansBold9pt7b);
}

// Which segment of a control a tap landed on (0 for a miss).  The control is
// inset 10 px inside its card, which is where drawSegmentedControl puts it.
static int segmentAt(int sx, int cardX, int cardW, int count) {
  int pad = 10;
  int innerW = cardW - 2 * pad;
  if (sx < cardX + pad || sx >= cardX + cardW - pad || count < 1) return 0;
  int seg = (sx - (cardX + pad)) / (innerW / count);
  return constrain(seg + 1, 1, count);
}

// The tab strip is shared: it has to be drawn on both pages, and a tap on it
// switches pages rather than doing anything to the page below.
static void drawSettingsTabs() {
  static const char* const pages[2] = { "DEVICE", "LED" };
  drawSegmentedControl(TAB_X, TAB_Y, TAB_W, TAB_H, pages, 2, settingsPage);
}

static void drawLedPage() {
  static const char* const effectLabels[LED_E_COUNT] = { "FADE", "BREATHE", "CYCLE", "PULSE", "SOLID" };
  static const char* const colorLabels[LED_C_COUNT] = { "BLUE", "VIOLET", "GREEN", "AMBER", "WHITE", "RED" };
  static const char* const levelLabels[LED_L_COUNT] = { "LOW", "MED", "HIGH" };
  static const char* const showLabels[LED_S_COUNT] = { "DARK", "ALWAYS", "APPS" };
  static const char* const wiringLabels[2] = { "LOW=ON", "HIGH=ON" };

  drawSegCard(10, LED_ROW_1, 300, "PATTERN", effectLabels, LED_E_COUNT, (int)ledEffect, LED_CARD_H);
  drawSegCard(10, LED_ROW_2, 300, "COLOUR", colorLabels, LED_C_COUNT, (int)ledColor, LED_CARD_H);
  // Brightness needs little room (LOW/MED/HIGH); SHOW gets the rest, because
  // ALWAYS has to fit inside its own third of the card.
  drawSegCard(COL_L, LED_ROW_3, 128, "BRIGHTNESS", levelLabels, LED_L_COUNT, (int)ledLevel, LED_CARD_H);
  drawSegCard(144, LED_ROW_3, 166, "SHOW", showLabels, LED_S_COUNT, (int)ledShow, LED_CARD_H);
  // Wiring: most CYDs light the LED when the pin is pulled LOW, but the clones
  // differ, so this is a setting rather than a guess in the code.
  drawSegCard(10, LED_ROW_4, 300, "WIRING - HIGH=ON IF THE LED IS ALWAYS BRIGHT",
              wiringLabels, 2, ledInvert ? 1 : 0, LED_CARD_H);
}

void drawSettingsScreen() {
  tft.fillScreen(BG_COLOR);
  drawScreenHeader("SETTINGS", true);
  drawSettingsTabs();
  if (settingsPage == 1) {
    drawLedPage();
    return;
  }

  static const char* const axisLabels[2] = { "NUM", "PI" };
  // The three idle modes all keep the board drawing power; they differ in what
  // the screen does and which load does the work.
  static const char* const idleLabels[3] = { "CLOCK", "DIM", "OFF" };
  static const char* const budLabels[2] = { "ON", "OFF" };

  drawSectionLabel("GRAPHER", 10, GROUP_1_LABEL_Y);
  drawSegCard(COL_L, ROW_1, CARD_W, "X AXIS", axisLabels, 2, xAxisPi ? 1 : 0);
  drawSegCard(COL_R, ROW_1, CARD_W, "Y AXIS", axisLabels, 2, yAxisPi ? 1 : 0);

  drawSectionLabel("DEVICE", 10, GROUP_2_LABEL_Y);
  drawSegCard(COL_L, ROW_2, CARD_W, "IDLE SCREEN", idleLabels, 3, (int)idleMode);
  drawSegCard(COL_R, ROW_2, CARD_W, "EARBUDS", budLabels, 2, earbudControlsEnabled() ? 0 : 1);
  // The counter is a live diagnostic: it moves as soon as the buds send
  // anything, so a hardware problem can be told apart from a settings problem
  // without a serial monitor.
  printRight("seen " + String(earbudEventCount()), COL_R + CARD_W - 10, ROW_2 + 8, NULL, MUTED_COLOR);

  // Clock panel: the time is the headline on the left, the actions on the right.
  drawCard(CLOCK_CARD_X, CLOCK_CARD_Y, CLOCK_CARD_W, CLOCK_CARD_H, false, RADIUS_MD);
  drawFieldLabel(timeSynced ? "CLOCK SYNCED" : "CLOCK NOT SYNCED", CLOCK_CARD_X + 12, CLOCK_CARD_Y + 8,
                 timeSynced ? PLOT_COLOR : MUTED_COLOR);
  tft.setFont(&FreeSansBold18pt7b);
  tft.setTextColor(timeSynced ? TEXT_COLOR : MUTED_COLOR);
  tft.setCursor(CLOCK_CARD_X + 12, CLOCK_CARD_Y + 34);
  tft.print(getTimeString());
  tft.setFont(NULL);

  struct { const char* label; uint16_t bg; uint16_t fg; } actions[4] = {
      { "+15M", SURFACE_HI, TEXT_COLOR },
      { "SYNC", PLOT_COLOR, BG_COLOR },
      { autoSyncBoot ? "AUTO" : "MANUAL", autoSyncBoot ? ACCENT_COLOR : SURFACE_HI, autoSyncBoot ? BG_COLOR : TEXT_COLOR },
      { "CALIBRATE", SURFACE_HI, TEXT_COLOR },
  };
  for (int i = 0; i < 4; i++) {
    int x, y;
    clockBtnRect(i, x, y);
    drawModernButton(x, y, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM, actions[i].bg, false);
    printCentered(actions[i].label, x + (CLOCK_BTN_W / 2), y + 6, NULL, actions[i].fg);
  }
}

void handleSettingsTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();
  if (inRect(sx, sy, 0, 0, 44, 34)) {
    flashButton(6, 5, 34, 24, RADIUS_SM);
    currentState = STATE_HOME;
    drawHomeScreen();
    return;
  }

  // Tab strip: DEVICE or LED.
  if (inRect(sx, sy, TAB_X, TAB_Y, TAB_W, TAB_H)) {
    flashButton(TAB_X, TAB_Y, TAB_W, TAB_H, TAB_H / 2);
    settingsPage = (sx > TAB_X + TAB_W / 2) ? 1 : 0;
    drawSettingsScreen();
    return;
  }

  if (settingsPage == 1) {
    // Every change previews itself for a couple of seconds: the LED is on the
    // back of the board, so the light says what the setting does.
    if (inRect(sx, sy, 10, LED_ROW_1, 300, LED_CARD_H)) {
      int seg = segmentAt(sx, 10, 300, LED_E_COUNT);
      flashButton(10, LED_ROW_1, 300, LED_ROW_H, RADIUS_MD);
      ledEffect = (LedEffect)(seg - 1);
      prefs.putInt("ledeffect", (int)ledEffect);
      ledPreview(ledColor, ledEffect, ledLevel);
      showToast(String("LED ") + ledEffectName(ledEffect));
      drawSettingsScreen();
      return;
    }
    if (inRect(sx, sy, 10, LED_ROW_2, 300, LED_CARD_H)) {
      int seg = segmentAt(sx, 10, 300, LED_C_COUNT);
      flashButton(10, LED_ROW_2, 300, LED_ROW_H, RADIUS_MD);
      ledColor = (LedColor)(seg - 1);
      prefs.putInt("ledcolor", (int)ledColor);
      ledPreview(ledColor, ledEffect, ledLevel);
      showToast(String("LED ") + ledColorName(ledColor));
      drawSettingsScreen();
      return;
    }
    if (inRect(sx, sy, COL_L, LED_ROW_3, 128, LED_CARD_H)) {
      int seg = segmentAt(sx, COL_L, 128, LED_L_COUNT);
      flashButton(COL_L, LED_ROW_3, CARD_W, LED_ROW_H, RADIUS_MD);
      ledLevel = (LedLevel)(seg - 1);
      prefs.putInt("ledlevel", (int)ledLevel);
      ledPreview(ledColor, ledEffect, ledLevel);
      ledLogState("brightness");
      showToast(String("Brightness ") + ledLevelName(ledLevel));
      drawSettingsScreen();
      return;
    }
    if (inRect(sx, sy, 144, LED_ROW_3, 166, LED_CARD_H)) {
      int seg = segmentAt(sx, 144, 166, LED_S_COUNT);
      flashButton(COL_R, LED_ROW_3, CARD_W, LED_ROW_H, RADIUS_MD);
      ledShow = (LedShow)(seg - 1);
      prefs.putInt("ledshow", (int)ledShow);
      ledPreview(ledColor, ledEffect, ledLevel);
      showToast(String("LED shows: ") + ledShowName(ledShow));
      drawSettingsScreen();
      return;
    }
    if (inRect(sx, sy, 10, LED_ROW_4, 300, LED_CARD_H)) {
      int seg = segmentAt(sx, 10, 300, 2);
      flashButton(10, LED_ROW_4, 300, LED_ROW_H, RADIUS_MD);
      ledInvert = (seg == 2);
      prefs.putBool("ledinvert", ledInvert);
      ledPreview(ledColor, ledEffect, ledLevel);
      ledLogState("wiring");
      showToast(ledInvert ? "LED wiring HIGH=ON" : "LED wiring LOW=ON");
      drawSettingsScreen();
      return;
    }
    return;
  }

  // X axis
  if (inRect(sx, sy, COL_L + 10, ROW_1 + 20, CARD_W - 20, 26)) {
    bool pi = sx > COL_L + (CARD_W / 2);
    flashButton(COL_L + 10, ROW_1 + 20, CARD_W - 20, 26, RADIUS_SM);
    xAxisPi = pi;
    prefs.putBool("xpi", xAxisPi);
    drawSettingsScreen();
    return;
  }
  // Y axis
  if (inRect(sx, sy, COL_R + 10, ROW_1 + 20, CARD_W - 20, 26)) {
    bool pi = sx > COL_R + (CARD_W / 2);
    flashButton(COL_R + 10, ROW_1 + 20, CARD_W - 20, 26, RADIUS_SM);
    yAxisPi = pi;
    prefs.putBool("ypi", yAxisPi);
    drawSettingsScreen();
    return;
  }
  // Idle screen behaviour: clock screensaver, dimmed panel, or dark with the
  // on-board LED lit so the power bank keeps the board alive.
  if (inRect(sx, sy, COL_L + 10, ROW_2 + 20, CARD_W - 20, 26)) {
    int seg = segmentAt(sx, COL_L, CARD_W, 3);
    flashButton(COL_L + 10, ROW_2 + 20, CARD_W - 20, 26, RADIUS_SM);
    idleMode = (IdleMode)(seg - 1);
    prefs.putInt("idlemode", (int)idleMode);
    showToast(idleMode == IDLE_CLOCK ? "Idle: clock screensaver"
              : idleMode == IDLE_DIM ? "Idle: dim screen + LED"
                                     : "Idle: dark screen + LED");
    drawSettingsScreen();
    return;
  }
  // Earbud transport buttons
  if (inRect(sx, sy, COL_R + 10, ROW_2 + 20, CARD_W - 20, 26)) {
    bool on = sx <= COL_R + (CARD_W / 2);
    flashButton(COL_R + 10, ROW_2 + 20, CARD_W - 20, 26, RADIUS_SM);
    earbudControlsSetEnabled(on);
    showToast(on ? "Earbud buttons on" : "Earbud buttons off");
    drawSettingsScreen();
    return;
  }

  for (int i = 0; i < 4; i++) {
    int bx, by;
    clockBtnRect(i, bx, by);
    if (!inRect(sx, sy, bx, by, CLOCK_BTN_W, CLOCK_BTN_H)) continue;
    flashButton(bx, by, CLOCK_BTN_W, CLOCK_BTN_H, RADIUS_SM);
    if (i == 0) {
      // Nudge the clock forward without a network.
      time_t now;
      time(&now);
      now += 15 * 60;
      struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
      settimeofday(&tv, NULL);
      drawSettingsScreen();
    } else if (i == 1) {
      syncTimeNTP(true);
      drawSettingsScreen();
    } else if (i == 2) {
      autoSyncBoot = !autoSyncBoot;
      prefs.putBool("autosync", autoSyncBoot);
      drawSettingsScreen();
    } else {
      currentState = STATE_CALIBRATE;
      calibStep = 0;
      calibFailCount = 0;
      drawCalibrationScreen();
    }
    return;
  }
}
