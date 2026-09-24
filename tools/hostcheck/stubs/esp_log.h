#pragma once
// Logging stand-in: the level macros print to stdout so a host run shows the
// same diagnostics the serial monitor would.
#include <Arduino.h>
#define ESP_LOGE(tag, fmt, ...) do { printf("[E][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { printf("[W][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { printf("[I][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { printf("[D][%s] ", tag); printf(fmt, ##__VA_ARGS__); printf("\n"); } while (0)
#define ESP_LOGV(tag, fmt, ...) do {} while (0)
