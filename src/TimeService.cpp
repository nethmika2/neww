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

// Puts the radio back the way it was found: a half-torn-down WiFi can make the
// *next* attempt fail, which is how "sometimes it does not connect" looks.
static void wifiTeardown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
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
  // Modem sleep adds hundreds of milliseconds to the association and can make
  // the name lookup and the NTP round trip time out on a slow access point.
  WiFi.setSleep(false);

  // Two association attempts.  Routers (and 4G ones in particular) routinely
  // ignore the first probe, and one 8 second window is not enough to tell a
  // slow access point from a wrong password.
  bool associated = false;
  for (int attempt = 1; attempt <= 2 && !associated; attempt++) {
    // The second window is shorter: a router that ignored the first probe
    // answers quickly once it is ready, and a wrong password must not cost a
    // minute of boot time.
    unsigned long window = (attempt == 1) ? 8000 : 6000;
    Serial.printf("[I][wifi] attempt %d: %s\n", attempt, WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < window) {
      if (showUI && ts.touched()) {
        wifiTeardown();
        return false;
      }
      delay(100);
    }
    associated = (WiFi.status() == WL_CONNECTED);
    Serial.printf("[I][wifi] attempt %d: %s after %lu ms\n", attempt,
                  associated ? "connected" : "no answer", millis() - start);
    if (!associated) {
      WiFi.disconnect(true);
      delay(200);
    }
  }
  if (!associated) {
    wifiTeardown();
    if (showUI) showToast("WiFi connection failed");
    return false;
  }
  Serial.printf("[I][wifi] ip %s rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  // Two NTP windows as well: the first request of a fresh association is the
  // one that gets lost, and re-issuing configTime() re-resolves the servers.
  timeSynced = false;
  bool cancelled = false;
  int h = 0, m = 0;
  for (int pass = 1; pass <= 2 && !timeSynced && !cancelled; pass++) {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, NTP_1, NTP_2, NTP_3);
    unsigned long start = millis();
    unsigned long window = (pass == 1) ? 7000 : 5000;
    while (!timeSynced && millis() - start < window) {
      timeSynced = getClock(h, m);
      if (showUI && ts.touched()) {
        cancelled = true;   // tapping to skip must not start the second pass
        break;
      }
      delay(100);
    }
    Serial.printf("[I][ntp] pass %d: %s\n", pass, timeSynced ? "clock set" : "no reply");
  }
  wifiTeardown();
  if (cancelled) return false;
  if (showUI) {
    if (timeSynced) showToast("Clock updated!");
    else showToast("NTP timeout, WiFi was ok");
  }
  return timeSynced;
}
