#pragma once
// Host stand-in for the Adafruit GFX library: same public surface, but every
// primitive is recorded instead of being pushed over SPI.
#include <Arduino.h>
#include "HostDraw.h"

#define HOST_SCREEN_W 320
#define HOST_SCREEN_H 240

typedef uint16_t GFXcolor;
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0

struct GFXfont {
  const void *bitmap;
  const void *glyph;
  uint8_t first;
  uint8_t last;
  uint8_t yAdvance;
};

// Print is only used to make the class compatible with Print based helpers.
class Print {
 public:
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t *buf, size_t size) = 0;
  void print(const char *s) { write((const uint8_t *)s, strlen(s)); }
  virtual ~Print() {}
};

class Adafruit_GFX : public Print {
 public:
  Adafruit_GFX(int16_t w, int16_t h) : WIDTH(w), HEIGHT(h) {}
  virtual ~Adafruit_GFX() {}

  void begin(int) {}
  void setRotation(uint8_t r) { rotation = r; }
  uint8_t getRotation() { return rotation; }
  int16_t width() { return WIDTH; }
  int16_t height() { return HEIGHT; }
  int16_t width(void) const { return WIDTH; }
  int16_t height(void) const { return HEIGHT; }

  void startWrite() {}
  void endWrite() {}
  void writePixel(int16_t x, int16_t y, uint16_t c) {
    if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
    HostDraw::add("px " + std::to_string(x) + " " + std::to_string(y) + " " + hostColor(c));
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) { writePixel(x, y, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void fillScreen(uint16_t c) { HostDraw::add("fillScreen " + hostColor(c)); }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    HostDraw::add("fillRect " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                  std::to_string(h) + " " + hostColor(c));
  }
  void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    if (w <= 0 || h <= 0) return;
    HostDraw::add("rect " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                  std::to_string(h) + " " + hostColor(c));
  }
  void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    HostDraw::add("rrect " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                  std::to_string(h) + " " + std::to_string(r) + " " + hostColor(c));
  }
  void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c) {
    HostDraw::add("frrect " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(w) + " " +
                  std::to_string(h) + " " + std::to_string(r) + " " + hostColor(c));
  }
  void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t c) {
    HostDraw::add("circle " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(r) + " " + hostColor(c));
  }
  void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t c) {
    HostDraw::add("disc " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(r) + " " + hostColor(c));
  }
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t c) {
    HostDraw::add("line " + std::to_string(x0) + " " + std::to_string(y0) + " " + std::to_string(x1) + " " +
                  std::to_string(y1) + " " + hostColor(c));
  }
  void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
    drawLine(x0, y0, x1, y1, c);
    drawLine(x1, y1, x2, y2, c);
    drawLine(x2, y2, x0, y0, c);
  }
  void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t c) {
    HostDraw::add("ftri " + std::to_string(x0) + " " + std::to_string(y0) + " " + std::to_string(x1) + " " +
                  std::to_string(y1) + " " + std::to_string(x2) + " " + std::to_string(y2) + " " + hostColor(c));
  }
  void setFont(const GFXfont *f) { currentFont = f; }
  void setTextColor(uint16_t c) { textColor = c; textBg = c; hasBg = false; }
  void setTextColor(uint16_t c, uint16_t bg) { textColor = c; textBg = bg; hasBg = true; }
  void setTextSize(uint8_t s) { textSize = s; }
  void setTextWrap(bool w) { wrap = w; }
  void setCursor(int16_t x, int16_t y) { cursorX = x; cursorY = y; }
  int16_t getCursorX() { return cursorX; }
  int16_t getCursorY() { return cursorY; }
  void getTextBounds(const char *s, int16_t x, int16_t y, int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {
    size_t n = strlen(s);
    *x1 = x;
    *y1 = y;
    *w = (uint16_t)(n * 6 * textSize);
    *h = (uint16_t)(8 * textSize);
  }
  size_t write(uint8_t c) override { return write((const char *)&c, 1); }
  size_t write(const uint8_t *buf, size_t size) override { return write((const char *)buf, size); }
  size_t write(const char *str) { return str ? write(str, strlen(str)) : 0; }
  size_t write(const char *buf, size_t size) {
    if (!buf || size == 0) return 0;
    std::string s(buf, size);
    if (hasBg) {
      // Only the built-in font supports a background fill in this firmware, and
      // it advances 6 px per character at text size 1.
      int16_t w = (int16_t)(s.size() * 6 * textSize);
      int16_t h = (int16_t)(8 * textSize);
      fillRect(cursorX, cursorY, w, h, textBg);
    }
    // The font identity is recorded through its line height, which is what the
    // renderer needs to pick a matching face.  textSize only applies to the
    // built-in font (Adafruit_GFX ignores it for custom fonts), so it is
    // recorded as 1 whenever a GFX font is selected.
    HostDraw::add("text " + std::to_string(cursorX) + " " + std::to_string(cursorY) + " " +
                  std::to_string(currentFont ? 1 : (int)textSize) + " " +
                  std::to_string(currentFont ? (int)currentFont->yAdvance : 0) +
                  " " + hostColor(textColor) + " " + s);
    cursorX += (int16_t)(s.size() * 6 * textSize);
    return size;
  }
  void print(const String &s) { write(s.c_str()); }
  void print(const char *s) { write(s); }
  void print(char c) { write((uint8_t)c); }
  void print(int v) { write(String(v).c_str()); }
  void print(unsigned int v) { write(String(v).c_str()); }
  void print(long v) { write(String(v).c_str()); }
  void print(unsigned long v) { write(String(v).c_str()); }
  void print(double v, int digits = 2) { write(String(v, digits).c_str()); }
  void printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write(buf);
  }

 protected:
  int16_t WIDTH, HEIGHT;
  uint8_t rotation = 0;
  const GFXfont *currentFont = nullptr;
  uint16_t textColor = 0xFFFF, textBg = 0;
  bool hasBg = false;
  uint8_t textSize = 1;
  bool wrap = true;
  int16_t cursorX = 0, cursorY = 0;
};

