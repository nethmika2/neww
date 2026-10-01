#pragma once
// Logging stand-in: the level macros print to stdout so a host run shows the
// same diagnostics the serial monitor would.  The firmware also sets a global
// level at boot (to stop the Bluetooth stack flooding the serial monitor), so
// the host records the call and keeps printing.
#include <Arduino.h>
#define ESP_LOGE(tag, fmt, ...) do { printf("[E][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { printf("[W][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { printf("[I][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { printf("[D][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGV(tag, fmt, ...) do {} while (0)

typedef enum {
  ESP_LOG_NONE = 0,
  ESP_LOG_ERROR,
  ESP_LOG_WARN,
  ESP_LOG_INFO,
  ESP_LOG_DEBUG,
  ESP_LOG_VERBOSE
} esp_log_level_t;

// Last level the firmware asked for, so a test can assert the flood is off.
inline esp_log_level_t &hostLogLevel() {
  static esp_log_level_t level = ESP_LOG_VERBOSE;
  return level;
}
inline void esp_log_level_set(const char *tag, esp_log_level_t level) {
  (void)tag;
  hostLogLevel() = level;
}
