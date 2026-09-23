#include "PomodoroStore.h"
#include <time.h>
#include "Globals.h"
#include "TimeService.h"
#include "PomodoroApp.h"

// ==========================================
// PERSISTENCE
// ==========================================
static void saveHourly() {
  prefs.putBytes("hrs", pomoHourly, sizeof(pomoHourly));
  prefs.putULong("hrsd", pomoHourlyDay);
}

void savePomoSettings() {
  prefs.putInt("pomow", pomoWorkTime);
  prefs.putInt("pomos", pomoShortTime);
  prefs.putInt("pomol", pomoLongTime);
  prefs.putInt("pomoe", pomoLongEvery);
  prefs.putInt("pomog", pomoDailyGoal);
  prefs.putInt("pomotask", pomoActiveTask);
}

void savePomoTemplates() {
  int count = 0;
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (!pomoTemplates[i].in_use) continue;
    String p = "tp" + String(count);
    prefs.putString((p + "n").c_str(), pomoTemplates[i].name);
    prefs.putInt((p + "w").c_str(), pomoTemplates[i].work);
    prefs.putInt((p + "s").c_str(), pomoTemplates[i].shortBreak);
    prefs.putInt((p + "l").c_str(), pomoTemplates[i].longBreak);
    prefs.putInt((p + "c").c_str(), pomoTemplates[i].cycles);
    count++;
  }
  prefs.putInt("tpN", count);
}

void savePomoTasks() {
  int count = 0;
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (!pomoTasks[i].in_use) continue;
    String p = "tk" + String(count);
    prefs.putString((p + "x").c_str(), pomoTasks[i].text);
    prefs.putInt((p + "d").c_str(), pomoTasks[i].done);
    prefs.putInt((p + "b").c_str(), pomoTasks[i].blocks);
    prefs.putInt((p + "g").c_str(), pomoTasks[i].target);
    count++;
  }
  prefs.putInt("tkN", count);
}

void savePomoHistory() {
  // Eight bytes per day: day number, focused minutes, finished blocks and a
  // flag telling whether the clock was already valid when it was recorded.
  static uint8_t buf[POMO_HISTORY_DAYS * 8];
  int n = 0;
  for (int i = 0; i < pomoHistoryCount; i++) {
    if (!pomoHistory[i].in_use) continue;
    int o = n * 8;
    uint32_t d = pomoHistory[i].day;
    buf[o] = (uint8_t)(d & 0xFF);
    buf[o + 1] = (uint8_t)((d >> 8) & 0xFF);
    buf[o + 2] = (uint8_t)((d >> 16) & 0xFF);
    buf[o + 3] = (uint8_t)((d >> 24) & 0xFF);
    buf[o + 4] = (uint8_t)(pomoHistory[i].minutes & 0xFF);
    buf[o + 5] = (uint8_t)((pomoHistory[i].minutes >> 8) & 0xFF);
    buf[o + 6] = pomoHistory[i].blocks;
    buf[o + 7] = pomoHistory[i].synced;
    n++;
  }
  prefs.putInt("histN", n);
  prefs.putBytes("hist", buf, n * 8);
}

static void loadHistory() {
  pomoHistoryCount = 0;
  for (int i = 0; i < POMO_HISTORY_DAYS; i++) pomoHistory[i] = PomoDayStat();
  int n = constrain(prefs.getInt("histN", 0), 0, POMO_HISTORY_DAYS);
  if (n <= 0) return;
  static uint8_t buf[POMO_HISTORY_DAYS * 8];
  size_t got = prefs.getBytes("hist", buf, sizeof(buf));
  int entries = min((int)(got / 8), n);
  for (int i = 0; i < entries; i++) {
    int o = i * 8;
    PomoDayStat s;
    s.day = (uint32_t)buf[o] | ((uint32_t)buf[o + 1] << 8) | ((uint32_t)buf[o + 2] << 16) | ((uint32_t)buf[o + 3] << 24);
    s.minutes = (uint16_t)(buf[o + 4] | (buf[o + 5] << 8));
    s.blocks = buf[o + 6];
    s.synced = buf[o + 7] ? 1 : 0;
    s.in_use = true;
    pomoHistory[i] = s;
  }
  pomoHistoryCount = entries;
  // Keep the list sorted: the clock may have jumped between boots.
  for (int i = 1; i < pomoHistoryCount; i++) {
    PomoDayStat key = pomoHistory[i];
    int j = i - 1;
    while (j >= 0 && pomoHistory[j].day > key.day) {
      pomoHistory[j + 1] = pomoHistory[j];
      j--;
    }
    pomoHistory[j + 1] = key;
  }
}

void loadPomoStore() {
  pomoWorkTime = constrain(prefs.getInt("pomow", DEFAULT_WORK_TIME), MIN_WORK_MINUTES * 60, MAX_WORK_MINUTES * 60);
  pomoShortTime = constrain(prefs.getInt("pomos", DEFAULT_SHORT_BREAK), MIN_SHORT_MINUTES * 60, MAX_SHORT_MINUTES * 60);
  pomoLongTime = constrain(prefs.getInt("pomol", DEFAULT_LONG_BREAK), MIN_LONG_MINUTES * 60, MAX_LONG_MINUTES * 60);
  pomoLongEvery = constrain(prefs.getInt("pomoe", DEFAULT_CYCLES_BEFORE_LONG), MIN_CYCLES, MAX_CYCLES);
  pomoDailyGoal = constrain(prefs.getInt("pomog", DEFAULT_DAILY_GOAL), MIN_DAILY_GOAL, MAX_DAILY_GOAL);
  pomoActiveTask = prefs.getInt("pomotask", -1);

  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    String p = "tp" + String(i);
    PomoTemplate t;
    if (i >= prefs.getInt("tpN", 0)) break;
    t.name = prefs.getString((p + "n").c_str(), "");
    t.work = constrain(prefs.getInt((p + "w").c_str(), 25), MIN_WORK_MINUTES, MAX_WORK_MINUTES);
    t.shortBreak = constrain(prefs.getInt((p + "s").c_str(), 5), MIN_SHORT_MINUTES, MAX_SHORT_MINUTES);
    t.longBreak = constrain(prefs.getInt((p + "l").c_str(), 15), MIN_LONG_MINUTES, MAX_LONG_MINUTES);
    t.cycles = constrain(prefs.getInt((p + "c").c_str(), 4), MIN_CYCLES, MAX_CYCLES);
    t.in_use = t.name.length() > 0;
    pomoTemplates[i] = t;
  }

  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    String p = "tk" + String(i);
    PomoTask t;
    if (i >= prefs.getInt("tkN", 0)) break;
    t.text = prefs.getString((p + "x").c_str(), "");
    t.done = prefs.getInt((p + "d").c_str(), 0) ? 1 : 0;
    t.blocks = constrain(prefs.getInt((p + "b").c_str(), 0), 0, 99);
    t.target = constrain(prefs.getInt((p + "g").c_str(), 1), 1, 9);
    t.in_use = t.text.length() > 0;
    pomoTasks[i] = t;
  }
  if (pomoActiveTask >= MAX_POMO_TASKS || (pomoActiveTask >= 0 && !pomoTasks[pomoActiveTask].in_use)) pomoActiveTask = -1;

  loadHistory();

  pomoHourlyDay = prefs.getULong("hrsd", 0xFFFFFFFFUL);
  size_t got = prefs.getBytes("hrs", pomoHourly, sizeof(pomoHourly));
  if (got != sizeof(pomoHourly)) {
    memset(pomoHourly, 0, sizeof(pomoHourly));
    pomoHourlyDay = 0xFFFFFFFFUL;
  }
  pomoEnsureToday();
}

// ==========================================
// ROUTINES
// ==========================================
String pomoTemplateSummary(const PomoTemplate& t) {
  return String(t.work) + "/" + String(t.shortBreak) + "/" + String(t.longBreak) + " x" + String(t.cycles);
}

int addPomoTemplate(const String& name) {
  for (int i = 0; i < MAX_POMO_TEMPLATES; i++) {
    if (pomoTemplates[i].in_use) continue;
    pomoTemplates[i].name = name;
    pomoTemplates[i].work = pomoWorkTime / 60;
    pomoTemplates[i].shortBreak = pomoShortTime / 60;
    pomoTemplates[i].longBreak = pomoLongTime / 60;
    pomoTemplates[i].cycles = pomoLongEvery;
    pomoTemplates[i].in_use = true;
    savePomoTemplates();
    return i;
  }
  return -1;
}

void deletePomoTemplate(int idx) {
  if (idx < 0 || idx >= MAX_POMO_TEMPLATES || !pomoTemplates[idx].in_use) return;
  for (int i = idx; i < MAX_POMO_TEMPLATES - 1; i++) pomoTemplates[i] = pomoTemplates[i + 1];
  pomoTemplates[MAX_POMO_TEMPLATES - 1] = PomoTemplate();
  savePomoTemplates();
}

void applyPomoTemplate(int idx) {
  if (idx < 0 || idx >= MAX_POMO_TEMPLATES || !pomoTemplates[idx].in_use) return;
  const PomoTemplate& t = pomoTemplates[idx];
  pomoWorkTime = constrain((int)t.work, MIN_WORK_MINUTES, MAX_WORK_MINUTES) * 60;
  pomoShortTime = constrain((int)t.shortBreak, MIN_SHORT_MINUTES, MAX_SHORT_MINUTES) * 60;
  pomoLongTime = constrain((int)t.longBreak, MIN_LONG_MINUTES, MAX_LONG_MINUTES) * 60;
  pomoLongEvery = constrain((int)t.cycles, MIN_CYCLES, MAX_CYCLES);
  // A new routine starts a fresh cycle rather than resuming a half finished
  // break from the previous one.
  pomoRunning = false;
  pomodorosCompleted = 0;
  pomoApplyMode(MODE_WORK);
  savePomoSettings();
}

// ==========================================
// TO-DO ITEMS
// ==========================================
int addPomoTask(const String& text) {
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (pomoTasks[i].in_use) continue;
    pomoTasks[i].text = text;
    pomoTasks[i].done = 0;
    pomoTasks[i].blocks = 0;
    pomoTasks[i].target = 1;
    pomoTasks[i].in_use = true;
    if (pomoActiveTask < 0) {
      pomoActiveTask = i;
      savePomoSettings();
    }
    savePomoTasks();
    return i;
  }
  return -1;
}

void deletePomoTask(int idx) {
  if (idx < 0 || idx >= MAX_POMO_TASKS || !pomoTasks[idx].in_use) return;
  for (int i = idx; i < MAX_POMO_TASKS - 1; i++) pomoTasks[i] = pomoTasks[i + 1];
  pomoTasks[MAX_POMO_TASKS - 1] = PomoTask();
  if (pomoActiveTask == idx) pomoActiveTask = -1;
  else if (pomoActiveTask > idx) pomoActiveTask--;
  // Fall back to the first item that is still open.
  if (pomoActiveTask < 0) {
    for (int i = 0; i < MAX_POMO_TASKS; i++)
      if (pomoTasks[i].in_use && !pomoTasks[i].done) {
        pomoActiveTask = i;
        break;
      }
  }
  savePomoTasks();
  savePomoSettings();
}

void togglePomoTaskDone(int idx) {
  if (idx < 0 || idx >= MAX_POMO_TASKS || !pomoTasks[idx].in_use) return;
  pomoTasks[idx].done = pomoTasks[idx].done ? 0 : 1;
  if (pomoTasks[idx].done && pomoActiveTask == idx) {
    pomoActiveTask = -1;
    for (int i = 0; i < MAX_POMO_TASKS; i++)
      if (pomoTasks[i].in_use && !pomoTasks[i].done) {
        pomoActiveTask = i;
        break;
      }
    savePomoSettings();
  }
  savePomoTasks();
}

void cyclePomoTaskTarget(int idx) {
  if (idx < 0 || idx >= MAX_POMO_TASKS || !pomoTasks[idx].in_use) return;
  pomoTasks[idx].target = (pomoTasks[idx].target % 9) + 1;
  pomoTasks[idx].done = (pomoTasks[idx].blocks >= pomoTasks[idx].target) ? 1 : 0;
  savePomoTasks();
}

void clearDonePomoTasks() {
  int removed = 0;
  for (int i = 0; i < MAX_POMO_TASKS; i++) {
    if (pomoTasks[i].in_use && pomoTasks[i].done) {
      deletePomoTask(i);
      i--;
      removed++;
    }
  }
  if (removed == 0) return;
}

void pomoCreditActiveTask() {
  if (pomoActiveTask < 0 || pomoActiveTask >= MAX_POMO_TASKS) return;
  PomoTask& t = pomoTasks[pomoActiveTask];
  if (!t.in_use) {
    pomoActiveTask = -1;
    savePomoSettings();
    return;
  }
  if (t.blocks < 99) t.blocks++;
  // Hitting the estimate ticks the box; the check mark stays tappable so the
  // user can still correct it by hand.
  if (t.blocks >= t.target) t.done = 1;
  savePomoTasks();
}

String pomoActiveTaskLabel() {
  if (pomoActiveTask < 0 || pomoActiveTask >= MAX_POMO_TASKS) return "";
  const PomoTask& t = pomoTasks[pomoActiveTask];
  if (!t.in_use) return "";
  return t.text + "  " + String(t.blocks) + "/" + String(t.target);
}

// ==========================================
// FOCUS HISTORY / REPORTS
// ==========================================
bool pomoClockValid() {
  int h, m;
  return getClock(h, m);
}

uint32_t pomoTodayDay() {
  time_t now;
  time(&now);
  struct tm ti;
  localtime_r(&now, &ti);
  ti.tm_hour = 0;
  ti.tm_min = 0;
  ti.tm_sec = 0;
  time_t midnight = mktime(&ti);
  if (midnight <= 0) return 0;
  return (uint32_t)(midnight / 86400);
}

int pomoWeekday(uint32_t day) {
  // 1970-01-01 was a Thursday, so the modulo below matches tm_wday.
  return (int)((day + 4) % 7);
}

static int historyIndex(uint32_t day) {
  for (int i = 0; i < pomoHistoryCount; i++)
    if (pomoHistory[i].in_use && pomoHistory[i].day == day) return i;
  return -1;
}

static PomoDayStat* historyGet(uint32_t day, bool create) {
  int existing = historyIndex(day);
  if (existing >= 0) return &pomoHistory[existing];
  if (!create) return nullptr;
  if (pomoHistoryCount >= POMO_HISTORY_DAYS) {
    // Drop the oldest day so the table keeps a rolling three month window.
    for (int i = 0; i < POMO_HISTORY_DAYS - 1; i++) pomoHistory[i] = pomoHistory[i + 1];
    pomoHistoryCount = POMO_HISTORY_DAYS - 1;
  }
  PomoDayStat s;
  s.day = day;
  s.in_use = true;
  pomoHistory[pomoHistoryCount++] = s;
  return &pomoHistory[pomoHistoryCount - 1];
}

void pomoEnsureToday() {
  uint32_t today = pomoTodayDay();
  bool valid = pomoClockValid();
  // Entries recorded before the clock was ever synced carry an uptime day
  // number.  As soon as a real date is available, move the newest of them onto
  // today instead of orphaning the work that was already logged.
  if (valid && pomoHistoryCount > 0) {
    PomoDayStat& last = pomoHistory[pomoHistoryCount - 1];
    if (last.in_use && !last.synced) {
      if (last.day != today) last.day = today;
      last.synced = 1;
      savePomoHistory();
    }
  }
  if (pomoHourlyDay != today) {
    memset(pomoHourly, 0, sizeof(pomoHourly));
    pomoHourlyDay = today;
    prefs.putBytes("hrs", pomoHourly, sizeof(pomoHourly));
    prefs.putULong("hrsd", pomoHourlyDay);
  }
}

void pomoRecordWorkBlock(int seconds, bool countBlock) {
  uint32_t today = pomoTodayDay();
  bool valid = pomoClockValid();
  int mins = (seconds + 59) / 60;
  if (mins < 1) mins = 1;
  PomoDayStat* e = historyGet(today, true);
  if (e) {
    long total = (long)e->minutes + mins;
    e->minutes = (uint16_t)constrain(total, 0, 6000);
    int blocks = (int)e->blocks + (countBlock ? 1 : 0);
    e->blocks = (uint8_t)constrain(blocks, 0, 250);
    e->synced = valid ? 1 : 0;
  }
  int h, m;
  if (getClock(h, m) && h >= 0 && h < 24) {
    if (pomoHourlyDay != today) {
      memset(pomoHourly, 0, sizeof(pomoHourly));
      pomoHourlyDay = today;
    }
    int v = (int)pomoHourly[h] + mins;
    pomoHourly[h] = (uint8_t)constrain(v, 0, 255);
    prefs.putBytes("hrs", pomoHourly, sizeof(pomoHourly));
    prefs.putULong("hrsd", pomoHourlyDay);
  }
  savePomoHistory();
}

int pomoStatMinutes(uint32_t day) {
  int i = historyIndex(day);
  return i < 0 ? 0 : pomoHistory[i].minutes;
}

int pomoStatBlocks(uint32_t day) {
  int i = historyIndex(day);
  return i < 0 ? 0 : pomoHistory[i].blocks;
}

int pomoSumMinutes(int fromOffset, int toOffset, int& blocks) {
  uint32_t today = pomoTodayDay();
  int total = 0;
  blocks = 0;
  for (int i = 0; i < pomoHistoryCount; i++) {
    if (!pomoHistory[i].in_use) continue;
    // Compare through signed offsets so a day number of 0 (clock never set)
    // cannot underflow into a huge "future" day.
    long off = (long)pomoHistory[i].day - (long)today;
    if (off < fromOffset || off > toOffset) continue;
    total += pomoHistory[i].minutes;
    blocks += pomoHistory[i].blocks;
  }
  return total;
}

int pomoActiveDays(int fromOffset, int toOffset) {
  uint32_t today = pomoTodayDay();
  int days = 0;
  for (int i = 0; i < pomoHistoryCount; i++) {
    if (!pomoHistory[i].in_use || pomoHistory[i].minutes <= 0) continue;
    long off = (long)pomoHistory[i].day - (long)today;
    if (off < fromOffset || off > toOffset) continue;
    days++;
  }
  return days;
}

int pomoBestDay(int fromOffset, int toOffset, int& bestMinutes) {
  uint32_t today = pomoTodayDay();
  bestMinutes = 0;
  uint32_t best = 0;
  for (int i = 0; i < pomoHistoryCount; i++) {
    if (!pomoHistory[i].in_use) continue;
    long off = (long)pomoHistory[i].day - (long)today;
    if (off < fromOffset || off > toOffset) continue;
    if (pomoHistory[i].minutes > bestMinutes) {
      bestMinutes = pomoHistory[i].minutes;
      best = pomoHistory[i].day;
    }
  }
  return (int)best;
}

String pomoFormatMinutes(int minutes) {
  if (minutes < 60) return String(minutes) + "m";
  int h = minutes / 60, m = minutes % 60;
  if (m == 0) return String(h) + "h";
  return String(h) + "h " + String(m) + "m";
}
