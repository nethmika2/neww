#include "TimeService.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include "Globals.h"
#include "DisplayUtils.h"

bool getClock(int& hour, int& minute) {
  time_t now;
  time(&now);
  struct tm ti;
  localtime_r(&now, &ti);
  hour = ti.tm_hour;
  minute = ti.tm_min;
  return (ti.tm_year + 1900) >= 2020;
}

String getTimeString() {
  int h, m;
  if (!getClock(h, m)) return "--:--";
  String s = "";
  if (h < 10) s += "0";
  s += h;
  s += ":";
  if (m < 10) s += "0";
  s += m;
  return s;
}

// Short local date, e.g. "Wed 24 Sep".  Used by the home status line and the
// timer reports; falls back to a neutral string when the clock is not set.
String dateLabelShort() {
  time_t now;
  time(&now);
  struct tm ti;
  localtime_r(&now, &ti);
  if ((ti.tm_year + 1900) < 2020) return "clock not set";
  static const char* const wd[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  static const char* const mo[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  return String(wd[ti.tm_wday % 7]) + " " + String(ti.tm_mday) + " " + String(mo[ti.tm_mon % 12]);
}

bool syncTimeNTP(bool showUI) {
  if (btInitialized) {
    if (showUI) showToast("Restart device to sync time");
    return false;
  }
  if (String(WIFI_SSID) == "YOUR_WIFI_NAME") {
    if (showUI) showToast("Set WiFi name in code");
    return false;
  }
  if (showUI) {
    tft.fillScreen(BG_COLOR);
    printCentered("Syncing clock...", 160, 90, &FreeSansBold9pt7b, TEXT_COLOR);
    printCentered(WIFI_SSID, 160, 118, &FreeSans9pt7b, MUTED_COLOR);
    printCentered("Tap screen to skip", 160, 205, &FreeSans9pt7b, MUTED_COLOR);
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    if (showUI && ts.touched()) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      return false;
    }
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    if (showUI) showToast("WiFi connection failed");
    return false;
  }
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_1, NTP_2, NTP_3);
  start = millis();
  int h, m;
  while (!getClock(h, m) && millis() - start < 6000) {
    if (showUI && ts.touched()) break;
    delay(100);
  }
  timeSynced = getClock(h, m);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  if (showUI) {
    if (timeSynced) showToast("Clock updated!");
    else showToast("NTP sync timed out");
  }
  return timeSynced;
}
