#pragma once
#include <Arduino.h>
#include "Types.h"

// ==========================================
// ON-BOARD RGB LED + BACKLIGHT PWM
// ==========================================
// This board has an RGB LED on GPIO 4/16/17 (active LOW).  It does two jobs:
//
//  1. Keep-awake.  With the screen dark the board draws only about 40 mA, which
//     is below the cut-off of many USB power banks - they see "no load" and
//     switch themselves off, taking the device with them.  The LED is therefore
//     always doing something while the screen is dark.
//  2. State light.  It fades, breathes and cycles through colours to show what
//     the device is doing: focused work, a break, music, or an announcement.
//
// Patterns are chosen in Settings; every one has a floor rather than a true
// black, so the LED never looks switched off while the board is running.

// ---- raw PWM ---------------------------------------------------------------
void pwmBegin(uint8_t pin, uint8_t channel);
void pwmWrite(uint8_t pin, uint8_t channel, uint8_t duty);
// Backlight duty 0..255: 255 is exactly the old digitalWrite(HIGH).
void setBacklight(uint8_t duty);
int backlightLevel();

// ---- LED -------------------------------------------------------------------
// LedColor / LedEffect / LedLevel live in Types.h, next to the rest of the
// shared state this file drives.
void statusLedBegin();
// Direct output, brightnesses 0..255 (255 = on as hard as the board allows).
void statusLedSet(uint8_t r, uint8_t g, uint8_t b);
void statusLedOff();
int statusLedBrightness();

// The animated channel: whichever task is asking, the pattern is rendered here.
void ledRequest(LedColor color, LedEffect effect, LedLevel level);
void ledRequestOff();
// Announcement: three quick red flashes over `ms`, whatever else is running.
void ledAlert(uint16_t ms);
// Shows a pattern for a couple of seconds regardless of state, so a setting can
// be previewed from the settings screen (the LED is on the back of the board).
void ledPreview(LedColor color, LedEffect effect, LedLevel level);
bool ledPreviewActive();
// True while an announcement's flashes are still running (host tests + the
// serial diagnostic).
bool ledAlertActiveForTest();

// Renders the current request (whatever asked for it).  updateStatusLed() calls
// this after working out what the device is doing; the host tests call it
// directly to check a pattern on its own.
void ledTick();
// Works out what the device is doing and drives the LED accordingly.  Called
// once per loop; non-blocking.
void updateStatusLed();

// Introspection for the settings screen and the host tests.
int ledBrightnessNow();
void ledChannelsNow(uint8_t* r, uint8_t* g, uint8_t* b);
uint8_t ledLevelPeak(LedLevel level);       // 0..255 multiplier for a level
const char* ledColorName(LedColor c);
const char* ledEffectName(LedEffect e);
const char* ledLevelName(LedLevel l);
