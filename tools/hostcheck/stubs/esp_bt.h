#pragma once
// The one controller setting the firmware touches: keeping Bluetooth out of
// modem sleep while it streams audio.
#include <Arduino.h>
inline bool &hostBtSleepDisabled() {
  static bool disabled = false;
  return disabled;
}
inline esp_err_t esp_bt_sleep_disable(void) {
  hostBtSleepDisabled() = true;
  return ESP_OK;
}
