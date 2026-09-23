#pragma once
#include <Arduino.h>
#include "Types.h"

void drawPomodoroScreen(bool fullWipe);
void handlePomodoroTouch(bool touched, int sx, int sy);

// ---- shared chrome ---------------------------------------------------------
// Every Pomodoro screen keeps the same top bar: back button plus one tab per
// view, so the timer, the to-do list, the routines and the reports are always
// one tap apart.
static const int POMO_TAB_H = 30;
static const int POMO_TAB_X = 40;
static const int POMO_TAB_W = 70;
static const int POMO_VIEW_COUNT = 4;

void drawPomoTopBar();
int pomoTopBarTab(int sx);  // tab index under x on the bar, or -1
void pomoSwitchView(PomoView view);

// ---- timer screen ----------------------------------------------------------
void drawPomoTimerView(bool fullWipe);
void pomoApplyMode(PomoMode mode);
// Ends the phase on screen, credits the finished focus work and returns a
// message describing what comes next.
String pomoCompletePhase(bool natural);
void pomoSkipPhase();
void pomoSetMode(PomoMode mode);
PomoMode pomoPhaseAfter(PomoMode finished);

// ---- other screens (PomodoroViews.cpp) -------------------------------------
void drawPomoTasksView();
void drawPomoPresetsView();
void drawPomoStatsView();
void handlePomoTasksTouch(int sx, int sy);
void handlePomoPresetsTouch(int sx, int sy);
void handlePomoStatsTouch(int sx, int sy);
