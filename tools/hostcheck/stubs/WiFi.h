#pragma once
#include <Arduino.h>
#include "esp_wifi.h"
#include <ctime>
#include <string>
#include <cstdarg>
#include <cstdio>
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_DISCONNECTED = 6 } wl_status_t;

// Enough of the event API for the disconnect reason the firmware logs.
typedef enum { ARDUINO_EVENT_WIFI_READY = 0, ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 5 } arduino_event_id_t;
typedef arduino_event_id_t WiFiEvent_t;
struct wifi_event_sta_disconnected_t_stub {
  uint8_t reason;
};
union WiFiEventInfo_t {
  wifi_event_sta_disconnected_t_stub wifi_sta_disconnected;
};
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

// A Date header response the tests can script, plus the request that was sent.
inline std::string &hostHttpResponse() {
  static std::string s;
  return s;
}
inline std::string &hostHttpRequest() {
  static std::string s;
  return s;
}
inline bool &hostHttpConnectOk() {
  static bool b = true;
  return b;
}
inline bool &hostDnsOk() {
  static bool b = true;
  return b;
}

// Minimal WiFiClient: the firmware only ever uses one for the HTTP Date
// fallback in TimeService.cpp, so this models a single request/response pair.
class WiFiClient {
 public:
  bool connect(const char *host, uint16_t) {
    host_ = host;
    pos_ = 0;
    return hostHttpConnectOk();
  }
  void setTimeout(uint32_t) {}
  size_t printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    hostHttpRequest() += buf;
    return (size_t)(n > 0 ? n : 0);
  }
  bool connected() { return hostHttpConnectOk(); }
  int available() { return (int)(hostHttpResponse().size() - pos_); }
  String readStringUntil(char terminator) {
    String out;
    const std::string &r = hostHttpResponse();
    while (pos_ < r.size()) {
      char c = r[pos_++];
      if (c == terminator) break;
      out += c;
    }
    return out;
  }
  void stop() {}

 private:
  const char *host_ = "";
  size_t pos_ = 0;
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
  bool hostByName(const char *name, IPAddress &out) {
    (void)name;
    lookups_++;
    if (!hostDnsOk()) return false;
    out = IPAddress("203.0.113.9");
    return true;
  }
  int RSSI() { return -60; }
  void hostname(const char *) {}
  void persistent(bool p) { persistent_ = p; }
  void setAutoReconnect(bool a) { autoReconnect_ = a; }
  void onEvent(void (*cb)(WiFiEvent_t, WiFiEventInfo_t), int = -1) { eventCb_ = cb; }
  bool hostPersistent() const { return persistent_; }
  bool hostAutoReconnect() const { return autoReconnect_; }
  // Fires the registered handler as the driver would on a failed association.
  void hostFireDisconnect(uint8_t reason) {
    if (!eventCb_) return;
    WiFiEventInfo_t info;
    info.wifi_sta_disconnected.reason = reason;
    eventCb_(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, info);
  }

  void hostAnswerOnBegin(int n) { answerOnBegin_ = n; begins_ = 0; connected_ = false; }
  int hostLookups() const { return lookups_; }
  int hostBegins() const { return begins_; }
  int hostDisconnects() const { return disconnects_; }
  int hostModes() const { return modes_; }
  bool hostSleepEnabled() const { return sleep_; }
  void hostReset() {
    begins_ = disconnects_ = modes_ = lookups_ = 0;
    answerOnBegin_ = 0;
    connected_ = false;
    sleep_ = true;
    mode_ = WIFI_OFF;
    hostHttpResponse().clear();
    hostHttpRequest().clear();
    hostHttpConnectOk() = true;
    hostDnsOk() = true;
    persistent_ = true;
    autoReconnect_ = true;
    eventCb_ = nullptr;
  }

 private:
  int begins_ = 0, disconnects_ = 0, modes_ = 0, lookups_ = 0;
  bool persistent_ = true, autoReconnect_ = true;
  void (*eventCb_)(WiFiEvent_t, WiFiEventInfo_t) = nullptr;
  int answerOnBegin_ = 0;
  bool connected_ = false, sleep_ = true;
  int mode_ = WIFI_OFF;
};
extern WiFiClass WiFi;
