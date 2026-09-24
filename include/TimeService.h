#pragma once
#include <Arduino.h>

bool getClock(int& hour, int& minute);
String getTimeString();
String dateLabelShort();  // "Wed 24 Sep", or "clock not set"
bool syncTimeNTP(bool showUI);
