#pragma once
#include <Arduino.h>
typedef int wifi_mode_t;
typedef int esp_err_t;
#define WIFI_MODE_NULL 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_AP 2
#define WIFI_MODE_APSTA 3
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
// Counted so a test can prove the radio is fully shut down on every path out
// of the clock sync, not only on the successful one.
inline int &hostWifiStopRef() {
  static int n = 0;
  return n;
}
inline esp_err_t esp_wifi_stop() {
  hostWifiStopRef()++;
  return ESP_OK;
}
inline int hostWifiStopCount() { return hostWifiStopRef(); }
inline void hostWifiStopReset() { hostWifiStopRef() = 0; }
inline esp_err_t esp_wifi_disconnect() { return ESP_OK; }
