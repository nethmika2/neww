#pragma once
#include <Arduino.h>

bool getClock(int& hour, int& minute);
String getTimeString();
String dateLabelShort();  // "Wed 24 Sep", or "clock not set"
bool syncTimeNTP(bool showUI);

// Parses an HTTP "Date:" header value ("Sun, 29 Sep 2026 12:34:56 GMT") into a
// UTC epoch, or 0 when the line is not a date.  Exposed for the host tests.
time_t parseHttpDate(const char* value);
