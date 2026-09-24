#pragma once
#include <Arduino.h>

// ==========================================
// EARBUD / HEADSET BUTTONS (AVRCP)
// ==========================================
// The earbuds that play our audio can send their touch controls back to the
// source as AVRCP commands:
//   * transport taps (play, pause, next, previous) as passthrough commands,
//   * volume taps as a "set absolute volume" command on buds that use the
//     absolute volume profile (most true-wireless buds, including the
//     Soundcore R50i NC).
// The ESP32-A2DP library forwards passthrough commands to a callback and logs
// absolute volume commands without acting on them.  Both halves are handled
// here.  Callbacks run in the Bluetooth task, so nothing more than a flag may
// happen there: the actions themselves are applied from the main loop in
// earbudControlsPoll(), where the display and the SD card are safe to touch.

// Receives the AVRCP passthrough keys from the Bluetooth task.  Public so the
// host side test harness can feed it key codes.
void earbudPassthruHandler(uint8_t key, bool isReleased);

// Sets the local volume from an absolute volume command (0..127 as used on the
// wire).  Called from the Bluetooth task, applied by the poll loop.
void earbudAbsoluteVolumeHandler(uint8_t wireVolume);

// Registers the passthrough handler.  Must be called before a2dp_source.start()
// because the library only initialises the AVRCP target when a handler exists.
void earbudControlsPrepare();
void earbudControlsPoll();

bool earbudControlsEnabled();
void earbudControlsSetEnabled(bool on);

// Diagnostics for the Settings screen: how many AVRCP commands the buds have
// sent since boot, and a short name for the last one.
int earbudEventCount();
String earbudLastEvent();
