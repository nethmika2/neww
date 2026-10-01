#pragma once
#include <Arduino.h>

// Which settings page is showing (0 device, 1 on-board LED); exposed so the
// host harness can render the second page without a touch.
int& settingsPageForTest();

void drawSettingsScreen();
void handleSettingsTouch(bool touched, int sx, int sy);
