#pragma once
#include <Arduino.h>

// ==========================================
// ON-BOARD RGB LED + BACKLIGHT PWM
// ==========================================
// This board has an RGB LED on GPIO 4/16/17 (active LOW).  With the screen off
// the whole board draws only about 40 mA, which is below the cut-off of many
// USB power banks - they see "no load" and switch themselves off, taking the
// device with them.  The LED is lit at full brightness whenever the screen is
// dark, so the pack always sees a load, and it doubles as a visible "still on"
// light.
//
// The backlight is on the same PWM machinery: the DIM idle mode keeps it at a
// low duty, which is the bigger load of the two and still leaves the panel
// almost black.

// Sets up both the LED channels and the backlight channel.
void pwmBegin(uint8_t pin, uint8_t channel);
void pwmWrite(uint8_t pin, uint8_t channel, uint8_t duty);

void statusLedBegin();
// r/g/b are brightnesses 0..255 (255 = the LED on as hard as the board allows).
void statusLedSet(uint8_t r, uint8_t g, uint8_t b);
void statusLedOff();
// Full white - the most current the LED can draw, used while the screen is dark.
void statusLedIdle();
// Highest channel brightness currently set, for the settings screen and tests.
int statusLedBrightness();

// Backlight duty 0..255: 255 is exactly the old digitalWrite(HIGH).
void setBacklight(uint8_t duty);
int backlightLevel();
