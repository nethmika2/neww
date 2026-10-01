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

// The A2DP audio states the firmware asks about when it decides to take the
// ring buffer back (see audioRingService).
typedef enum {
  ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND = 0,
  ESP_A2D_AUDIO_STATE_STOPPED = 1,
  ESP_A2D_AUDIO_STATE_STARTED = 2,
  ESP_A2D_AUDIO_STATE_SUSPEND = 3
} esp_a2d_audio_state_t;

// AVDTP media control (esp_a2d_api.h in the real SDK): the app uses it to
// restart the stream without dropping the Bluetooth link.
typedef enum {
  ESP_A2D_MEDIA_CTRL_NONE = 0,
  ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY,
  ESP_A2D_MEDIA_CTRL_START,
  ESP_A2D_MEDIA_CTRL_SUSPEND,
  ESP_A2D_MEDIA_CTRL_STOP
} esp_a2d_media_ctrl_t;

inline int &hostMediaCtrlSuspendCount() {
  static int n = 0;
  return n;
}
inline int &hostMediaCtrlStartCount() {
  static int n = 0;
  return n;
}
inline esp_err_t esp_a2d_media_ctrl(esp_a2d_media_ctrl_t cmd) {
  if (cmd == ESP_A2D_MEDIA_CTRL_SUSPEND) hostMediaCtrlSuspendCount()++;
  if (cmd == ESP_A2D_MEDIA_CTRL_START) hostMediaCtrlStartCount()++;
  return ESP_OK;
}

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
    startCount++;
    startAfterPassthru = (passthru != nullptr);
    return true;
  }
  void end() { started_ = false; endCount++; }
  bool is_connected() { return connected_; }
  bool is_connected(int) { return connected_; }
  esp_a2d_audio_state_t get_audio_state() { return audioState_; }
  void hostSetAudioState(esp_a2d_audio_state_t st) { audioState_ = st; }
  void set_on_audio_state_changed(void (*cb)(esp_a2d_audio_state_t, void*), void* = nullptr) { audioStateCb_ = cb; }
  // Posts a state the way the stack does: through the registered callback.
  void hostPostAudioState(esp_a2d_audio_state_t st) {
    audioState_ = st;
    if (audioStateCb_) audioStateCb_(st, nullptr);
  }
  int endCount = 0;
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
  int startCount = 0;
  bool startAfterPassthru = false;
  void hostResetPassthru() { passthru = nullptr; passthruActiveFlag = false; rnEvents.clear(); }
  int volume_ = 50;

 protected:
  bool started_ = false;
  bool connected_ = false;
  esp_a2d_audio_state_t audioState_ = ESP_A2D_AUDIO_STATE_SUSPEND;
  void (*audioStateCb_)(esp_a2d_audio_state_t, void*) = nullptr;
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
  // Records what the firmware asked for, so the tests can check that it asks for
  // the remembered earbuds (and not for a scan) when it has them.
  void set_auto_reconnect(bool v, int tries = 1000) { autoReconnect = v; reconnectTries = tries; reconnectAddrSet = false; }
  void set_auto_reconnect(esp_bd_addr_t addr, int tries = 3) {
    autoReconnect = true; reconnectTries = tries; reconnectAddrSet = true;
    memcpy(lastPeer, addr, 6); memcpy(reconnectAddr, addr, 6);
  }
  esp_bd_addr_t *get_last_peer_address() { return &lastPeer; }
  // Like the real end(): the library forgets the peer it held.
  void end() { BluetoothA2DPCommon::end(); memset(lastPeer, 0, 6); }
  bool autoReconnect = false; int reconnectTries = 0; bool reconnectAddrSet = false;
  esp_bd_addr_t reconnectAddr = {0}; esp_bd_addr_t lastPeer = {0};
  void set_ssid(bool v) { (void)v; }
};
