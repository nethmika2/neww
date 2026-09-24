#pragma once
// SD card stand-in: a fs::FS instance plus a few helpers used by the harness to
// drop test files (WAV audio) onto the fake card.
#include <Arduino.h>
#include "FS.h"
#include "SPI.h"

class SDClass : public fs::FS {
 public:
  bool begin(uint8_t cs, SPIClass &spi, uint32_t freq) { (void)cs; (void)spi; (void)freq; ready_ = true; return true; }
  bool begin(uint8_t cs, uint32_t freq) { (void)cs; (void)freq; ready_ = true; return true; }
  bool begin() { ready_ = true; return true; }
  uint8_t cardType() { return 2; }
  uint64_t cardSize() { return 1024ULL * 1024ULL * 512ULL; }
  bool ready() const { return ready_; }
  void end() { ready_ = false; }

 private:
  bool ready_ = false;
};
extern SDClass SD;

// Harness helper: writes a valid 16 bit stereo PCM WAV into the fake card.
void hostMakeWav(const char *path, int seconds = 2, int freq = 440, uint32_t sampleRate = 44100);
void hostMakeFile(const char *path, const char *text);
