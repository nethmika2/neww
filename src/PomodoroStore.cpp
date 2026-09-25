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
  prefs.putBool("pomoauto", pomoAutoStart);
  // Timer state: a reboot (or a flat battery) resumes where it left off,
  // paused, instead of silently resetting the session.
  prefs.putInt("pomomode", (int)pomoMode);
  prefs.putInt("pomoleft", pomoSeconds);
  prefs.putInt("pomotot", pomoPhaseTotal);
  prefs.putInt("pomocnt", pomodorosCompleted);
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
  // Fold duplicates: an older build could write two rows for one day, and that
  // made the totals and the day view disagree.  Merging keeps the hours.
  int out = 0;
  for (int i = 0; i < pomoHistoryCount; i++) {
    if (out > 0 && pomoHistory[out - 1].day == pomoHistory[i].day) {
      long total = (long)pomoHistory[out - 1].minutes + pomoHistory[i].minutes;
      pomoHistory[out - 1].minutes = (uint16_t)constrain(total, 0, 6000);
      pomoHistory[out - 1].blocks = (uint8_t)constrain((int)pomoHistory[out - 1].blocks + pomoHistory[i].blocks, 0, 250);
      pomoHistory[out - 1].synced = (pomoHistory[out - 1].synced || pomoHistory[i].synced) ? 1 : 0;
      continue;
    }
    if (out != i) pomoHistory[out] = pomoHistory[i];
    out++;
  }
  if (out != pomoHistoryCount) {
    Serial.printf("[I][pomo] merged %d duplicate history row(s)\n", pomoHistoryCount - out);
    for (int i = out; i < POMO_HISTORY_DAYS; i++) pomoHistory[i] = PomoDayStat();
    pomoHistoryCount = out;
    savePomoHistory();
  }
}

// Declared up front: loadPomoStore() seeds the day anchor from the clock, and
// both helpers live further down with the rest of the date handling.
static uint32_t civilToDays(int year, int month, int dayOfMonth);
static uint32_t pomoDayAnchor;
bool pomoClockValid();

void loadPomoStore() {
  pomoWorkTime = constrain(prefs.getInt("pomow", DEFAULT_WORK_TIME), MIN_WORK_MINUTES * 60, MAX_WORK_MINUTES * 60);
  pomoShortTime = constrain(prefs.getInt("pomos", DEFAULT_SHORT_BREAK), MIN_SHORT_MINUTES * 60, MAX_SHORT_MINUTES * 60);
  pomoLongTime = constrain(prefs.getInt("pomol", DEFAULT_LONG_BREAK), MIN_LONG_MINUTES * 60, MAX_LONG_MINUTES * 60);
  pomoLongEvery = constrain(prefs.getInt("pomoe", DEFAULT_CYCLES_BEFORE_LONG), MIN_CYCLES, MAX_CYCLES);
  pomoDailyGoal = constrain(prefs.getInt("pomog", DEFAULT_DAILY_GOAL), MIN_DAILY_GOAL, MAX_DAILY_GOAL);
  pomoActiveTask = prefs.getInt("pomotask", -1);
  pomoAutoStart = prefs.getBool("pomoauto", false);

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
  // The day number to use while the clock is unset: the last known date, or the
  // newest entry already in the history.
  pomoDayAnchor = prefs.getULong("histday", 0);
  if (pomoDayAnchor == 0 && pomoHistoryCount > 0) pomoDayAnchor = pomoHistory[pomoHistoryCount - 1].day;
  if (pomoClockValid()) {
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    pomoDayAnchor = civilToDays(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    prefs.putULong("histday", pomoDayAnchor);
  }

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

// The estimate is a plain number of blocks, so it goes down as well as up.
// (The original stepper only cycled upwards, which is why the count could not
// be lowered.)
void adjustPomoTaskTarget(int idx, int delta) {
  if (idx < 0 || idx >= MAX_POMO_TASKS || !pomoTasks[idx].in_use) return;
  int target = (int)pomoTasks[idx].target + delta;
  pomoTasks[idx].target = (uint8_t)constrain(target, 1, MAX_TASK_BLOCKS);
  pomoTasks[idx].done = (pomoTasks[idx].blocks >= pomoTasks[idx].target) ? 1 : 0;
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

// Day numbers are the civil DATE, local time: 1970-01-01 is 0, and each day is
// one more.  Converting the local date straight to a day number (rather than
// taking midnight and dividing by 86400) is what keeps the reports on the same
// date as the clock: east of UTC, local midnight falls on the previous UTC day,
// which used to make every label a day early.
static uint32_t civilToDays(int year, int month, int dayOfMonth) {
  long y = year - (month <= 2 ? 1 : 0);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned long yoe = (unsigned long)(y - era * 400);
  unsigned long doy = (unsigned long)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + dayOfMonth - 1);
  unsigned long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (uint32_t)(era * 146097 + (long)doe - 719468);
}

// The day number to use when the clock has never been set: the last one that was
// known (persisted), advanced by whole days of uptime so a session that runs for
// days still rolls over instead of piling everything onto one date.
uint32_t pomoTodayDay() {
  time_t now;
  time(&now);
  if (!pomoClockValid()) return pomoDayAnchor + (uint32_t)(millis() / 86400000UL);
  struct tm ti;
  localtime_r(&now, &ti);
  return civilToDays(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
}

// The inverse, for every date the reports print.
void pomoDayDate(uint32_t day, int* year, int* month, int* dayOfMonth) {
  long z = (long)day + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned long doe = (unsigned long)(z - era * 146097);
  unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long y = (long)yoe + era * 400;
  unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned long mp = (5 * doy + 2) / 153;
  unsigned long d = doy - (153 * mp + 2) / 5 + 1;
  long m = (long)mp + (mp < 10 ? 3 : -9);
  if (year) *year = (int)(y + (m <= 2 ? 1 : 0));
  if (month) *month = (int)m;
  if (dayOfMonth) *dayOfMonth = (int)d;
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

static PomoDayStat* historyGet(uint32_t day, bool create);

// Adds (or merges) minutes/blocks for one day.  Merging matters: a day can be
// reached twice - a clock that synced late, a clock-less entry re-keyed onto a
// real date - and two rows for one day made the reports disagree with each other
// (the sums added both, the day view showed the first).
static void historyAdd(uint32_t day, int minutes, int blocks, bool synced) {
  PomoDayStat* e = historyGet(day, true);
  if (!e) return;
  long total = (long)e->minutes + minutes;
  e->minutes = (uint16_t)constrain(total, 0, 6000);
  int b = (int)e->blocks + blocks;
  e->blocks = (uint8_t)constrain(b, 0, 250);
  e->synced = (synced || e->synced) ? 1 : 0;
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
  if (valid) {
    // Remember the real date so a later session without a clock continues from
    // it instead of dropping work into 1970.
    if (pomoDayAnchor != today) {
      pomoDayAnchor = today;
      prefs.putULong("histday", pomoDayAnchor);
    }
    // Entries recorded while the clock was unset are moved onto the real
    // calendar, newest to today and the ones before it stepping back a day, so
    // the work that was already logged keeps its order and its hours.
    int first = pomoHistoryCount;
    while (first > 0 && !pomoHistory[first - 1].synced) first--;
    int n = pomoHistoryCount - first;
    if (n > 0) {
      // Take the entries out and add them back onto the real calendar, so a day
      // that already has data merges instead of gaining a second row.
      PomoDayStat moved[POMO_HISTORY_DAYS];
      for (int k = 0; k < n; k++) moved[k] = pomoHistory[first + k];
      pomoHistoryCount = first;
      for (int k = 0; k < n; k++) {
        uint32_t back = (uint32_t)(n - 1 - k);
        uint32_t day = (back > today) ? 0 : today - back;
        historyAdd(day, moved[k].minutes, moved[k].blocks, true);
      }
      Serial.printf("[I][pomo] %d day(s) logged without a clock moved onto %lu\n", n,
                    (unsigned long)today);
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
  historyAdd(today, mins, countBlock ? 1 : 0, valid);
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
  Serial.printf("[I][pomo] %d min -> day %lu (%s), total %u min today\n", mins,
                (unsigned long)today, valid ? "dated" : "no clock yet",
                (unsigned)pomoStatMinutes(today));
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
