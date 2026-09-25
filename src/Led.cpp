#include "Led.h"
#include "Config.h"
#include "Globals.h"
#include "DisplayUtils.h"

// The Arduino core changed the LEDC API in 3.x (attach by pin instead of by
// channel), so both are handled here; the rest of the firmware only sees the
// small interface above.
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

static uint8_t backlightDuty = TFT_BL_FULL_DUTY;

void setBacklight(uint8_t duty) {
  backlightDuty = duty;
  pwmWrite(TFT_BL, TFT_BL_CH, duty);
}

int backlightLevel() { return backlightDuty; }

// ==========================================
// LEVELS AND COLOURS
// ==========================================
// Nothing here runs the LED at the full 255 by default: at full white it is
// uncomfortably bright in a dark room.  HIGH stays available for a power bank
// that needs convincing.
static const uint8_t LEVEL_PEAK[LED_L_COUNT] = { 45, 110, 255 };

uint8_t ledLevelPeak(LedLevel level) {
  return LEVEL_PEAK[constrain((int)level, 0, LED_L_COUNT - 1)];
}

// Colours are the peak of each channel: the pattern scales them down from here.
static const uint8_t COLOR_RGB[LED_C_COUNT][3] = {
    { 0, 90, 255 },     // BLUE
    { 150, 40, 255 },   // VIOLET
    { 0, 255, 90 },     // GREEN
    { 255, 140, 0 },    // AMBER
    { 255, 255, 255 },  // WHITE
    { 255, 0, 0 },      // RED
};

const char* ledColorName(LedColor c) {
  static const char* const names[LED_C_COUNT] = { "BLUE", "VIOLET", "GREEN", "AMBER", "WHITE", "RED" };
  return names[constrain((int)c, 0, LED_C_COUNT - 1)];
}

const char* ledEffectName(LedEffect e) {
  static const char* const names[LED_E_COUNT] = { "FADE", "BREATHE", "CYCLE", "PULSE", "SOLID" };
  return names[constrain((int)e, 0, LED_E_COUNT - 1)];
}

const char* ledLevelName(LedLevel l) {
  static const char* const names[LED_L_COUNT] = { "LOW", "MED", "HIGH" };
  return names[constrain((int)l, 0, LED_L_COUNT - 1)];
}

// ==========================================
// OUTPUT
// ==========================================
// The board wires the LED to 3V3 with the GPIO pulling it down, so a LOW pin is
// "on".  Duty is inverted here, which lets everything else talk about
// brightness the way a person expects.
static uint8_t ledDuty(uint8_t brightness) {
#if LED_ACTIVE_LOW
  return (uint8_t)(LED_PWM_MAX - brightness);
#else
  return brightness;
#endif
}

static uint8_t nowChannels[3] = { 0, 0, 0 };
static uint8_t nowBrightness = 0;

void statusLedSet(uint8_t r, uint8_t g, uint8_t b) {
  pwmWrite(LED_R_PIN, LED_CH_R, ledDuty(r));
  pwmWrite(LED_G_PIN, LED_CH_G, ledDuty(g));
  pwmWrite(LED_B_PIN, LED_CH_B, ledDuty(b));
  nowChannels[0] = r;
  nowChannels[1] = g;
  nowChannels[2] = b;
  nowBrightness = max(r, max(g, b));
}

void statusLedOff() { statusLedSet(0, 0, 0); }

int statusLedBrightness() { return nowBrightness; }

void ledChannelsNow(uint8_t* r, uint8_t* g, uint8_t* b) {
  if (r) *r = nowChannels[0];
  if (g) *g = nowChannels[1];
  if (b) *b = nowChannels[2];
}

int ledBrightnessNow() { return nowBrightness; }

void statusLedBegin() {
  pwmBegin(LED_R_PIN, LED_CH_R);
  pwmBegin(LED_G_PIN, LED_CH_G);
  pwmBegin(LED_B_PIN, LED_CH_B);
  statusLedOff();
}

// ==========================================
// PATTERNS
// ==========================================
// Every pattern is a 0..255 shape over time, except that the steady ones keep a
// floor: the point of this light is partly to keep a USB power bank awake, so it
// is never allowed to look switched off.
static const int LED_FLOOR = 56;              // ~22% of the pattern amplitude
static const int FADE_PERIOD_MS = 2600;       // one up-and-down
static const int BREATHE_PERIOD_MS = 3400;
static const int PULSE_PERIOD_MS = 1300;
static const int CYCLE_PERIOD_MS = 7200;      // a full trip round the colour wheel
static const int ALERT_PERIOD_MS = 220;       // one flash of the announcement

// Linear up-then-down: the shape a "fade" is supposed to have.  Both halves run
// at the same slope, so it reads as a straight ramp rather than a curve.
static uint8_t triangle(unsigned long phase, int period) {
  int half = period / 2;
  int t = (int)(phase % period);
  int v = (t < half) ? (t * 255 / half) : ((period - t) * 255 / half);
  return (uint8_t)constrain(v, 0, 255);
}

// Smooth sine breathing, the calm one.
static uint8_t breathe(unsigned long phase, int period) {
  float t = (float)(phase % period) / (float)period;
  float v = (1.0f - cosf(t * 2.0f * PI)) * 0.5f;
  return (uint8_t)constrain((int)(v * 255.0f), 0, 255);
}

// Fast rise, instant drop: reads as a heartbeat/ping.
static uint8_t sawtooth(unsigned long phase, int period) {
  int t = (int)(phase % period);
  return (uint8_t)constrain(t * 255 / (period - 1), 0, 255);
}

// Hue wheel, so CYCLE walks through every colour on its own.
static void hueToRgb(uint8_t hue, uint8_t* r, uint8_t* g, uint8_t* b) {
  uint8_t region = hue / 43;                  // 0..5
  uint8_t rem = (hue - region * 43) * 6;      // 0..255 within the region
  uint8_t q = (uint8_t)(255 - rem);
  uint8_t t = rem;
  switch (region) {
    case 0: *r = 255; *g = t; *b = 0; break;
    case 1: *r = q; *g = 255; *b = 0; break;
    case 2: *r = 0; *g = 255; *b = t; break;
    case 3: *r = 0; *g = q; *b = 255; break;
    case 4: *r = t; *g = 0; *b = 255; break;
    default: *r = 255; *g = 0; *b = q; break;
  }
}

// ==========================================
// THE RUNNING REQUEST
// ==========================================
static LedColor reqColor = LED_C_BLUE;
static LedEffect reqEffect = LED_E_FADE;
static LedLevel reqLevel = LED_L_MED;
static bool reqOn = false;

static unsigned long previewUntil = 0;
static LedColor previewColor = LED_C_BLUE;
static LedEffect previewEffect = LED_E_FADE;
static LedLevel previewLevel = LED_L_MED;

static unsigned long alertUntil = 0;

void ledRequest(LedColor color, LedEffect effect, LedLevel level) {
  reqColor = color;
  reqEffect = effect;
  reqLevel = level;
  reqOn = true;
}

void ledRequestOff() { reqOn = false; }

void ledAlert(uint16_t ms) { alertUntil = ms ? (millis() + ms) : 0; }

void ledPreview(LedColor color, LedEffect effect, LedLevel level) {
  previewColor = color;
  previewEffect = effect;
  previewLevel = level;
  previewUntil = millis() + 2500;
}

bool ledPreviewActive() { return previewUntil && (long)(millis() - previewUntil) < 0; }

bool ledAlertActiveForTest() { return alertUntil && (long)(millis() - alertUntil) < 0; }

static void render(LedColor color, LedEffect effect, LedLevel level, unsigned long phase) {
  uint8_t peak = ledLevelPeak(level);
  uint8_t shape;
  switch (effect) {
    case LED_E_FADE: shape = LED_FLOOR + (int)triangle(phase, FADE_PERIOD_MS) * (255 - LED_FLOOR) / 255; break;
    case LED_E_BREATHE: shape = LED_FLOOR + (int)breathe(phase, BREATHE_PERIOD_MS) * (255 - LED_FLOOR) / 255; break;
    case LED_E_PULSE: shape = LED_FLOOR + (int)sawtooth(phase, PULSE_PERIOD_MS) * (255 - LED_FLOOR) / 255; break;
    case LED_E_CYCLE: shape = LED_FLOOR + (int)breathe(phase, CYCLE_PERIOD_MS / 4) * (255 - LED_FLOOR) / 255; break;
    default: shape = 255; break;
  }
  int scale = (int)shape * (int)peak / 255;
  uint8_t r, g, b;
  if (effect == LED_E_CYCLE) {
    uint8_t hue = (uint8_t)((phase % CYCLE_PERIOD_MS) * 255 / CYCLE_PERIOD_MS);
    hueToRgb(hue, &r, &g, &b);
    r = (uint8_t)((int)r * scale / 255);
    g = (uint8_t)((int)g * scale / 255);
    b = (uint8_t)((int)b * scale / 255);
  } else {
    const uint8_t* c = COLOR_RGB[constrain((int)color, 0, LED_C_COUNT - 1)];
    r = (uint8_t)((int)c[0] * scale / 255);
    g = (uint8_t)((int)c[1] * scale / 255);
    b = (uint8_t)((int)c[2] * scale / 255);
  }
  statusLedSet(r, g, b);
}

void ledTick() {
  unsigned long now = millis();
  // An announcement overrides everything for its couple of seconds.
  if (alertUntil && (long)(now - alertUntil) < 0) {
    unsigned long phase = now % ALERT_PERIOD_MS;
    uint8_t v = (phase < ALERT_PERIOD_MS / 2) ? 255 : 0;
    statusLedSet(v, 0, 0);
    return;
  }
  if (ledPreviewActive()) {
    render(previewColor, previewEffect, previewLevel, now);
    return;
  }
  if (!reqOn) {
    if (nowBrightness) statusLedOff();
    return;
  }
  render(reqColor, reqEffect, reqLevel, now);
}

// ==========================================
// WHAT THE DEVICE IS DOING
// ==========================================
// One place maps state to light, so every screen gets the same behaviour and
// there is nothing to keep in sync:
//
//   dark screen          - the keep-awake pattern, in the colour you chose
//   work phase running   - amber/red linear fade (focus)
//   short break running  - green breathing (rest)
//   long break running   - blue breathing (rest)
//   music playing        - the colour wheel
//   work blocked done    - three red flashes (from the timer itself)
void updateStatusLed() {
  if (alertUntil && (long)(millis() - alertUntil) < 0) {
    ledTick();
    return;
  }
  if (ledPreviewActive()) {
    ledTick();
    return;
  }
  if (!screenOn) {
    // The panel is dark: this is the load that keeps a power bank awake, so it
    // runs in every idle mode and is not affected by the follow-apps switch.
    ledRequest(ledColor, ledEffect, ledLevel);
    ledTick();
    return;
  }
  if (ledFollowApps) {
    if (pomoRunning) {
      if (pomoMode == MODE_WORK) ledRequest(LED_C_AMBER, LED_E_FADE, LED_L_MED);
      else if (pomoMode == MODE_SHORT_BREAK) ledRequest(LED_C_GREEN, LED_E_BREATHE, LED_L_MED);
      else ledRequest(LED_C_BLUE, LED_E_BREATHE, LED_L_MED);
      ledTick();
      return;
    }
    if (isPlaying) {
      ledRequest(LED_C_BLUE, LED_E_CYCLE, LED_L_MED);
      ledTick();
      return;
    }
  }
  if (nowBrightness) statusLedOff();
}
