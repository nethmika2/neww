#pragma once
#include <Arduino.h>

void drawSingleKey(int r, int c, const char* keys[5][6]);
void updateInputBox();
void printPrettyEquation(int start_x, int start_y, String eq, int cursor_pos, uint16_t color, int base_size);
void drawKeyboardScreen(const char* keys[5][6]);
void handleKeyboardTouch(bool touched, int sx, int sy);

void drawPointKeyboardScreen();
void updatePointInputBox();
void handlePointKeyboardTouch(bool touched, int sx, int sy);
