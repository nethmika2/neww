#pragma once
#include <Arduino.h>

bool getClock(int& hour, int& minute);
String getTimeString();
bool syncTimeNTP(bool showUI);
