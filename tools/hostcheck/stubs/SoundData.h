#pragma once
// Audio frame definition, matching the ESP32-A2DP source callback signature.
#include <Arduino.h>
struct Frame {
  int16_t channel1[2048];
  int16_t channel2[2048];
};
int32_t get_audio_data(Frame *frame, int32_t frame_count);
typedef int32_t (*get_audio_data_cb_t)(Frame *frame, int32_t frame_count);
