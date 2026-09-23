#include "PomodoroApp.h"
#include <math.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "TouchDriver.h"
#include "PomodoroStore.h"
#include "TextInput.h"

// ==========================================
// SHARED HELPERS
// ==========================================
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

// "Wed 23 Sep" for a report header, or "day N" when the clock was never set.
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
static const int TASK_ROW_X = 8;
static const int TASK_ROW_W = 304;
static const int TASK_ROW_H = 26;
static const int TASK_ROW_Y0 = 48;
static const int TASK_BTN_Y = 208;
static const int TASK_BTN_H = 30;

static void drawPomoTaskRow(int i) {
  int y = TASK_ROW_Y0 + (i * TASK_ROW_H);
  const PomoTask& t = pomoTasks[i];
  bool active = (pomoActiveTask == i);
  tft.fillRoundRect(TASK_ROW_X, y, TASK_ROW_W, TASK_ROW_H - 2, RADIUS_SM, active ? SURFACE_HI : SURFACE_COLOR);
  tft.drawRoundRect(TASK_ROW_X, y, TASK_ROW_W, TASK_ROW_H - 2, RADIUS_SM, active ? ACCENT_COLOR : BTN_OUTLINE);
  // Check box
  int cbx = TASK_ROW_X + 18, cby = y + 12;
  if (t.done) {
    tft.fillCircle(cbx, cby, 8, PLOT_COLOR);
    drawCheckMark(cbx, cby, BG_COLOR);
  } else {
    tft.drawCircle(cbx, cby, 8, MUTED_COLOR);
    tft.drawCircle(cbx, cby, 7, MUTED_COLOR);
  }
  // Title
  String title = truncated(t.text, 20);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(t.done ? MUTED_COLOR : TEXT_COLOR);
  tft.setCursor(TASK_ROW_X + 34, y + 16);
  tft.print(title);
  tft.setFont(NULL);
  // Progress "1/2" doubles as the estimate button
  tft.setTextSize(1);
  tft.setTextColor(t.done ? PLOT_COLOR : ACCENT_COLOR);
  tft.setCursor(TASK_ROW_X + 226, y + 8);
  tft.print(String(t.blocks) + "/" + String(t.target));
  if (active) {
    tft.fillTriangle(TASK_ROW_X + 12, y + 9, TASK_ROW_X + 12, y + 15, TASK_ROW_X + 5, y + 12, ACCENT_COLOR);
  }
  // Delete
  drawModernButton(TASK_ROW_X + 262, y + 3, 30, 18, 4, DEL_COLOR, false);
  printCentered("X", TASK_ROW_X + 277, y + 16, &FreeSans9pt7b, TEXT_COLOR);
}

void drawPomoTasksView() {
  tft.fillScreen(BG_COLOR);
  drawPomoTopBar();
  int used = 0;
  for (int i = 0; i < MAX_POMO_TASKS; i++)
    if (pomoTasks[i].in_use) used++;
  printCentered("row = focus   o = done   n/m = estimate", 160, 42, NULL, MUTED_COLOR);
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    drawPomoTaskRow(i);
  }
  if (used == 0) printCentered("Empty - add the first thing to do", 160, 110, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(8, TASK_BTN_Y, 148, TASK_BTN_H, RADIUS_MD, PLOT_COLOR, true);
  printCentered("ADD TASK", 82, TASK_BTN_Y + 20, &FreeSansBold9pt7b, TEXT_COLOR);
  drawModernButton(164, TASK_BTN_Y, 148, TASK_BTN_H, RADIUS_MD, SURFACE_COLOR, true);
  printCentered("CLEAR DONE", 238, TASK_BTN_Y + 20, &FreeSans9pt7b, TEXT_COLOR);
}

void handlePomoTasksTouch(int sx, int sy) {
  if (inRect(sx, sy, 8, TASK_BTN_Y, 148, TASK_BTN_H)) {
    flashButton(8, TASK_BTN_Y, 148, TASK_BTN_H, RADIUS_MD);
    startTextInput("NEW TASK", "", TEXT_TARGET_TASK, POMO_NAME_LEN);
    return;
  }
  if (inRect(sx, sy, 164, TASK_BTN_Y, 148, TASK_BTN_H)) {
    flashButton(164, TASK_BTN_Y, 148, TASK_BTN_H, RADIUS_MD);
    clearDonePomoTasks();
    drawPomoTasksView();
    return;
  }
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    int y = TASK_ROW_Y0 + (i * TASK_ROW_H);
    if (!inRect(sx, sy, TASK_ROW_X, y, TASK_ROW_W, TASK_ROW_H - 2)) continue;
    if (inRect(sx, sy, TASK_ROW_X + 2, y, 32, TASK_ROW_H - 2)) {
      flashButton(TASK_ROW_X + 2, y, 32, TASK_ROW_H - 2, RADIUS_SM);
      togglePomoTaskDone(i);
      drawPomoTasksView();
    } else if (inRect(sx, sy, TASK_ROW_X + 216, y, 40, TASK_ROW_H - 2)) {
      flashButton(TASK_ROW_X + 216, y, 40, TASK_ROW_H - 2, RADIUS_SM);
      cyclePomoTaskTarget(i);
      showToast("Estimate: " + String(pomoTasks[i].target) + " block(s)");
      drawPomoTasksView();
    } else if (inRect(sx, sy, TASK_ROW_X + 258, y, 40, TASK_ROW_H - 2)) {
      flashButton(TASK_ROW_X + 258, y, 40, TASK_ROW_H - 2, RADIUS_SM);
      deletePomoTask(i);
      drawPomoTasksView();
    } else {
      flashButton(TASK_ROW_X, y, TASK_ROW_W, TASK_ROW_H - 2, RADIUS_SM);
      if (pomoTasks[i].done) togglePomoTaskDone(i);
      pomoActiveTask = i;
      savePomoSettings();
      drawPomoTasksView();
    }
    return;
  }
}

// ==========================================
// ROUTINES (TEMPLATES)
// ==========================================
static const int PRE_CELL_W = 150;
static const int PRE_CELL_H = 36;
static const int PRE_ROW_H = 22;
static const int PRE_ROW_Y0 = 148;

// One editable duration: label, "25m" and a pair of steppers.
static void drawPresetCell(int x, int y, const char* label, int value, bool minutes) {
  drawModernButton(x, y, PRE_CELL_W, PRE_CELL_H, RADIUS_SM, SURFACE_COLOR, false);
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(x + 8, y + 21);
  tft.print(label);
  String text = minutes ? String(value) + "m" : "x" + String(value);
  printCentered(text, x + 62, y + 24, &FreeSansBold9pt7b, TEXT_COLOR);
  drawModernButton(x + 96, y + 6, 24, 24, 4, SURFACE_HI, false);
  drawMinusIcon(x + 108, y + 18, TEXT_COLOR);
  drawModernButton(x + 124, y + 6, 24, 24, 4, SURFACE_HI, false);
  drawPlusIcon(x + 136, y + 18, TEXT_COLOR);
}

static void drawPomoPresetRow(int i) {
  int y = PRE_ROW_Y0 + (i * PRE_ROW_H);
  const PomoTemplate& t = pomoTemplates[i];
  tft.fillRoundRect(8, y, 304, PRE_ROW_H - 2, RADIUS_SM, SURFACE_COLOR);
  tft.drawRoundRect(8, y, 304, PRE_ROW_H - 2, RADIUS_SM, BTN_OUTLINE);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(16, y + 16);
  tft.print(truncated(t.name, 18));
  tft.setFont(NULL);
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(166, y + 8);
  tft.print(pomoTemplateSummary(t));
  drawModernButton(282, y + 2, 26, 16, 3, DEL_COLOR, false);
  printCentered("X", 295, y + 14, NULL, TEXT_COLOR);
}

void drawPomoPresetsView() {
  tft.fillScreen(BG_COLOR);
  drawPomoTopBar();
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(10, 46);
  tft.print("CURRENT ROUTINE");
  drawPresetCell(8, 52, "WORK", pomoWorkTime / 60, true);
  drawPresetCell(162, 52, "SHORT", pomoShortTime / 60, true);
  drawPresetCell(8, 92, "LONG", pomoLongTime / 60, true);
  drawPresetCell(162, 92, "CYCLES", pomoLongEvery, false);

  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(10, 142);
  tft.print("SAVED ROUTINES");
  drawModernButton(202, 126, 110, 24, RADIUS_SM, PLOT_COLOR, false);
  printCentered("SAVE CURRENT", 257, 143, &FreeSans9pt7b, TEXT_COLOR);

  int used = 0;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (!pomoTemplates[i].in_use) continue;
    drawPomoPresetRow(i);
    used++;
  }
  if (used == 0) printCentered("Nothing saved yet - tap SAVE CURRENT", 160, 178, &FreeSans9pt7b, MUTED_COLOR);
}

void handlePomoPresetsTouch(int sx, int sy) {
  // Duration steppers
  for (int cell = 0; cell < 4; cell++) {
    int x = (cell % 2 == 0) ? 8 : 162;
    int y = (cell < 2) ? 52 : 92;
    int dir = 0;
    if (inRect(sx, sy, x + 96, y + 6, 24, 24)) dir = -1;
    else if (inRect(sx, sy, x + 124, y + 6, 24, 24)) dir = 1;
    if (dir == 0) continue;
    flashButton(x + (dir < 0 ? 96 : 124), y + 6, 24, 24, 4);
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
  if (inRect(sx, sy, 202, 126, 110, 24)) {
    flashButton(202, 126, 110, 24, RADIUS_SM);
    startTextInput("SAVE ROUTINE", "", TEXT_TARGET_TEMPLATE, POMO_NAME_LEN);
    return;
  }
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (!pomoTemplates[i].in_use) continue;
    int y = PRE_ROW_Y0 + (i * PRE_ROW_H);
    if (!inRect(sx, sy, 8, y, 304, PRE_ROW_H - 2)) continue;
    if (inRect(sx, sy, 278, y, 34, PRE_ROW_H - 2)) {
      flashButton(278, y, 34, PRE_ROW_H - 2, RADIUS_SM);
      String name = pomoTemplates[i].name;
      deletePomoTemplate(i);
      showToast("Removed " + truncated(name, 14));
      drawPomoPresetsView();
    } else {
      flashButton(8, y, 304, PRE_ROW_H - 2, RADIUS_SM);
      applyPomoTemplate(i);
      showToast("Loaded " + truncated(pomoTemplates[i].name, 14));
      pomoView = POMO_VIEW_TIMER;
      drawPomodoroScreen(true);
    }
    return;
  }
}

// ==========================================
// REPORTS
// ==========================================
static const int STATS_TAB_Y = 34;
static const int STATS_TAB_H = 24;

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
  for (int i = 0; i < 3; i++) {
    int x = 8 + (i * 103);
    bool active = ((int)statsTab == i);
    drawModernButton(x, STATS_TAB_Y, 96, STATS_TAB_H, RADIUS_SM, active ? ACCENT_COLOR : SURFACE_COLOR, false);
    printCentered(labels[i], x + 48, STATS_TAB_Y + 17, &FreeSans9pt7b, active ? BG_COLOR : TEXT_COLOR);
  }
}

static void drawDayReport() {
  uint32_t today = pomoTodayDay();
  int minutes = pomoStatMinutes(today);
  int blocks = pomoStatBlocks(today);
  int streak = pomoStreakDays();

  printCentered(pomoDateLabel(today), 160, 72, &FreeSans9pt7b, MUTED_COLOR);
  printCentered(pomoFormatMinutes(minutes), 160, 104, &FreeSansBold24pt7b, minutes > 0 ? TEXT_COLOR : MUTED_COLOR);
  String sub = String(blocks) + " block" + (blocks == 1 ? "" : "s") + "   -   streak " + String(streak) + "d";
  printCentered(sub, 160, 124, &FreeSans9pt7b, MUTED_COLOR);

  // Daily goal: a progress bar with the steppers that change the target.
  int goal = constrain(pomoDailyGoal, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
  float pct = constrain((float)blocks / (float)goal, 0.0f, 1.0f);
  bool reached = blocks >= goal;
  tft.fillRoundRect(20, 130, 280, 14, 7, SURFACE_COLOR);
  int fillW = (int)(pct * 276);
  if (fillW > 0) tft.fillRoundRect(22, 132, fillW, 10, 5, reached ? PLOT_COLOR : ACCENT_COLOR);
  drawModernButton(20, 152, 46, 28, RADIUS_SM, SURFACE_HI, false);
  drawMinusIcon(43, 166, TEXT_COLOR);
  drawModernButton(254, 152, 46, 28, RADIUS_SM, SURFACE_HI, false);
  drawPlusIcon(277, 166, TEXT_COLOR);
  printCentered("GOAL " + String(goal) + " BLOCKS" + (reached ? " DONE" : ""), 160, 171, &FreeSans9pt7b, reached ? PLOT_COLOR : TEXT_COLOR);

  // Hour by hour distribution of today's focus time.
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(10, 197);
  tft.print("FOCUS BY HOUR");
  int maxH = 0;
  for (int h = 0; h < 24; h++) maxH = max(maxH, (int)pomoHourly[h]);
  int scale = chartScale(maxH);
  for (int h = 0; h < 24; h++) {
    int x = 16 + (h * 12);
    int barH = (int)(((float)pomoHourly[h] / scale) * 20.0f);
    if (pomoHourly[h] > 0) barH = max(barH, 2);
    tft.fillRect(x, 230 - barH, 9, barH, pomoHourly[h] > 0 ? ACCENT_COLOR : SURFACE_COLOR);
    if (h % 6 == 0) {
      tft.setCursor(x, 231);
      tft.setTextColor(MUTED_COLOR);
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

  printCentered("LAST 7 DAYS", 160, 74, &FreeSans9pt7b, MUTED_COLOR);
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
    if (values[i] > 0) {
      printCentered(String(values[i]), x + (barW / 2), baseY - h - 4, NULL, MUTED_COLOR);
    }
    tft.drawFastHLine(24, baseY + 1, 272, BTN_OUTLINE);
    long day = (long)today - (6 - i);
    String label = (day < 0 || !pomoClockValid()) ? String("d") + String(i) : String(WEEKDAY_SHORT[pomoWeekday((uint32_t)day)]);
    printCentered(label, x + (barW / 2), baseY + 16, &FreeSans9pt7b, i == 6 ? TEXT_COLOR : MUTED_COLOR);
    if (day >= 0 && pomoClockValid()) {
      int y2, m2, d2;
      civilFromDays((uint32_t)day, y2, m2, d2);
      printCentered(String(d2), x + (barW / 2), baseY + 27, NULL, MUTED_COLOR);
    }
  }
  printCentered("TOTAL " + pomoFormatMinutes(total) + "  -  " + String(blocks) + " blocks", 160, 220, &FreeSans9pt7b, TEXT_COLOR);
  String footer = "active " + String(activeDays) + "d  -  avg " + pomoFormatMinutes(activeDays ? total / activeDays : 0);
  if (bestMinutes > 0) footer += "  -  best " + String(WEEKDAY_MED[pomoWeekday(bestDay)]) + " " + pomoFormatMinutes(bestMinutes);
  printCentered(footer, 160, 234, NULL, MUTED_COLOR);
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
  printCentered("LAST 30 DAYS", 160, 74, &FreeSans9pt7b, MUTED_COLOR);
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
  static const int cellY[2] = { 186, 212 };
  const char* labels[4] = { "FOCUS", "BLOCKS", "ACTIVE DAYS", "BEST DAY" };
  String valuesTxt[4];
  valuesTxt[0] = pomoFormatMinutes(total30);
  valuesTxt[1] = String(blocks);
  valuesTxt[2] = String(activeDays) + " of 30";
  valuesTxt[3] = bestMinutes > 0 ? String(WEEKDAY_MED[pomoWeekday(bestDay)]) + " " + pomoFormatMinutes(bestMinutes) : "-";
  for (int c = 0; c < 4; c++) {
    int cx = (c % 2 == 0) ? 10 : 162;
    int cy = cellY[c / 2];
    tft.fillRoundRect(cx, cy, 148, 24, RADIUS_SM, SURFACE_COLOR);
    tft.setTextSize(1);
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(cx + 8, cy + 8);
    tft.print(labels[c]);
    tft.setCursor(cx + 8, cy + 18);
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
  for (int i = 0; i < 3; i++) {
    int x = 8 + (i * 103);
    if (!inRect(sx, sy, x, STATS_TAB_Y, 96, STATS_TAB_H)) continue;
    flashButton(x, STATS_TAB_Y, 96, STATS_TAB_H, RADIUS_SM);
    if ((int)statsTab != i) {
      statsTab = (StatsTab)i;
      drawPomoStatsView();
    }
    return;
  }
  if (statsTab != STATS_DAY) return;
  if (inRect(sx, sy, 20, 156, 46, 36)) {
    flashButton(20, 156, 46, 36, RADIUS_SM);
    pomoDailyGoal = constrain(pomoDailyGoal - 1, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
    savePomoSettings();
    drawPomoStatsView();
    return;
  }
  if (inRect(sx, sy, 254, 156, 46, 36)) {
    flashButton(254, 156, 46, 36, RADIUS_SM);
    pomoDailyGoal = constrain(pomoDailyGoal + 1, MIN_DAILY_GOAL, MAX_DAILY_GOAL);
    savePomoSettings();
    drawPomoStatsView();
    return;
  }
}
