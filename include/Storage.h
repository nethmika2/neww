#pragma once
#include <Arduino.h>

// The micro SD card is read by two things: the app in front (notes) and the
// background audio feeder task (music).  FatFs is not safe to drive from two
// tasks at once, so every card operation goes through this one lock, which is
// the same mutex the feeder takes around its reads.  It returns false when the
// lock could not be taken in time - the caller may continue anyway, but it
// should say so on the serial port.
bool sdLockBegin(uint32_t timeoutMs);
void sdLockEnd();

void saveFunctions();
void loadFunctions();
void savePoints();
void loadPoints();
void saveVariables();
void loadVariables();
