#pragma once
// Audio frame definition, matching the ESP32-A2DP source callback signature.
//
// The real library defines this in A2DPVolumeControl.h (v1.8.11):
//
//   struct __attribute__((packed)) Frame {
//     int16_t channel1;
//     int16_t channel2;
//   };
//
// It is a *four byte* stereo pair, and BluetoothA2DPSource::get_audio_data()
// calls the frames callback with len/4 and multiplies the result by 4, so the
// frame count the firmware sees is bytes/4.  Getting this wrong (an older stub
// here had two 2048 sample arrays) makes the firmware's byte maths look 2048
// times larger than it is, which hides every real bug in it.
#include <Arduino.h>
struct __attribute__((packed)) Frame {
  int16_t channel1;
  int16_t channel2;
};
int32_t get_audio_data(Frame *frame, int32_t frame_count);
typedef int32_t (*get_audio_data_cb_t)(Frame *frame, int32_t frame_count);
