#include "TimeService.h"
#include <WiFi.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
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

// Why an association failed is the one thing the status code does not say.
static void wifiEventLog(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[I][wifi] disconnected, reason %u\n", (unsigned)info.wifi_sta_disconnected.reason);
  }
}

// ==========================================
// TIME OVER HTTP (LAST RESORT)
// ==========================================
// A Date header is good to the second and works on networks that block NTP.  The
// first target is reached by IP address, so it needs neither DNS nor UDP: it
// covers the network where name resolution is what fails.
struct HttpTimeTarget { const char* connectTo; const char* hostHeader; };
static const HttpTimeTarget HTTP_TIME_TARGETS[3] = {
  { "1.1.1.1", "one.one.one.one" },                                          // no DNS at all
  { "connectivitycheck.gstatic.com", "connectivitycheck.gstatic.com" },
  { "www.google.com", "www.google.com" },
};

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

static bool syncFromHttpDate(bool showUI, bool& cancelled) {
  for (int i = 0; i < 3; i++) {
    const char* host = HTTP_TIME_TARGETS[i].connectTo;
    WiFiClient client;
    client.setTimeout(3000);
    if (!client.connect(host, 80)) {
      Serial.printf("[I][ntp] http %s: no connection\n", host);
      continue;
    }
    client.printf("GET / HTTP/1.0\r\nHost: %s\r\nUser-Agent: cyd-os\r\nConnection: close\r\n\r\n",
                  HTTP_TIME_TARGETS[i].hostHeader);
    unsigned long start = millis();
    time_t stamp = 0;
    while (millis() - start < 3500) {
      if (!client.connected() && !client.available()) break;
      String line = client.readStringUntil('\n');
      if (line.startsWith("Date:")) {
        stamp = parseHttpDate(line.c_str() + 5);
        break;
      }
      if (showUI && ts.touched()) {   // tapping to skip stops the whole sync
        cancelled = true;
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
  // The access point settings are written to flash by default: a power cut while
  // that write is in flight leaves the driver with a half-written profile, which
  // is one of the ways "it connected yesterday" turns into "no answer" today.
  // This firmware always passes the credentials explicitly, so nothing is lost.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  // A fresh handler per sync call would pile up in the driver's callback list.
  static bool wifiEventsHooked = false;
  if (!wifiEventsHooked) {
    WiFi.onEvent(wifiEventLog);
    wifiEventsHooked = true;
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
    Serial.printf("[I][wifi] attempt %d: %s after %lu ms (status %d)\n", attempt,
                  associated ? "connected" : "no answer", millis() - start, (int)WiFi.status());
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

  // One name lookup per server, logged: it tells DNS problems apart from a
  // network that simply does not answer NTP.
  {
    IPAddress ip;
    const char* names[3] = { NTP_1, NTP_2, NTP_3 };
    for (int i = 0; i < 3; i++) {
      bool ok = WiFi.hostByName(names[i], ip);
      Serial.printf("[I][ntp] dns %s -> %s\n", names[i], ok ? ip.toString().c_str() : "fail");
    }
  }

  // Some LTE routers answer NTP on the LAN even when the operator blocks it
  // upstream, so the gateway is worth one short pass of its own.
  String gateway = WiFi.gatewayIP().toString();
  if (gateway == "0.0.0.0" || gateway.length() == 0) gateway = "";

  timeSynced = false;
  bool cancelled = false;
  int h = 0, m = 0;

  // One NTP pass: its own configTime() (the Arduino core stops and restarts
  // SNTP there, so every pass sends a fresh request instead of waiting on the
  // retry backoff) and a short window, because a server that is reachable
  // answers in well under a second.
  struct NtpPass { const char* label; const char* server; };
  String namesLabel = String("names -> ") + NTP_1;
  NtpPass ntpPasses[3] = {
    { namesLabel.c_str(), NTP_1 },
    { "router", gateway.length() ? gateway.c_str() : NTP_IP_1 },
    { "cloudflare ip", NTP_IP_1 },
  };
  auto tryNtp = [&]() -> bool {
    for (int pass = 0; pass < 3 && !timeSynced && !cancelled; pass++) {
      configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, ntpPasses[pass].server,
                 ntpPasses[pass].server == NTP_IP_1 ? NTP_IP_2 : NTP_3, NTP_3);
      unsigned long start = millis();
      while (!timeSynced && millis() - start < 4000) {
        timeSynced = getClock(h, m);
        if (showUI && ts.touched()) {
          cancelled = true;   // tapping to skip must not start the next pass
          break;
        }
        delay(100);
      }
      Serial.printf("[I][ntp] pass %d (%s -> %s): %s\n", pass + 1, ntpPasses[pass].label,
                    ntpPasses[pass].server, timeSynced ? "clock set" : "no reply");
    }
    return timeSynced;
  };
  auto tryHttp = [&]() -> bool {
    if (timeSynced || cancelled) return timeSynced;
    Serial.printf("[I][ntp] NTP gave nothing, trying an HTTP Date header\n");
    return syncFromHttpDate(showUI, cancelled);
  };

  // Which source worked last time is remembered: on a network that filters NTP
  // (or HTTP), the next sync starts with the one that works and the clock is set
  // in a second or two instead of after a string of timeouts.
  int preferred = prefs.getInt("timesrc", 0);
  bool viaHttp = false;
  if (preferred == 1) {
    viaHttp = tryHttp();
    if (viaHttp) {
      timeSynced = true;
    } else {
      timeSynced = tryNtp();
      viaHttp = false;   // remember the source that actually answered
    }
  } else {
    timeSynced = tryNtp();
    viaHttp = false;
    if (!timeSynced && !cancelled) {
      viaHttp = tryHttp();
      timeSynced = viaHttp;
    }
  }
  if (cancelled) timeSynced = false;
  if (timeSynced) {
    prefs.putInt("timesrc", viaHttp ? 1 : 0);
    Serial.printf("[I][time] synced via %s\n", viaHttp ? "HTTP" : "NTP");
  }

  wifiTeardown();
  if (cancelled) return false;
  if (showUI) {
    if (timeSynced) showToast("Clock updated!");
    else showToast("Clock not set - no time server");
  }
  return timeSynced;
}
