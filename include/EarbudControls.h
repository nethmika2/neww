#pragma once
#include <Arduino.h>

// ==========================================
// EARBUD / HEADSET BUTTONS (AVRCP passthrough)
// ==========================================
// The earbuds that play our audio can send their transport buttons (play,
// pause, next, previous, volume) back to the source as AVRCP passthrough
// commands.  The ESP32-A2DP library forwards them to a callback, but that
// callback runs in the Bluetooth task, so nothing more than a flag may happen
// there: the actions themselves are applied from the main loop in
// earbudControlsPoll(), where the display and the SD card are safe to touch.

// Receives the AVRCP passthrough keys from the Bluetooth task.  Public so the
// host side test harness can feed it key codes.
void earbudPassthruHandler(uint8_t key, bool isReleased);

// Registers the passthrough handler.  Must be called before a2dp_source.start()
// because the library only initialises the AVRCP target when a handler exists.
void earbudControlsPrepare();
void earbudControlsPoll();

bool earbudControlsEnabled();
void earbudControlsSetEnabled(bool on);
