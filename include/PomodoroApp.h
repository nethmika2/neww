#pragma once
#include <Arduino.h>

void drawPomodoroScreen(bool fullWipe);
void handlePomodoroTouch(bool touched, int sx, int sy);
