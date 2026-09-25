#include "Led.h"
#include "Config.h"

// The Arduino core changed the LEDC API in 3.x (attach by pin instead of by
// channel), so both are handled here; the firmware itself only sees the small
// interface above.
static void pwmAttach(uint8_t pin, uint8_t channel) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(pin, LED_PWM_FREQ, LED_PWM_BITS);
  (void)channel;
#else
  ledcSetup(channel, LED_PWM_FREQ, LED_PWM_BITS);
  ledcAttachPin(pin, channel);
#endif
}

static void pwmSet(uint8_t pin, uint8_t channel, uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
  (void)channel;
#else
  ledcWrite(channel, duty);
  (void)pin;
#endif
}

void pwmBegin(uint8_t pin, uint8_t channel) { pwmAttach(pin, channel); }

void pwmWrite(uint8_t pin, uint8_t channel, uint8_t duty) { pwmSet(pin, channel, duty); }

// The board wires the LED to 3V3 with the GPIO pulling it down, so a LOW pin is
// "on".  Duty is inverted here, which lets the rest of the firmware talk about
// brightness the way a person expects.
static uint8_t ledDuty(uint8_t brightness) {
#if LED_ACTIVE_LOW
  return (uint8_t)(LED_PWM_MAX - brightness);
#else
  return brightness;
#endif
}

static uint8_t ledBrightness = 0;

void statusLedBegin() {
  pwmBegin(LED_R_PIN, LED_CH_R);
  pwmBegin(LED_G_PIN, LED_CH_G);
  pwmBegin(LED_B_PIN, LED_CH_B);
  statusLedOff();
}

void statusLedSet(uint8_t r, uint8_t g, uint8_t b) {
  pwmWrite(LED_R_PIN, LED_CH_R, ledDuty(r));
  pwmWrite(LED_G_PIN, LED_CH_G, ledDuty(g));
  pwmWrite(LED_B_PIN, LED_CH_B, ledDuty(b));
  ledBrightness = max(r, max(g, b));
}

void statusLedOff() { statusLedSet(0, 0, 0); }

void statusLedIdle() { statusLedSet(255, 255, 255); }

int statusLedBrightness() { return ledBrightness; }
