#include "PomodoroApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "PomodoroStore.h"
#include "TextInput.h"

// ==========================================
// PAGE CHROME & SCROLLING
// ==========================================
// The Task and Preset pages can hold more than one screenful.  Instead of
// silently cutting the last row off, the content is shifted by pomoScrollY and
// the header plus the scroll control are repainted on top afterwards, so the
// chrome always stays put while the content slides underneath it.
static const int PAGE_TOP = 32;        // below the tab bar
static const int PAGE_BOTTOM = 204;    // above the bottom action bar
// Two small chevron buttons stacked in the bottom right corner, clear of the
// action bar buttons and inside the 240 px screen.
static const int SCROLL_BTN_X = 262;
static const int SCROLL_BTN_W = 50;
static const int SCROLL_BTN_H = 16;
static const int SCROLL_UP_Y = 205;
static const int SCROLL_DOWN_Y = 222;

static void drawScrollButton(int y, bool up, bool enabled) {
  drawModernButton(SCROLL_BTN_X, y, SCROLL_BTN_W, SCROLL_BTN_H, 4, enabled ? SURFACE_HI : SURFACE_COLOR, false);
  uint16_t color = enabled ? TEXT_COLOR : MUTED_COLOR;
  int cx = SCROLL_BTN_X + (SCROLL_BTN_W / 2), cy = y + (SCROLL_BTN_H / 2);
  drawChevron(cx, cy, up, color);
}

// Draws the fixed chrome after the (possibly scrolled) content.
static void drawPageChrome() {
  drawPomoTopBar();
  if (pomoScrollMax <= 0) return;
  drawScrollButton(SCROLL_UP_Y, true, pomoScrollY > 0);
  drawScrollButton(SCROLL_DOWN_Y, false, pomoScrollY < pomoScrollMax);
}

static void clampScroll() {
  if (pomoScrollY < 0) pomoScrollY = 0;
  if (pomoScrollY > pomoScrollMax) pomoScrollY = pomoScrollMax;
}

// Small helper so a page can report how tall its content is.
static void setPageContentHeight(int height) {
  int visible = PAGE_BOTTOM - PAGE_TOP;
  pomoScrollMax = max(0, height - visible);
  clampScroll();
}

// True when a block of the given height overlaps the scrollable viewport.  Used
// to skip content that is scrolled completely out of sight, so a scrolled page
// costs no more SPI traffic than a short one.
static bool blockVisible(int y, int h) {
  return (y + h) > PAGE_TOP && y < PAGE_BOTTOM;
}

static void drawCheckMark(int cx, int cy, uint16_t color) {
  tft.drawLine(cx - 4, cy, cx - 1, cy + 3, color);
  tft.drawLine(cx - 1, cy + 3, cx + 4, cy - 3, color);
  tft.drawLine(cx - 4, cy - 1, cx - 1, cy + 2, color);
  tft.drawLine(cx - 1, cy + 2, cx + 4, cy - 4, color);
}

static const char* const WEEKDAY_SHORT[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

static String truncated(const String& s, int maxChars) {
  if ((int)s.length() <= maxChars) return s;
  return s.substring(0, maxChars - 2) + "..";
}

static const char* const WEEKDAY_MED[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* const MONTH_SHORT[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// Civil-calendar decomposition of a day number (days since 1970-01-01).
static void civilFromDays(uint32_t day, int& year, int& month, int& dayOfMonth) {
  long z = (long)day + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp = (5 * doy + 2) / 153;
  unsigned long d = doy - (153 * mp + 2) / 5 + 1;
  long m = (long)mp + (mp < 10 ? 3 : -9);
  year = (int)(y + (m <= 2 ? 1 : 0));
  month = (int)m;
  dayOfMonth = (int)d;
}

// "Wed 23 Sep" for a report header, or "Today" when the clock was never set.
static String pomoDateLabel(uint32_t day) {
  if (!pomoClockValid() || day == 0) return "Today";
  int y, m, d;
  civilFromDays(day, y, m, d);
  String out = String(WEEKDAY_MED[pomoWeekday(day)]) + " " + String(d) + " " + String(MONTH_SHORT[constrain(m - 1, 0, 11)]);
  return out;
}

static int pomoStreakDays() {
  uint32_t today = pomoTodayDay();
  bool todayDone = pomoStatBlocks(today) > 0;
  if (!todayDone && today == 0) return 0;
  long offset = todayDone ? 0 : -1;
  int streak = 0;
  while (streak < 365) {
    long day = (long)today + offset;
    if (day < 0) break;
    if (pomoStatBlocks((uint32_t)day) == 0) break;
    streak++;
    offset--;
  }
  return streak;
}

// ==========================================
// TO-DO LIST
// ==========================================
// Row layout (all offsets inside the 304 px card):
//   checkbox | title | - | n/m | + | delete
static const int TASK_ROW_X = 8;
static const int TASK_ROW_W = 304;
static const int TASK_ROW_H = 26;
static const int TASK_HINT_H = 12;
static const int TASK_ACT_X = 8;
static const int TASK_ACT_Y = 206;
static const int TASK_ACT_H = 32;
static const int ROW_TITLE_MAX = 20;

static void taskRowZones(int y, int& cbX, int& minusX, int& countX, int& plusX, int& delX) {
  cbX = TASK_ROW_X + 2;
  minusX = TASK_ROW_X + 178;   // 186..206
  countX = TASK_ROW_X + 206;   // the counter sits between the two steppers
  plusX = TASK_ROW_X + 238;    // 246..266
  delX = TASK_ROW_X + 272;     // 280..310
  (void)y;
}

static void drawPomoTaskRow(int i, int y) {
  const PomoTask& t = pomoTasks[i];
  bool active = (pomoActiveTask == i);
  drawCard(TASK_ROW_X, y, TASK_ROW_W, TASK_ROW_H - 2, active, RADIUS_SM);
  // A short accent bar marks the item the running timer is crediting.
  if (active) tft.fillRect(TASK_ROW_X + 1, y + 6, 3, TASK_ROW_H - 14, ACCENT_COLOR);

  int cbX, minusX, countX, plusX, delX;
  taskRowZones(y, cbX, minusX, countX, plusX, delX);

  int cbx = cbX + 16, cby = y + 12;
  if (t.done) {
    tft.fillCircle(cbx, cby, 8, PLOT_COLOR);
    drawCheckMark(cbx, cby, BG_COLOR);
  } else {
    tft.drawCircle(cbx, cby, 8, MUTED_COLOR);
    tft.drawCircle(cbx, cby, 7, MUTED_COLOR);
  }

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(t.done ? MUTED_COLOR : TEXT_COLOR);
  tft.setCursor(TASK_ROW_X + 34, y + 16);
  tft.print(truncated(t.text, ROW_TITLE_MAX));
  tft.setFont(NULL);

  // Block estimate: "- n/m +" so it can go down as well as up.
  drawModernButton(minusX, y + 4, 20, 18, 4, SURFACE_HI, false);
  drawMinusIcon(minusX + 10, y + 13, TEXT_COLOR);
  // "done / planned": the steppers below move the planned part.
  String count = String(t.blocks) + "/" + String(t.target);
  printCentered(count, (minusX + 20 + plusX) / 2, y + 9, NULL, t.blocks >= t.target ? PLOT_COLOR : ACCENT_COLOR);
  drawModernButton(plusX, y + 4, 20, 18, 4, SURFACE_HI, false);
  drawPlusIcon(plusX + 10, y + 13, TEXT_COLOR);

  drawModernButton(delX, y + 4, 30, 18, 4, SURFACE_HI, false);
  drawMinusIcon(delX + 15, y + 13, DEL_COLOR);
}

void drawPomoTasksView() {
  tft.fillScreen(BG_COLOR);
  int used = 0, done = 0;
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    used++;
    if (pomoTasks[i].done) done++;
  }

  // Content (scrolls)
  int contentH = TASK_HINT_H + (used * TASK_ROW_H);
  setPageContentHeight(contentH);
  int y = PAGE_TOP - pomoScrollY;
  // The built-in font is anchored at its top left corner, so the hint line sits
  // one pixel below the viewport top and the first row follows right after it.
  if (blockVisible(y, TASK_HINT_H)) {
    drawSectionLabel("TAP A ROW TO FOCUS IT", 10, y + 2);
    printRight(String(done) + "/" + String(used) + " done", 312, y + 2, NULL, MUTED_COLOR);
  }
  y += TASK_HINT_H;
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    // Rows that fall outside the viewport are skipped, which keeps the cost of
    // a scrolled page the same as an unscrolled one.
    if (blockVisible(y, TASK_ROW_H)) drawPomoTaskRow(i, y);
    y += TASK_ROW_H;
  }

  // Fixed chrome: bottom action bar, tab bar and the scroll control.
  tft.fillRect(0, PAGE_BOTTOM, 320, 240 - PAGE_BOTTOM, BG_COLOR);
  drawModernButton(TASK_ACT_X, TASK_ACT_Y, 142, TASK_ACT_H, RADIUS_MD, PLOT_COLOR, true);
  printCentered("ADD TASK", TASK_ACT_X + 71, TASK_ACT_Y + 21, &FreeSansBold9pt7b, TEXT_COLOR);
  drawModernButton(156, TASK_ACT_Y, 104, TASK_ACT_H, RADIUS_MD, SURFACE_COLOR, true);
  printCentered("CLEAR DONE", 208, TASK_ACT_Y + 21, &FreeSans9pt7b, TEXT_COLOR);
  drawPageChrome();

  if (used == 0 && blockVisible(PAGE_TOP, 40)) {
    printCentered("Nothing planned yet", 160, 104, &FreeSans9pt7b, TEXT_COLOR);
    printCentered("Tap ADD TASK to write down the first block", 160, 126, &FreeSans9pt7b, MUTED_COLOR);
  }
}

void handlePomoTasksTouch(int sx, int sy) {
  if (inRect(sx, sy, TASK_ACT_X, TASK_ACT_Y, 142, TASK_ACT_H)) {
    flashButton(TASK_ACT_X, TASK_ACT_Y, 142, TASK_ACT_H, RADIUS_MD);
    startTextInput("NEW TASK", "", TEXT_TARGET_TASK, POMO_NAME_LEN);
    return;
  }
  if (inRect(sx, sy, 156, TASK_ACT_Y, 104, TASK_ACT_H)) {
    flashButton(156, TASK_ACT_Y, 104, TASK_ACT_H, RADIUS_MD);
    clearDonePomoTasks();
    drawPomoTasksView();
    return;
  }
  if (pomoHandleScrollTouch(sx, sy)) return;

  int y = PAGE_TOP - pomoScrollY + TASK_HINT_H;
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    int rowY = y;
    y += TASK_ROW_H;
    if (rowY < PAGE_TOP || rowY + TASK_ROW_H > PAGE_BOTTOM) continue;  // scrolled out or partly hidden
    if (!inRect(sx, sy, TASK_ROW_X, rowY, TASK_ROW_W, TASK_ROW_H - 2)) continue;
    int cbX, minusX, countX, plusX, delX;
    taskRowZones(rowY, cbX, minusX, countX, plusX, delX);
    if (inRect(sx, sy, cbX, rowY, 32, TASK_ROW_H - 2)) {
      flashButton(cbX, rowY, 32, TASK_ROW_H - 2, RADIUS_SM);
      togglePomoTaskDone(i);
    } else if (inRect(sx, sy, minusX, rowY, 20, TASK_ROW_H - 2)) {
      flashButton(minusX, rowY, 20, TASK_ROW_H - 2, 4);
      adjustPomoTaskTarget(i, -1);
    } else if (inRect(sx, sy, plusX, rowY, 20, TASK_ROW_H - 2)) {
      flashButton(plusX, rowY, 20, TASK_ROW_H - 2, 4);
      adjustPomoTaskTarget(i, 1);
    } else if (inRect(sx, sy, delX, rowY, 34, TASK_ROW_H - 2)) {
      flashButton(delX, rowY, 34, TASK_ROW_H - 2, RADIUS_SM);
      deletePomoTask(i);
    } else {
      flashButton(TASK_ROW_X, rowY, TASK_ROW_W, TASK_ROW_H - 2, RADIUS_SM);
      if (pomoTasks[i].done) togglePomoTaskDone(i);
      pomoActiveTask = i;
      savePomoSettings();
    }
    drawPomoTasksView();
    return;
  }
}

// ==========================================
// ROUTINES (TEMPLATES)
// ==========================================
static const int PRE_CELL_W = 150;
static const int PRE_CELL_H = 30;
static const int PRE_CELL_GAP = 3;
static const int PRE_ROW_H = 20;
static const int PRE_LABEL_H = 10;   // "CURRENT ROUTINE"
static const int PRE_AUTO_H = 22;
static const int PRE_SAVE_H = 24;
// Vertical offsets inside the page, so the drawing and the touch zones cannot
// drift apart.
static const int PRE_OFF_LABEL = 0;
static const int PRE_OFF_ROW1 = PRE_LABEL_H;
static const int PRE_OFF_ROW2 = PRE_OFF_ROW1 + PRE_CELL_H + PRE_CELL_GAP;
static const int PRE_OFF_AUTO = PRE_OFF_ROW2 + PRE_CELL_H + 4;
static const int PRE_OFF_SAVE = PRE_OFF_AUTO + PRE_AUTO_H + 4;
static const int PRE_OFF_LIST = PRE_OFF_SAVE + PRE_SAVE_H + PRE_LABEL_H;

// One editable duration: label, value and a pair of steppers.
static void drawPresetCell(int x, int y, const char* label, int value, bool minutes) {
  drawCard(x, y, PRE_CELL_W, PRE_CELL_H, false, RADIUS_SM);
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(x + 8, y + 4);
  tft.print(label);
  String text = minutes ? String(value) + "m" : "x" + String(value);
  printCentered(text, x + 60, y + 21, &FreeSansBold9pt7b, TEXT_COLOR);
  drawModernButton(x + 94, y + 3, 24, 24, 4, SURFACE_HI, false);
  drawMinusIcon(x + 106, y + 15, TEXT_COLOR);
  drawModernButton(x + 122, y + 3, 24, 24, 4, SURFACE_HI, false);
  drawPlusIcon(x + 134, y + 15, TEXT_COLOR);
}

static bool presetCellHit(int cell, int sx, int sy, int contentTop, int& dir) {
  int x = (cell % 2 == 0) ? 8 : 162;
  int y = contentTop + ((cell < 2) ? PRE_OFF_ROW1 : PRE_OFF_ROW2);
  if (inRect(sx, sy, x + 94, y + 3, 24, 24)) {
    dir = -1;
    return true;
  }
  if (inRect(sx, sy, x + 122, y + 3, 24, 24)) {
    dir = 1;
    return true;
  }
  return false;
}

static void drawPomoPresetRow(int i, int y) {
  const PomoTemplate& t = pomoTemplates[i];
  drawCard(8, y, 304, PRE_ROW_H - 2, false, RADIUS_SM);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(16, y + 15);
  tft.print(truncated(t.name, 18));
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(170, y + 6);
  tft.print(pomoTemplateSummary(t));
  drawModernButton(284, y + 2, 24, 16, 3, SURFACE_HI, false);
  drawMinusIcon(296, y + 10, DEL_COLOR);
}

void drawPomoPresetsView() {
  tft.fillScreen(BG_COLOR);

  int used = 0;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++)
    if (pomoTemplates[i].in_use) used++;

  // Content height of the whole page: durations, options, save button, list.
  int contentH = PRE_OFF_LIST + PRE_LABEL_H + (used * PRE_ROW_H);
  setPageContentHeight(contentH);

  int y = PAGE_TOP - pomoScrollY + PRE_OFF_LABEL;
  if (blockVisible(y, PRE_LABEL_H)) drawSectionLabel("CURRENT ROUTINE", 10, y + 2);
  y = PAGE_TOP - pomoScrollY + PRE_OFF_ROW1;
  if (blockVisible(y, PRE_CELL_H)) {
    drawPresetCell(8, y, "WORK", pomoWorkTime / 60, true);
    drawPresetCell(162, y, "SHORT", pomoShortTime / 60, true);
  }
  y = PAGE_TOP - pomoScrollY + PRE_OFF_ROW2;
  if (blockVisible(y, PRE_CELL_H)) {
    drawPresetCell(8, y, "LONG", pomoLongTime / 60, true);
    drawPresetCell(162, y, "CYCLES", pomoLongEvery, false);
  }

  // Auto start of the next phase.
  y = PAGE_TOP - pomoScrollY + PRE_OFF_AUTO;
  if (blockVisible(y, PRE_AUTO_H)) {
    drawModernButton(8, y, 196, PRE_AUTO_H, RADIUS_SM, SURFACE_COLOR, false);
    printCentered("AUTO START NEXT", 106, y + 15, &FreeSans9pt7b, MUTED_COLOR);
    static const char* const autoLabels[2] = { "ON", "OFF" };
    drawSegmentedControl(212, y, 100, PRE_AUTO_H, autoLabels, 2, pomoAutoStart ? 0 : 1);
  }

  y = PAGE_TOP - pomoScrollY + PRE_OFF_SAVE;
  if (blockVisible(y, PRE_SAVE_H)) {
    drawModernButton(8, y, 304, PRE_SAVE_H, RADIUS_MD, PLOT_COLOR, true);
    printCentered("SAVE CURRENT AS ROUTINE", 160, y + 17, &FreeSans9pt7b, TEXT_COLOR);
  }

  y = PAGE_TOP - pomoScrollY + PRE_OFF_LIST;
  if (blockVisible(y, PRE_LABEL_H)) drawSectionLabel("SAVED ROUTINES", 10, y + 2);
  y += PRE_LABEL_H;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (!pomoTemplates[i].in_use) continue;
    if (blockVisible(y, PRE_ROW_H)) drawPomoPresetRow(i, y);
    y += PRE_ROW_H;
  }
  if (used == 0 && blockVisible(y, 16)) printCentered("Nothing saved yet", 160, y + 12, &FreeSans9pt7b, MUTED_COLOR);

  // Fixed chrome.
  tft.fillRect(0, PAGE_BOTTOM, 320, 240 - PAGE_BOTTOM, BG_COLOR);
  drawPageChrome();
}

void handlePomoPresetsTouch(int sx, int sy) {
  int contentTop = PAGE_TOP - pomoScrollY;

  // Duration steppers
  for (int cell = 0; cell < 4; cell++) {
    int dir = 0;
    if (!presetCellHit(cell, sx, sy, contentTop, dir)) continue;
    int x = (cell % 2 == 0) ? 8 : 162;
    int cy = contentTop + ((cell < 2) ? PRE_OFF_ROW1 : PRE_OFF_ROW2);
    flashButton(x + (dir < 0 ? 94 : 122), cy + 3, 24, 24, 4);
    int workMin = pomoWorkTime / 60, shortMin = pomoShortTime / 60, longMin = pomoLongTime / 60;
    if (cell == 0) {
      workMin = constrain(workMin + (dir * 5), MIN_WORK_MINUTES, MAX_WORK_MINUTES);
      pomoWorkTime = workMin * 60;
    } else if (cell == 1) {
      shortMin = constrain(shortMin + dir, MIN_SHORT_MINUTES, MAX_SHORT_MINUTES);
      pomoShortTime = shortMin * 60;
    } else if (cell == 2) {
      longMin = constrain(longMin + (dir * 5), MIN_LONG_MINUTES, MAX_LONG_MINUTES);
      pomoLongTime = longMin * 60;
    } else {
      pomoLongEvery = constrain(pomoLongEvery + dir, MIN_CYCLES, MAX_CYCLES);
    }
    // A changed duration only takes effect on the next block of that kind, so
    // a running countdown is never disturbed.
    if (!pomoRunning && ((cell == 0 && pomoMode == MODE_WORK) || (cell == 1 && pomoMode == MODE_SHORT_BREAK) || (cell == 2 && pomoMode == MODE_LONG_BREAK))) {
      pomoApplyMode(pomoMode);
    }
    savePomoSettings();
    drawPomoPresetsView();
    return;
  }

  int y = contentTop + PRE_OFF_AUTO;
  if (inRect(sx, sy, 8, y, 196, PRE_AUTO_H)) {
    flashButton(8, y, 196, PRE_AUTO_H, RADIUS_SM);
    pomoAutoStart = !pomoAutoStart;
    savePomoSettings();
    drawPomoPresetsView();
    return;
  }
  if (inRect(sx, sy, 212, y, 100, PRE_AUTO_H)) {
    flashButton(212, y, 100, PRE_AUTO_H, RADIUS_SM);
    pomoAutoStart = (sx <= 262);
    savePomoSettings();
    drawPomoPresetsView();
    return;
  }
  y = contentTop + PRE_OFF_SAVE;
  if (inRect(sx, sy, 8, y, 304, PRE_SAVE_H)) {
    flashButton(8, y, 304, PRE_SAVE_H, RADIUS_MD);
    startTextInput("SAVE ROUTINE", "", TEXT_TARGET_TEMPLATE, POMO_NAME_LEN);
    return;
  }
  y = contentTop + PRE_OFF_LIST + PRE_LABEL_H;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (!pomoTemplates[i].in_use) continue;
    int rowY = y;
    y += PRE_ROW_H;
    if (rowY < PAGE_TOP - 2 || rowY + PRE_ROW_H > PAGE_BOTTOM + 2) continue;
    if (!inRect(sx, sy, 8, rowY, 304, PRE_ROW_H - 2)) continue;
    if (inRect(sx, sy, 280, rowY, 32, PRE_ROW_H - 2)) {
      flashButton(280, rowY, 32, PRE_ROW_H - 2, RADIUS_SM);
      String name = pomoTemplates[i].name;
      deletePomoTemplate(i);
      showToast("Removed " + truncated(name, 14));
    } else {
      flashButton(8, rowY, 304, PRE_ROW_H - 2, RADIUS_SM);
      applyPomoTemplate(i);
      showToast("Loaded " + truncated(pomoTemplates[i].name, 14));
      pomoView = POMO_VIEW_TIMER;
    }
    drawPomodoroScreen(true);
    return;
  }
  if (pomoHandleScrollTouch(sx, sy)) return;
}

// ==========================================
// REPORTS
// ==========================================
static const int STATS_TAB_X = 8;
static const int STATS_TAB_Y = 34;
static const int STATS_TAB_W = 304;
static const int STATS_TAB_H = 26;

static int chartScale(int maxValue) {
  if (maxValue < 30) return 30;
  if (maxValue < 60) return 60;
  if (maxValue < 90) return 90;
  if (maxValue < 120) return 120;
  if (maxValue < 180) return 180;
  if (maxValue < 240) return 240;
  return ((maxValue + 59) / 60) * 60;
}

static void drawStatsTabs() {
  static const char* const labels[3] = { "DAY", "WEEK", "MONTH" };
  drawSegmentedControl(STATS_TAB_X, STATS_TAB_Y, STATS_TAB_W, STATS_TAB_H, labels, 3, (int)statsTab);
}

static void drawDayReport() {
  uint32_t today = pomoTodayDay();
  int minutes = pomoStatMinutes(today);
  int blocks = pomoStatBlocks(today);
  int streak = pomoStreakDays();

  printCentered(pomoDateLabel(today), 160, 72, &FreeSans9pt7b, MUTED_COLOR);
  printCentered(pomoFormatMinutes(minutes), 160, 102, &FreeSansBold24pt7b, minutes > 0 ? TEXT_COLOR : MUTED_COLOR);
  String sub = String(blocks) + " block" + (blocks == 1 ? "" : "s") + "   -   streak " + String(streak) + "d";
  printCentered(sub, 160, 122, &FreeSans9pt7b, MUTED_COLOR);

  // Daily goal: a progress bar with the steppers that change the target.
  int goal = constrain(pomoDailyGoal, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
  float pct = constrain((float)blocks / (float)goal, 0.0f, 1.0f);
  bool reached = blocks >= goal;
  drawProgressBar(20, 132, 280, 12, pct, reached ? PLOT_COLOR : ACCENT_COLOR);
  // Goal row: stepper, label and stepper on one shared baseline.
  drawModernButton(60, 150, 44, 26, RADIUS_SM, SURFACE_HI, false);
  drawMinusIcon(82, 163, TEXT_COLOR);
  printCentered("GOAL " + String(goal) + " BLOCKS" + (reached ? " DONE" : ""), 160, 166, &FreeSans9pt7b, reached ? PLOT_COLOR : MUTED_COLOR);
  drawModernButton(216, 150, 44, 26, RADIUS_SM, SURFACE_HI, false);
  drawPlusIcon(238, 163, TEXT_COLOR);

  // Hour by hour distribution of today's focus time.
  drawSectionLabel("FOCUS BY HOUR", 10, 182);
  int maxH = 0;
  for (int h = 0; h < 24; h++) maxH = max(maxH, (int)pomoHourly[h]);
  int scale = chartScale(maxH);
  for (int h = 0; h < 24; h++) {
    int x = 16 + (h * 12);
    int barH = (int)(((float)pomoHourly[h] / scale) * 22.0f);
    if (pomoHourly[h] > 0) barH = max(barH, 2);
    tft.fillRect(x, 226 - barH, 9, barH, pomoHourly[h] > 0 ? ACCENT_COLOR : SURFACE_COLOR);
    if (h % 6 == 0) {
      tft.setTextSize(1);
      tft.setTextColor(MUTED_COLOR);
      tft.setCursor(x, 228);
      tft.print(String(h));
    }
  }
}

static void drawWeekReport() {
  int blocks = 0;
  int total = pomoSumMinutes(-6, 0, blocks);
  int bestMinutes = 0;
  uint32_t bestDay = pomoBestDay(-6, 0, bestMinutes);
  int activeDays = pomoActiveDays(-6, 0);
  uint32_t today = pomoTodayDay();

  printCentered("LAST 7 DAYS", 160, 78, &FreeSans9pt7b, MUTED_COLOR);
  int values[7];
  int peak = 0;
  for (int i = 0; i < 7; i++) {
    long day = (long)today - (6 - i);
    values[i] = day < 0 ? 0 : pomoStatMinutes((uint32_t)day);
    peak = max(peak, values[i]);
  }
  int scale = chartScale(peak);
  const int baseY = 176;
  const int barW = 26;
  const int step = 41;  // 7 bars of 26 px inside 24..296
  for (int i = 0; i < 7; i++) {
    int x = 24 + (i * step);
    int h = (int)(((float)values[i] / scale) * 98.0f);
    if (values[i] > 0) h = max(h, 3);
    tft.fillRect(x, baseY - h, barW, h, i == 6 ? ACCENT_COLOR : PLOT_COLOR);
    if (values[i] > 0) printCentered(String(values[i]), x + (barW / 2), baseY - h - 4, NULL, MUTED_COLOR);
    tft.drawFastHLine(24, baseY + 1, 272, BTN_OUTLINE);
    long day = (long)today - (6 - i);
    String label = (day < 0 || !pomoClockValid()) ? String("d") + String(i) : String(WEEKDAY_SHORT[pomoWeekday((uint32_t)day)]);
    printCentered(label, x + (barW / 2), baseY + 16, &FreeSans9pt7b, i == 6 ? TEXT_COLOR : MUTED_COLOR);
    if (day >= 0 && pomoClockValid()) {
      int y2, m2, d2;
      civilFromDays((uint32_t)day, y2, m2, d2);
      printCentered(String(d2), x + (barW / 2), baseY + 20, NULL, MUTED_COLOR);
    }
  }
  printCentered("TOTAL " + pomoFormatMinutes(total) + "  -  " + String(blocks) + " blocks", 160, 214, &FreeSans9pt7b, TEXT_COLOR);
  String footer = "active " + String(activeDays) + "d  -  avg " + pomoFormatMinutes(activeDays ? total / activeDays : 0);
  if (bestMinutes > 0) footer += "  -  best " + String(WEEKDAY_MED[pomoWeekday(bestDay)]) + " " + pomoFormatMinutes(bestMinutes);
  printCentered(footer, 160, 226, NULL, MUTED_COLOR);
}

static void drawMonthReport() {
  int blocks = 0;
  int total30 = pomoSumMinutes(-29, 0, blocks);
  int activeDays = pomoActiveDays(-29, 0);
  int bestMinutes = 0;
  uint32_t bestDay = pomoBestDay(-29, 0, bestMinutes);
  uint32_t today = pomoTodayDay();
  int best = 0;
  int values[30];
  for (int i = 0; i < 30; i++) {
    long day = (long)today - (29 - i);
    values[i] = day < 0 ? 0 : pomoStatMinutes((uint32_t)day);
    best = max(best, values[i]);
  }
  int scale = chartScale(best);
  printCentered("LAST 30 DAYS", 160, 78, &FreeSans9pt7b, MUTED_COLOR);
  // A narrow, calendar-like strip: one bar per day.
  for (int i = 0; i < 30; i++) {
    int x = 10 + (i * 10);
    int h = (int)(((float)values[i] / scale) * 68.0f);
    if (values[i] > 0) h = max(h, 3);
    tft.fillRect(x, 174 - h, 7, h, i == 29 ? ACCENT_COLOR : PLOT_COLOR);
    if (values[i] == 0) tft.fillRect(x, 172, 7, 2, SURFACE_HI);
  }
  tft.drawFastHLine(8, 175, 304, BTN_OUTLINE);

  // Four headline figures.
  static const int cellY[2] = { 184, 210 };
  const char* labels[4] = { "FOCUS", "BLOCKS", "ACTIVE DAYS", "BEST DAY" };
  String valuesTxt[4];
  valuesTxt[0] = pomoFormatMinutes(total30);
  valuesTxt[1] = String(blocks);
  valuesTxt[2] = String(activeDays) + " of 30";
  valuesTxt[3] = bestMinutes > 0 ? String(WEEKDAY_MED[pomoWeekday(bestDay)]) + " " + pomoFormatMinutes(bestMinutes) : "-";
  for (int c = 0; c < 4; c++) {
    int cx = (c % 2 == 0) ? 10 : 162;
    int cy = cellY[c / 2];
    drawCard(cx, cy, 148, 24, false, RADIUS_SM);
    tft.setTextSize(1);
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(cx + 8, cy + 5);
    tft.print(labels[c]);
    tft.setCursor(cx + 8, cy + 14);
    tft.setTextColor(TEXT_COLOR);
    tft.print(valuesTxt[c]);
  }
}

void drawPomoStatsView() {
  tft.fillScreen(BG_COLOR);
  drawPomoTopBar();
  drawStatsTabs();
  if (statsTab == STATS_DAY) drawDayReport();
  else if (statsTab == STATS_WEEK) drawWeekReport();
  else drawMonthReport();
}

void handlePomoStatsTouch(int sx, int sy) {
  if (inRect(sx, sy, STATS_TAB_X, STATS_TAB_Y, STATS_TAB_W, STATS_TAB_H)) {
    int segW = (STATS_TAB_W - 4) / 3;
    int i = constrain((sx - (STATS_TAB_X + 2)) / segW, 0, 2);
    flashButton(STATS_TAB_X + 2 + (i * segW), STATS_TAB_Y + 2, segW, STATS_TAB_H - 4, (STATS_TAB_H - 4) / 2);
    if ((int)statsTab != i) {
      statsTab = (StatsTab)i;
      drawPomoStatsView();
    }
    return;
  }
  if (statsTab != STATS_DAY) return;
  if (inRect(sx, sy, 60, 150, 44, 26)) {
    flashButton(60, 150, 44, 26, RADIUS_SM);
    pomoDailyGoal = constrain(pomoDailyGoal - 1, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
    savePomoSettings();
    drawPomoStatsView();
    return;
  }
  if (inRect(sx, sy, 216, 150, 44, 26)) {
    flashButton(216, 150, 44, 26, RADIUS_SM);
    pomoDailyGoal = constrain(pomoDailyGoal + 1, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
    savePomoSettings();
    drawPomoStatsView();
    return;
  }
}

// ==========================================
// SCROLL CONTROL (shared by the pages above)
// ==========================================
bool pomoHandleScrollTouch(int sx, int sy) {
  if (pomoScrollMax <= 0) return false;
  if (inRect(sx, sy, SCROLL_BTN_X, SCROLL_UP_Y, SCROLL_BTN_W, SCROLL_BTN_H)) {
    if (pomoScrollY > 0) {
      flashButton(SCROLL_BTN_X, SCROLL_UP_Y, SCROLL_BTN_W, SCROLL_BTN_H, 4);
      pomoScrollY = max(0, pomoScrollY - (PAGE_BOTTOM - PAGE_TOP) / 2);
      drawPomodoroScreen(true);
    }
    return true;
  }
  if (inRect(sx, sy, SCROLL_BTN_X, SCROLL_DOWN_Y, SCROLL_BTN_W, SCROLL_BTN_H)) {
    if (pomoScrollY < pomoScrollMax) {
      flashButton(SCROLL_BTN_X, SCROLL_DOWN_Y, SCROLL_BTN_W, SCROLL_BTN_H, 4);
      pomoScrollY = min(pomoScrollMax, pomoScrollY + (PAGE_BOTTOM - PAGE_TOP) / 2);
      drawPomodoroScreen(true);
    }
    return true;
  }
  return false;
}
