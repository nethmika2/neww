#pragma once
// ---------------------------------------------------------------------------
// Minimal Arduino core stand-in for the host side build.
// Only the surface the firmware actually uses is implemented; everything is
// host compiled with plain g++ so the project can be verified without the
// (unreachable) ESP32 toolchain.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#include <functional>
#include <cctype>
#include <cstdarg>

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define OUTPUT_OPEN_DRAIN 4
#define PROGMEM
#define F(x) (x)
#ifndef PI
#define PI 3.14159265358979323846f
#endif
#define PI2 (PI * 2)
#define DEG_TO_RAD 0.017453292519943295769236907684886f
#define RAD_TO_DEG 57.295779513082320876798154814105f
#define LED_BUILTIN 2
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_ptr(addr) (*(void *const *)(addr))

#ifndef _BV
#define _BV(b) (1UL << (b))
#endif
#ifndef bitRead
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#endif

// The real core provides these as templates; macros would break <vector>.
template <class T, class U> constexpr auto max(T a, U b) -> decltype(a > b ? a : b) { return a > b ? a : b; }
template <class T, class U> constexpr auto min(T a, U b) -> decltype(a < b ? a : b) { return a < b ? a : b; }
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
inline float radians(float deg) { return deg * DEG_TO_RAD; }
inline float degrees(float rad) { return rad * RAD_TO_DEG; }
inline int random(int maxv) { return maxv > 0 ? (int)(rand() % maxv) : 0; }
inline int random(int minv, int maxv) { return maxv > minv ? minv + (int)(rand() % (maxv - minv)) : minv; }
inline void randomSeed(unsigned long s) { srand((unsigned)s); }

// ---------------------------------------------------------------------------
// FreeRTOS + ESP-IDF basics (the real core pulls these in through Arduino.h)
// ---------------------------------------------------------------------------
typedef int32_t esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NO_MEM 0x101
#endif
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)
#define portTICK_PERIOD_MS 1
#define configMAX_PRIORITIES 25
class HostSemaphore {};
typedef HostSemaphore *SemaphoreHandle_t;
typedef void *TaskHandle_t;
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return new HostSemaphore(); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *out, int) {
  if (out) *out = nullptr;
  return pdPASS;
}
inline BaseType_t xTaskCreate(void (*)(void *), const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *out) {
  if (out) *out = nullptr;
  return pdPASS;
}

// ---------------------------------------------------------------------------
// String: a thin wrapper around std::string with the Arduino names.
// ---------------------------------------------------------------------------
class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned int v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(float v, int decimals = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b; }
  String(double v, int decimals = 2) { char b[40]; snprintf(b, sizeof(b), "%.*f", decimals, v); s_ = b; }

  unsigned int length() const { return (unsigned int)s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  char operator[](unsigned int i) const { return charAt(i); }
  char &operator[](unsigned int i) { return s_[i]; }

  String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(unsigned int from, unsigned int to) const {
    if (from >= s_.size()) return String();
    if (to > s_.size()) to = (unsigned int)s_.size();
    return to <= from ? String() : String(s_.substr(from, to - from));
  }
  int indexOf(char c) const { size_t p = s_.find(c); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *t) const { size_t p = s_.find(t); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &t) const { return indexOf(t.c_str()); }
  int indexOf(char c, unsigned int from) const { size_t p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &t, unsigned int from) const { size_t p = s_.find(t.c_str(), from); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c) const { size_t p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  int lastIndexOf(char c, unsigned int from) const {
    if (s_.empty()) return -1;
    if (from >= s_.size()) from = (unsigned int)s_.size() - 1;
    size_t p = s_.rfind(c, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  int lastIndexOf(const char *t, unsigned int from) const {
    if (from > s_.size()) from = (unsigned int)s_.size();
    size_t p = s_.rfind(t, from);
    return p == std::string::npos ? -1 : (int)p;
  }
  bool startsWith(const String &t) const { return s_.rfind(t.c_str(), 0) == 0; }
  bool startsWith(const char *t) const { return s_.rfind(t, 0) == 0; }
  bool startsWith(const String &t, unsigned int off) const { return off <= s_.size() && s_.rfind(t.s_, off) == off; }
  bool startsWith(const char *t, unsigned int off) const { return off <= s_.size() && s_.rfind(t, off) == off; }
  bool endsWith(const String &t) const {
    return s_.size() >= t.s_.size() && s_.compare(s_.size() - t.s_.size(), t.s_.size(), t.s_) == 0;
  }
  void remove(unsigned int idx) { if (idx < s_.size()) s_.erase(idx); }
  void remove(unsigned int idx, unsigned int count) { if (idx < s_.size()) s_.erase(idx, count); }
  void replace(const String &from, const String &to) {
    size_t p = 0;
    while ((p = s_.find(from.s_, p)) != std::string::npos) { s_.replace(p, from.s_.size(), to.s_); p += to.s_.size(); }
  }
  void trim() {
    size_t b = s_.find_first_not_of(" \t\r\n");
    size_t e = s_.find_last_not_of(" \t\r\n");
    s_ = (b == std::string::npos) ? "" : s_.substr(b, e - b + 1);
  }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (auto &c : s_) c = (char)tolower((unsigned char)c); }
  long toInt() const { return atol(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  double toDouble() const { return atof(s_.c_str()); }
  void concat(const String &t) { s_ += t.s_; }
  bool equals(const String &t) const { return s_ == t.s_; }

  String &operator+=(const String &t) { s_ += t.s_; return *this; }
  String &operator+=(const char *t) { s_ += (t ? t : ""); return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(int v) { s_ += std::to_string(v); return *this; }
  String &operator=(const char *t) { s_ = (t ? t : ""); return *this; }
  bool operator==(const String &t) const { return s_ == t.s_; }
  bool operator==(const char *t) const { return s_ == (t ? t : ""); }
  bool operator!=(const String &t) const { return s_ != t.s_; }
  bool operator!=(const char *t) const { return s_ != (t ? t : ""); }
  bool operator<(const String &t) const { return s_ < t.s_; }
  operator bool() const { return !s_.empty(); }

 private:
  std::string s_;
  friend String operator+(const String &a, const String &b);
};

inline String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
inline String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
inline String operator+(const String &a, char b) { String r(a); r += b; return r; }
inline String operator+(const String &a, int b) { String r(a); r += b; return r; }
inline String operator+(const String &a, unsigned int b) { String r(a); r += String(b); return r; }
inline String operator+(const String &a, long b) { String r(a); r += String(b); return r; }
inline String operator+(const String &a, unsigned long b) { String r(a); r += String(b); return r; }
inline String operator+(const String &a, float b) { String r(a); r += String(b, 2); return r; }
inline String operator+(const String &a, double b) { String r(a); r += String(b, 2); return r; }

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------
class HostSerial {
 public:
  void begin(unsigned long) {}
  void end() {}
  int available() { return 0; }
  int read() { return -1; }
  void flush() {}
  void print(const String &s) { fputs(s.c_str(), stdout); }
  void print(const char *s) { fputs(s ? s : "", stdout); }
  void print(char c) { fputc(c, stdout); }
  void print(int v) { printf("%d", v); }
  void print(unsigned int v) { printf("%u", v); }
  void print(long v) { printf("%ld", v); }
  void print(unsigned long v) { printf("%lu", v); }
  void print(double v, int digits = 2) { printf("%.*f", digits, v); }
  void println() { fputc('\n', stdout); }
  void println(const String &s) { printf("%s\n", s.c_str()); }
  void println(const char *s) { printf("%s\n", s ? s : ""); }
  void println(char c) { printf("%c\n", c); }
  void println(int v) { printf("%d\n", v); }
  void println(unsigned int v) { printf("%u\n", v); }
  void println(long v) { printf("%ld\n", v); }
  void println(unsigned long v) { printf("%lu\n", v); }
  void println(double v, int digits = 2) { printf("%.*f\n", digits, v); }
  void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
};
extern HostSerial Serial;

// ---------------------------------------------------------------------------
// Time & GPIO
// ---------------------------------------------------------------------------
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned long us);
void yield();
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
void analogWrite(uint8_t pin, int val);
uint16_t analogReadMilliVolts(uint8_t pin);
void ledcSetup(uint8_t chan, double freq, uint8_t res);
void ledcAttachPin(uint8_t pin, uint8_t chan);
void ledcWrite(uint8_t chan, uint32_t duty);
double ledcReadFreq(uint8_t chan);

// Advanced I/O helpers used by the ILI9341 driver on real hardware.
struct HostSpiPin {
  HostSpiPin &operator=(int) { return *this; }
};

class EspClass {
 public:
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getHeapSize() { return 320000; }
  uint32_t getMinFreeHeap() { return 150000; }
  uint32_t getPsramSize() { return 0; }
  uint32_t getFreePsram() { return 0; }
  void restart() { exit(0); }
  uint32_t getCpuFreqMHz() { return 240; }
};
extern EspClass ESP;
