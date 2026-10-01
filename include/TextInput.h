#pragma once
#include <Arduino.h>
#include "Types.h"

// ==========================================
// GENERIC TEXT KEYBOARD
// ==========================================
// Small letter/number keyboard used for naming to-do items and routines.  The
// result is handed back through textInputTarget, and the screen it came from
// is restored afterwards.

void startTextInput(const String& title, const String& initial, TextTarget target, int maxLen);
void drawTextKeyboardScreen(bool fullWipe);
void handleTextKeyboardTouch(bool touched, int sx, int sy);
void cancelTextInput();
