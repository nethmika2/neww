#pragma once
#include <Arduino.h>
#include <SD.h>
#include "BluetoothA2DPSource.h"
#include "Types.h"

// Audio ring buffer & background task
int getRingBufferAvailableWrite();
int getRingBufferAvailableRead();
int allocAudioRing(uint32_t reserveBytes, bool verbose);   // picks the ring size, 0 when none fits
void audioRingService();       // allocates the ring once a connection has settled
void audioFeederTask(void* pvParameters);
int32_t get_audio_data(Frame* channels, int32_t frame_count);

// One pass of the feeder: tops the ring up from the open file.  Returns the
// number of bytes moved, 0 when there is nothing to do (ring full, paused, no
// file, end of file already reached).  audioFeederTask() calls it in a loop;
// the host tests call it directly.
int audioFeederStep();

// Telemetry, printed as a single [I][audio] line so a stutter can be told apart
// from a slow card and from a starved Bluetooth link.
uint32_t audioBytesFed();
uint32_t audioStarveCount();
uint32_t audioSilenceBytes();
uint32_t audioFeederPasses();
void audioStatsReset();
void audioLogStats(const char* why);
void audioLogStatsIfDue();

// Audio playback management
WavInfo parseWavHeader(File& f);
void loadPlaylist();
void playTrack(int index);
void applyVolume();

// UI & touch handlers
void drawMusicScreen(bool fullWipe);
void drawMusicStatus();  // connection + volume pills
void handleMusicTouch(bool touched, int sx, int sy);
void drawMusicList();
void handleMusicListTouch(bool touched, int sx, int sy);
