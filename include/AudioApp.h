#pragma once
#include <Arduino.h>
#include <SD.h>
#include "BluetoothA2DPSource.h"
#include "Types.h"

// Audio ring buffer & background task
int getRingBufferAvailableWrite();
int getRingBufferAvailableRead();
void audioFeederTask(void* pvParameters);
int32_t get_audio_data(Frame* channels, int32_t frame_count);

// Audio playback management
WavInfo parseWavHeader(File& f);
void loadPlaylist();
void playTrack(int index);
void applyVolume();

// UI & touch handlers
void drawMusicScreen(bool fullWipe);
void handleMusicTouch(bool touched, int sx, int sy);
void drawMusicList();
void handleMusicListTouch(bool touched, int sx, int sy);
