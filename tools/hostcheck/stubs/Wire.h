#pragma once
#include <Arduino.h>
class TwoWire {
 public:
  TwoWire(uint8_t bus = 0) { (void)bus; }
  bool begin(int sda = -1, int scl = -1, uint32_t freq = 0) { (void)sda; (void)scl; (void)freq; return true; }
  void end() {}
  void setClock(uint32_t f) { (void)f; }
  void beginTransmission(uint8_t addr) { (void)addr; }
  uint8_t endTransmission(bool stop = true) { (void)stop; return 0; }
  uint8_t requestFrom(uint8_t addr, uint8_t len) { (void)addr; (void)len; return 0; }
  size_t write(uint8_t d) { (void)d; return 1; }
  int read() { return -1; }
  int available() { return 0; }
};
extern TwoWire Wire;
