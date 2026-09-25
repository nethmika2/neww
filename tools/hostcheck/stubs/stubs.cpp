// ---------------------------------------------------------------------------
// Definitions for the host side stand-ins: global objects, the advancing clock,
// the fake SD card and the small AVRCP target model.
// ---------------------------------------------------------------------------
#include <vector>
#include <string>
#include <map>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "HostDraw.h"
#include "Adafruit_GFX.h"
#include "SPI.h"
#include "Wire.h"
#include "SD.h"
#include "WiFi.h"
#include "Preferences.h"
#include "esp_avrc_api.h"
#include "BluetoothA2DPSource.h"

// ---- globals ---------------------------------------------------------------
HostSerial Serial;
SPIClass SPI(0);
SPIClass SPI1(1);
TwoWire Wire(0);
SDClass SD;
WiFiClass WiFi;
EspClass ESP;
BluetoothA2DPCommon *actual_bluetooth_a2dp_common = nullptr;
HostAvrc hostAvrc;

extern const GFXfont FreeSans9pt7b = {nullptr, nullptr, 0, 0, 14};
extern const GFXfont FreeSansBold9pt7b = {nullptr, nullptr, 0, 0, 14};
extern const GFXfont FreeSans12pt7b = {nullptr, nullptr, 0, 0, 18};
extern const GFXfont FreeSansBold12pt7b = {nullptr, nullptr, 0, 0, 18};
extern const GFXfont FreeSansBold18pt7b = {nullptr, nullptr, 0, 0, 25};
extern const GFXfont FreeSansBold24pt7b = {nullptr, nullptr, 0, 0, 33};

// ---- clock -----------------------------------------------------------------
// millis() advances on every call so that debounce and time based code paths
// can be exercised from a single threaded test.
static unsigned long hostMillis = 0;
static unsigned long hostMillisStep = 250;
void hostSetMillis(unsigned long v) { hostMillis = v; }
void hostSetMillisStep(unsigned long v) { hostMillisStep = v; }
unsigned long hostAdvanceMillis(unsigned long by) { hostMillis += by; return hostMillis; }
unsigned long millis() {
  unsigned long v = hostMillis;
  hostMillis += hostMillisStep;
  return v;
}
unsigned long micros() { return millis() * 1000UL; }
void delay(unsigned long ms) { (void)ms; }
void delayMicroseconds(unsigned long) {}
void yield() {}

// ---- GPIO ------------------------------------------------------------------
static std::map<uint8_t, int> gpioState;
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t val) { gpioState[pin] = val; }
int digitalRead(uint8_t pin) { return gpioState.count(pin) ? gpioState[pin] : 0; }
int analogRead(uint8_t) { return 2048; }
void analogWrite(uint8_t, int) {}
uint16_t analogReadMilliVolts(uint8_t) { return 1650; }
// ---- PWM (RGB LED + backlight) --------------------------------------------
// The host build models the 2.x core: channels carry the duty, which is recorded
// so the tests can assert exactly what the idle modes light up.
static uint32_t pwmDuty[8] = {0, 0, 0, 0, 0, 0, 0, 0};
void ledcSetup(uint8_t, double, uint8_t) {}
void ledcAttachPin(uint8_t, uint8_t) {}
void ledcWrite(uint8_t channel, uint32_t duty) {
  if (channel < 8) pwmDuty[channel] = duty;
}
int hostPwmDuty(int channel) { return (channel >= 0 && channel < 8) ? (int)pwmDuty[channel] : -1; }
double ledcReadFreq(uint8_t) { return 0; }

// ---- AVRCP target model ----------------------------------------------------
static esp_avrc_rn_evt_cap_mask_t hostRnCap = {0};
static esp_avrc_psth_bit_mask_t hostPsthSupported = {0xFFFFFFFFu};

esp_err_t esp_avrc_tg_init() { return ESP_OK; }
esp_err_t esp_avrc_tg_deinit() { return ESP_OK; }
esp_err_t esp_avrc_tg_register_callback(esp_avrc_tg_cb_t callback) {
  hostAvrc.registered = callback;
  return ESP_OK;
}
esp_err_t esp_avrc_tg_set_rn_evt_cap(const esp_avrc_rn_evt_cap_mask_t *evt_set) {
  if (!evt_set) return ESP_ERR_INVALID_ARG;
  hostRnCap = *evt_set;
  return ESP_OK;
}
esp_err_t esp_avrc_tg_get_psth_cmd_filter(esp_avrc_psth_filter_t filter, esp_avrc_psth_bit_mask_t *cmd_set) {
  (void)filter;
  if (!cmd_set) return ESP_ERR_INVALID_ARG;
  *cmd_set = hostPsthSupported;
  return ESP_OK;
}
esp_err_t esp_avrc_tg_set_psth_cmd_filter(esp_avrc_psth_filter_t filter, const esp_avrc_psth_bit_mask_t *cmd_set) {
  (void)filter;
  (void)cmd_set;
  return ESP_OK;
}
esp_err_t esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t event_id, esp_avrc_rn_rsp_t rsp, esp_avrc_rn_param_t *param) {
  (void)param;
  hostAvrc.rnResponses.push_back({(int)event_id, (int)rsp});
  return ESP_OK;
}

esp_err_t hostAvrcRnCapHas(esp_avrc_rn_event_ids_t e) {
  return hostRnCap.has(e) ? ESP_OK : ESP_FAIL;
}
void hostAvrcReset() {
  hostAvrc.reset();
  hostRnCap.bits = 0;
  hostAvrc.registered = nullptr;
}

extern "C" void ccall_app_rc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
  if (actual_bluetooth_a2dp_common) actual_bluetooth_a2dp_common->app_rc_tg_callback(event, param);
}

// ---- fake SD card ----------------------------------------------------------
// Switches the stub between cores that return the whole path from File::name()
// and ones that return just the base name.
void hostSetNameBaseOnly(bool v) { fs::nameBaseOnly() = v; }

uint32_t &hostFreeHeapValue() {
  static uint32_t v = 200000;
  return v;
}
void hostSetFreeHeap(uint32_t v) { hostFreeHeapValue() = v; }

void hostMakeFile(const char *path, const char *text) {
  fs::File f = SD.open(path, FILE_WRITE);
  f.write((const uint8_t *)text, strlen(text));
  f.close();
}

void hostMakeWav(const char *path, int seconds, int freq, uint32_t sampleRate) {
  const uint16_t channels = 2, bits = 16;
  uint32_t frames = (uint32_t)sampleRate * (uint32_t)seconds;
  uint32_t dataBytes = frames * channels * (bits / 8);
  std::vector<uint8_t> buf(44 + dataBytes);
  auto put32 = [&](int off, uint32_t v) { memcpy(&buf[off], &v, 4); };
  auto put16 = [&](int off, uint16_t v) { memcpy(&buf[off], &v, 2); };
  memcpy(&buf[0], "RIFF", 4);
  put32(4, 36 + dataBytes);
  memcpy(&buf[8], "WAVE", 4);
  memcpy(&buf[12], "fmt ", 4);
  put32(16, 16);
  put16(20, 1);
  put16(22, channels);
  put32(24, sampleRate);
  put32(28, sampleRate * channels * (bits / 8));
  put16(32, channels * (bits / 8));
  put16(34, bits);
  memcpy(&buf[36], "data", 4);
  put32(40, dataBytes);
  for (uint32_t i = 0; i < frames; i++) {
    double t = (double)i / sampleRate;
    int16_t s = (int16_t)(sin(2.0 * M_PI * freq * t) * 12000);
    for (int c = 0; c < channels; c++) memcpy(&buf[44 + (i * channels + c) * 2], &s, 2);
  }
  fs::File f = SD.open(path, FILE_WRITE);
  f.write(buf.data(), buf.size());
  f.close();
}
