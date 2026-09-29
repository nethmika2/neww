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

// The real core hands back an IPAddress with a toString(); model just enough of
// it that the firmware's logging compiles and can be asserted in a test.
class IPAddress {
 public:
  IPAddress() {}
  IPAddress(const char *s) : s_(s) {}
  String toString() const { return s_; }
  operator String() const { return s_; }

 private:
  String s_ = "0.0.0.0";
};

class WiFiClass {
 public:
  // The harness scripts the link: hostWifiAnswerOnBegin(n) makes the n-th
  // begin() the one the access point answers, 0 meaning it never does.  The
  // counters expose what the firmware did with the radio.
  int begin(const char *, const char * = nullptr) {
    begins_++;
    connected_ = (answerOnBegin_ != 0 && begins_ >= answerOnBegin_);
    return 0;
  }
  int status() { return connected_ ? WL_CONNECTED : WL_DISCONNECTED; }
  void disconnect(bool = false) { connected_ = false; disconnects_++; }
  void mode(int m) { mode_ = m; modes_++; }
  void setSleep(bool s) { sleep_ = s; }
  IPAddress localIP() { return IPAddress("192.168.8.123"); }
  int RSSI() { return -60; }
  void hostname(const char *) {}
  void persistent(bool) {}

  void hostAnswerOnBegin(int n) { answerOnBegin_ = n; begins_ = 0; connected_ = false; }
  int hostBegins() const { return begins_; }
  int hostDisconnects() const { return disconnects_; }
  int hostModes() const { return modes_; }
  bool hostSleepEnabled() const { return sleep_; }
  void hostReset() { begins_ = disconnects_ = modes_ = 0; answerOnBegin_ = 0; connected_ = false; sleep_ = true; mode_ = WIFI_OFF; }

 private:
  int begins_ = 0, disconnects_ = 0, modes_ = 0;
  int answerOnBegin_ = 0;
  bool connected_ = false, sleep_ = true;
  int mode_ = WIFI_OFF;
};
extern WiFiClass WiFi;
