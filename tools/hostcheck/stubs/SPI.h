#pragma once
#include <Arduino.h>

class SPISettings {
 public:
  SPISettings() {}
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) { (void)clock; (void)bitOrder; (void)dataMode; }
};

#define MSBFIRST 1
#define LSBFIRST 0
#define SPI_MODE0 0
#define SPI_MODE1 1
#define SPI_MODE2 2
#define SPI_MODE3 3

class SPIClass {
 public:
  SPIClass(uint8_t bus = 0) : bus_(bus) {}
  void begin(int8_t sck = -1, int8_t miso = -1, int8_t mosi = -1, int8_t ss = -1) {
    (void)sck; (void)miso; (void)mosi; (void)ss;
  }
  void end() {}
  void beginTransaction(SPISettings s) { (void)s; }
  void endTransaction() {}
  uint8_t transfer(uint8_t d) { return d; }
  uint16_t transfer16(uint16_t d) { return d; }
  void transferBytes(const uint8_t *, uint8_t *, uint32_t) {}
  void setFrequency(uint32_t f) { freq_ = f; }
  uint32_t getFrequency() { return freq_; }
  uint8_t busId() { return bus_; }

 private:
  uint8_t bus_;
  uint32_t freq_ = 1000000;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SPI)
extern SPIClass SPI;
extern SPIClass SPI1;
#endif
#define HSPI SPI1
#define VSPI SPI
