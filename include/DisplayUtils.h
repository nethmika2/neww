#pragma once
#include <Arduino.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "Globals.h"

// Screen power and screensaver
bool displayActive();
void setScreenPower(bool on);
void redrawCurrentScreen();
void drawScreensaver();
void updateScreensaver();

// UI & geometry helpers
bool inRect(int px, int py, int rx, int ry, int rw, int rh);
uint16_t brighten565(uint16_t c, int amt);
void printCentered(String text, int centerX, int baselineY, const GFXfont* font, uint16_t color);
void printRight(String text, int rightX, int baselineY, const GFXfont* font, uint16_t color);
void drawModernButton(int x, int y, int w, int h, int r, uint16_t bg, bool shadow);
void flashButton(int x, int y, int w, int h, int r);
void showToast(String msg);
String formatTime(uint32_t totalSeconds);

// Shared layout pieces: every app uses the same card, segmented control and
// progress bar so the screens line up with each other and with the 8 px margin
// grid.  They are drawn with the same handful of primitives the old ad-hoc
// buttons used, so nothing here costs extra frames.
void drawScreenHeader(const char* title, bool showBack);
void drawIconTile(int x, int y, int w, int h, int radius, uint16_t tint);
void drawStatusPill(int x, int y, int w, const char* text, uint16_t dotColor, uint16_t textColor);
void drawCard(int x, int y, int w, int h, bool active, int radius);
void drawSectionLabel(const char* text, int x, int y);
void drawSegmentedControl(int x, int y, int w, int h, const char* const* labels, int count, int activeIndex);
void drawProgressBar(int x, int y, int w, int h, float pct, uint16_t color);
void drawCrosshairTarget(int cx, int cy, int r, uint16_t color);

// Vector UI Icons
void drawGearIcon(int cx, int cy, int r, uint16_t color);
void drawClockIcon(int cx, int cy, int r, uint16_t color);
void drawChevron(int cx, int cy, bool pointUp, uint16_t color);
void drawBackChevron(int cx, int cy, uint16_t color);
void drawListIcon(int cx, int cy, uint16_t color);
void drawPlusIcon(int cx, int cy, uint16_t color);
void drawMinusIcon(int cx, int cy, uint16_t color);
int drawRadical(int x, int y, int size, uint16_t color);
void drawWaveIcon(int cx, int cy, int halfW, int amp, uint16_t color);
void drawPlayIcon(int cx, int cy, uint16_t color);
void drawPauseIcon(int cx, int cy, uint16_t color);
void drawSkipFwdIcon(int cx, int cy, uint16_t color);
void drawSkipRevIcon(int cx, int cy, uint16_t color);
