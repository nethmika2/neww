#include "TimeService.h"
#include <WiFi.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
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
//
// It deliberately does NOT call esp_wifi_stop(): Bluetooth uses the same radio
// through the coexistence layer, and hard-stopping the WiFi driver in the same
// boot as a Bluedroid start trips an internal assert (vQueueDelete with a null
// handle) as soon as audio starts streaming.  Taking the mode down to WIFI_OFF
// stops the driver just as well for our purposes.
static void wifiTeardown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
}

// ==========================================
// TIME OVER HTTP (LAST RESORT)
// ==========================================
// A Date header is good to the second, needs no lookup beyond the host name and
// works on networks that block NTP.  Two hosts are tried: the first is a tiny
// connectivity check page, the second a very large name that is unlikely to be
// blocked.
static const char* const HTTP_TIME_HOSTS[2] = { "connectivitycheck.gstatic.com", "www.google.com" };

time_t parseHttpDate(const char* value) {
  static const char* const mon[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
  if (!value) return 0;
  const char* p = value;
  while (*p == ' ') p++;
  // Skip the weekday ("Sun," or "Sunday,") when it is there.
  const char* comma = strchr(p, ',');
  if (comma) p = comma + 1;
  int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
  char month[4] = { 0, 0, 0, 0 };
  if (sscanf(p, " %d %3s %d %d:%d:%d", &day, month, &year, &hh, &mm, &ss) != 6) return 0;
  int mo = -1;
  for (int i = 0; i < 12; i++)
    if (strncasecmp(month, mon[i], 3) == 0) mo = i;
  if (mo < 0 || day < 1 || day > 31 || year < 2020 || year > 2200) return 0;
  if (hh > 23 || mm > 59 || ss > 60) return 0;
  // Days since 1970-01-01, civil calendar (the same arithmetic the timer store
  // uses for its day numbers): the year is taken as the one that began in
  // March, the month index is then the plain calendar month.
  int y = (mo <= 1) ? year - 1 : year;
  unsigned m = (unsigned)(mo + 1);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + (unsigned)day - 1u;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = (long)era * 146097L + (long)doe - 719468L;
  return (time_t)(days * 86400L + hh * 3600 + mm * 60 + ss);
}

static bool syncFromHttpDate() {
  for (int i = 0; i < 2; i++) {
    const char* host = HTTP_TIME_HOSTS[i];
    WiFiClient client;
    client.setTimeout(3000);
    if (!client.connect(host, 80)) {
      Serial.printf("[I][ntp] http %s: no connection\n", host);
      continue;
    }
    client.printf("GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: cyd-os\r\nConnection: close\r\n\r\n", host);
    unsigned long start = millis();
    time_t stamp = 0;
    while (millis() - start < 5000) {
      if (!client.connected() && !client.available()) break;
      String line = client.readStringUntil('\n');
      if (line.startsWith("Date:")) {
        stamp = parseHttpDate(line.c_str() + 5);
        break;
      }
    }
    client.stop();
    if (stamp <= 0) {
      Serial.printf("[I][ntp] http %s: no date header\n", host);
      continue;
    }
    struct timeval tv;
    tv.tv_sec = stamp;          // the header is GMT, which is what the RTC holds
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    Serial.printf("[I][ntp] http %s -> clock set\n", host);
    return true;
  }
  return false;
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

  // One name lookup per server, logged: if the names do not resolve, the NTP
  // port is irrelevant, and the address-only passes below are the answer.
  {
    IPAddress ip;
    const char* names[3] = { NTP_1, NTP_2, NTP_3 };
    for (int i = 0; i < 3; i++) {
      bool ok = WiFi.hostByName(names[i], ip);
      Serial.printf("[I][ntp] dns %s -> %s\n", names[i], ok ? ip.toString().c_str() : "fail");
    }
  }

  // Three passes, each with its own configTime(): the Arduino core stops and
  // restarts SNTP there, so every pass sends a fresh request instead of waiting
  // on the long retry backoff.  The first pass uses the configured names, the
  // others plain addresses, which need no DNS at all.
  const char* passA[3] = { NTP_1, NTP_2, NTP_3 };
  const char* passB[3] = { NTP_IP_1, NTP_IP_2, NTP_3 };
  const char* passC[3] = { NTP_IP_2, NTP_IP_1, NTP_3 };
  const char* const* passes[3] = { passA, passB, passC };
  const char* labels[3] = { "names", "cloudflare ip", "google ip" };

  timeSynced = false;
  bool cancelled = false;
  int h = 0, m = 0;
  for (int pass = 0; pass < 3 && !timeSynced && !cancelled; pass++) {
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, passes[pass][0], passes[pass][1], passes[pass][2]);
    unsigned long start = millis();
    while (!timeSynced && millis() - start < 6000) {
      timeSynced = getClock(h, m);
      if (showUI && ts.touched()) {
        cancelled = true;   // tapping to skip must not start the next pass
        break;
      }
      delay(100);
    }
    Serial.printf("[I][ntp] pass %d (%s -> %s): %s\n", pass + 1, labels[pass],
                  passes[pass][0], timeSynced ? "clock set" : "no reply");
  }

  // Still nothing?  Some networks drop NTP entirely but answer HTTP: take the
  // clock from a Date header rather than leave the device without a time.
  if (!timeSynced && !cancelled) {
    Serial.printf("[I][ntp] no NTP reply, trying the HTTP date\n");
    syncFromHttpDate();
    timeSynced = getClock(h, m);
  }

  wifiTeardown();
  if (cancelled) return false;
  if (showUI) {
    if (timeSynced) showToast("Clock updated!");
    else showToast("Clock not set - no time server");
  }
  return timeSynced;
}
