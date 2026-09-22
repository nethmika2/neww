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
void drawModernButton(int x, int y, int w, int h, int r, uint16_t bg, bool shadow);
void flashButton(int x, int y, int w, int h, int r);
void showToast(String msg);
String formatTime(uint32_t totalSeconds);

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
