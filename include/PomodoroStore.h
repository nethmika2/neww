#pragma once
#include <Arduino.h>
#include "Types.h"

// ==========================================
// POMODORO DATA: routines, to-do items, focus history
// ==========================================
// Everything here is persisted in NVS so the timer, the to-do list and the
// reports survive a reboot.

void loadPomoStore();      // settings + routines + to-do items + history
void savePomoSettings();   // durations, cycle count, goal, active item
void savePomoTemplates();
void savePomoTasks();
void savePomoHistory();

// ---- routines (templates) ----
int addPomoTemplate(const String& name);  // -1 when the list is full
void deletePomoTemplate(int idx);
void applyPomoTemplate(int idx);
String pomoTemplateSummary(const PomoTemplate& t);

// ---- to-do items ----
int addPomoTask(const String& text);  // -1 when the list is full
void deletePomoTask(int idx);
void togglePomoTaskDone(int idx);
void adjustPomoTaskTarget(int idx, int delta);  // clamped to 1..MAX_TASK_BLOCKS
void cyclePomoTaskTarget(int idx);
void clearDonePomoTasks();
void pomoCreditActiveTask();  // one more finished focus block for the active item
String pomoActiveTaskLabel();

// ---- focus history / reports ----
bool pomoClockValid();
uint32_t pomoTodayDay();
// The calendar date of a day number (both are local civil days, 1970-01-01 = 0),
// so every printed date and the stored history agree.
void pomoDayDate(uint32_t day, int* year, int* month, int* dayOfMonth);
void pomoEnsureToday();  // day rollover + clock-sync re-keying
// Credits finished focus time.  countBlock is false for a short skipped
// block, which still counts towards the minutes but not to the block tally.
void pomoRecordWorkBlock(int seconds, bool countBlock);
int pomoStatMinutes(uint32_t day);
int pomoStatBlocks(uint32_t day);
// Offsets are relative to today: 0 = today, -6 = six days ago.
int pomoSumMinutes(int fromOffset, int toOffset, int& blocks);
int pomoActiveDays(int fromOffset, int toOffset);
int pomoBestDay(int fromOffset, int toOffset, int& bestMinutes);
int pomoWeekday(uint32_t day);  // 0 = Sunday
String pomoFormatMinutes(int minutes);
