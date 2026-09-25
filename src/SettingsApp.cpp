#include "SettingsApp.h"
#include <time.h>
#include <sys/time.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "EarbudControls.h"

// Forward declarations
void drawHomeScreen();
void drawCalibrationScreen();

// ==========================================
// LAYOUT
// ==========================================
// Two labelled groups of toggle cards, then the clock panel.  Everything sits
// on the same 8 px margin grid the other apps use, and the group captions use
// the shared section rule so the page has a clear hierarchy.
static const int CARD_W = 148;
static const int CARD_H = 54;
static const int COL_L = 10;
static const int COL_R = 162;
static const int GROUP_1_LABEL_Y = 42;
static const int ROW_1 = 52;
static const int GROUP_2_LABEL_Y = 116;
static const int ROW_2 = 126;
static const int CLOCK_CARD_X = 10;
static const int CLOCK_CARD_Y = 190;
static const int CLOCK_CARD_W = 300;
static const int CLOCK_CARD_H = 46;
// The four clock actions form a 2x2 grid on the right of the panel; the time
// and its sync state take the left half.
static const int CLOCK_BTN_X0 = 152;
static const int CLOCK_BTN_X1 = 234;
static const int CLOCK_BTN_Y0 = 196;
static const int CLOCK_BTN_Y1 = 217;
static const int CLOCK_BTN_W = 76;
static const int CLOCK_BTN_H = 18;

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
static void drawToggleCard(int x, int y, const char* title, const char* const* labels, int count,
                           int active) {
  drawCard(x, y, CARD_W, CARD_H, false, RADIUS_MD);
  drawFieldLabel(title, x + 10, y + 8, MUTED_COLOR);
  // Three segments leave each label little room, so those use the built-in
  // font; the two-way cards keep the bolder face.
  drawSegmentedControl(x + 10, y + 20, CARD_W - 20, 26, labels, count, active,
                       count > 2 ? NULL : &FreeSansBold9pt7b);
}

// Which segment of a card control a tap landed on (0 for a miss).
static int segmentAt(int sx, int cardX, int count) {
  if (sx < cardX + 10 || sx >= cardX + CARD_W - 10) return 0;
  int seg = (sx - (cardX + 10)) / ((CARD_W - 20) / count);
  return constrain(seg + 1, 1, count);
}

void drawSettingsScreen() {
  tft.fillScreen(BG_COLOR);
  drawScreenHeader("SETTINGS", true);

  static const char* const axisLabels[2] = { "NUM", "PI" };
  // The three idle modes all keep the board drawing power; they differ in what
  // the screen does and which load does the work.
  static const char* const idleLabels[3] = { "CLOCK", "DIM", "OFF" };
  static const char* const budLabels[2] = { "ON", "OFF" };

  drawSectionLabel("GRAPHER", 10, GROUP_1_LABEL_Y);
  drawToggleCard(COL_L, ROW_1, "X AXIS", axisLabels, 2, xAxisPi ? 1 : 0);
  drawToggleCard(COL_R, ROW_1, "Y AXIS", axisLabels, 2, yAxisPi ? 1 : 0);

  drawSectionLabel("DEVICE", 10, GROUP_2_LABEL_Y);
  drawToggleCard(COL_L, ROW_2, "IDLE SCREEN", idleLabels, 3, (int)idleMode);
  drawToggleCard(COL_R, ROW_2, "EARBUDS", budLabels, 2, earbudControlsEnabled() ? 0 : 1);
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
  tft.setCursor(CLOCK_CARD_X + 12, CLOCK_CARD_Y + 36);
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
    int seg = segmentAt(sx, COL_L, 3);
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
