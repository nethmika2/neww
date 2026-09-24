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
inline esp_err_t esp_wifi_stop() { return ESP_OK; }
inline esp_err_t esp_wifi_disconnect() { return ESP_OK; }
