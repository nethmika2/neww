#pragma once
// ESP32-A2DP stand-in.  Keeps the API surface the firmware uses and lets a test
// push AVRCP events and audio state changes through it.
#include <Arduino.h>
#include "esp_avrc_api.h"
#include "SoundData.h"
#include <vector>
#include <functional>

class BluetoothA2DPCommon;
extern BluetoothA2DPCommon *actual_bluetooth_a2dp_common;

extern "C" void ccall_app_rc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

class A2DPVolumeControl {
 public:
  int volume = 50;
};

class BluetoothA2DPCommon {
 public:
  virtual ~BluetoothA2DPCommon() {}
  bool start(const char *name) {
    bt_name_ = name;
    actual_bluetooth_a2dp_common = this;
    started_ = true;
    return true;
  }
  void end() { started_ = false; }
  bool is_connected() { return connected_; }
  bool is_connected(int) { return connected_; }
  void set_connected(bool c) { connected_ = c; }
  void set_volume(int v) { volume_ = v; }
  int get_volume() { return volume_; }
  void set_avrc_rn_events(std::vector<esp_avrc_rn_event_ids_t> events) { rnEvents = events; }
  void set_avrc_passthru_command_callback(void (*cb)(uint8_t key, bool isReleased)) { passthru = cb; passthruActiveFlag = true; }
  void (*passthruCallback())(uint8_t, bool) { return passthru; }
  bool is_passthru_active() const { return passthruActiveFlag; }
  const std::vector<esp_avrc_rn_event_ids_t> &rnEventsRef() const { return rnEvents; }
  virtual void app_rc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) { lastTgEvent = (int)event; (void)param; }

  // Harness helpers
  int lastTgEvent = -1;
  int volume_ = 50;

 protected:
  bool started_ = false;
  bool connected_ = false;
  bool passthruActiveFlag = false;
  void (*passthru)(uint8_t, bool) = nullptr;
  std::vector<esp_avrc_rn_event_ids_t> rnEvents;
  const char *bt_name_ = "";
};

class BluetoothA2DPSource : public BluetoothA2DPCommon {
 public:
  virtual ~BluetoothA2DPSource() {}
  bool start(const char *name) { return BluetoothA2DPCommon::start(name); }
  bool start(const char *name, get_audio_data_cb_t cb) { (void)cb; return BluetoothA2DPCommon::start(name); }
  bool start(const char *name, void *cb) { (void)cb; return BluetoothA2DPCommon::start(name); }
  bool start() { return BluetoothA2DPCommon::start("hostcheck"); }
  void set_volume(int v) { BluetoothA2DPCommon::set_volume(v); }
  // Data callback plumbing used by AudioApp.cpp
  void set_data_callback(void *cb) { (void)cb; }
  // Real library: the stream reader / data callback forms used by AudioApp.
  void set_data_callback(int32_t (*cb)(Frame *, int32_t)) { (void)cb; }
  void set_stream_reader(int32_t (*cb)(Frame *, int32_t)) { (void)cb; }
  void set_stream_reader(void *r) { (void)r; }
  void set_reconnect(bool v) { (void)v; }
  void set_auto_reconnect(bool v) { (void)v; }
  void set_ssid(bool v) { (void)v; }
};
