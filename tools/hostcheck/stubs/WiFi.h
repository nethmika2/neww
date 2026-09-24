#pragma once
#include <Arduino.h>
#include "esp_wifi.h"
#include <ctime>
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;
#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP 2
#define WIFI_AP_STA 3
inline void configTime(long, int, const char *, const char *, const char * = nullptr) {}
inline void configTzTime(const char *, const char *, const char * = nullptr) {}
inline bool getLocalTime(struct tm *info, uint32_t ms = 5000) { (void)ms; memset(info, 0, sizeof(struct tm)); return false; }

class WiFiClass {
 public:
  int begin(const char *, const char * = nullptr) { return 0; }
  int status() { return 0; }
  void disconnect(bool = false) {}
  void mode(int) {}
  String localIP() { return String("0.0.0.0"); }
  int RSSI() { return -60; }
  void hostname(const char *) {}
  void persistent(bool) {}
};
extern WiFiClass WiFi;
