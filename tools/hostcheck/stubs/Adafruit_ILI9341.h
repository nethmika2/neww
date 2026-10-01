#pragma once
#include <Arduino.h>
#include "Adafruit_GFX.h"
#include "SPI.h"

// Host stand-in for the ILI9341 driver: keeps the constructors and the extra
// calls the firmware makes, and records the rest through the GFX base class.
class Adafruit_ILI9341 : public Adafruit_GFX {
 public:
  Adafruit_ILI9341(int8_t cs, int8_t dc, int8_t rst = -1) : Adafruit_GFX(HOST_SCREEN_W, HOST_SCREEN_H) { (void)cs; (void)dc; (void)rst; }
  Adafruit_ILI9341(SPIClass *spi, int8_t cs, int8_t dc, int8_t rst = -1) : Adafruit_GFX(HOST_SCREEN_W, HOST_SCREEN_H) { (void)spi; (void)cs; (void)dc; (void)rst; }
  void begin(uint32_t freq = 40000000) { (void)freq; }
  void setSPISpeed(uint32_t freq) { (void)freq; }
  void invertDisplay(bool i) { (void)i; }
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) { (void)x; (void)y; (void)w; (void)h; }
  void pushColor(uint16_t c) { writePixel(cursorX, cursorY, c); }
  void fillScreen(uint16_t c) { Adafruit_GFX::fillScreen(c); }
  void scrollTo(uint16_t y) { (void)y; }
  void setScrollMargins(uint16_t t, uint16_t b) { (void)t; (void)b; }
  void readRect(int16_t, int16_t, int16_t, int16_t, uint16_t *) {}
};
