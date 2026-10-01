#pragma once
#include <Arduino.h>
#include <SD.h>
#include "BluetoothA2DPSource.h"
#include "Types.h"

// Audio ring buffer & background task
int getRingBufferAvailableWrite();
int getRingBufferAvailableRead();
// Data path selection: direct SD reads from the callback (default, matches the
// older working builds) or the ring buffer + feeder task.
void audioSetSourceDirect(bool direct);
#ifdef HOSTCHECK
void audioSetFileBusyForTest(bool busy);
#endif
bool audioSourceDirect();

int allocAudioRing(uint32_t reserveBytes, bool verbose);   // picks the ring size, 0 when none fits
void audioRingService();       // allocates the ring once a connection has settled
void audioRingServiceReset();      // back to power-on state (host tests)
void audioLinkRecoveryReset();     // ditto for the recovery gate
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
uint32_t audioOutBytes();      // bytes handed to the Bluetooth stack
uint32_t audioOutCalls();      // data callbacks served
uint32_t audioOutKBps();       // the above per second, since the last report
bool audioLinkBehind();        // the stack has been pulling under real time
unsigned long audioLinkBehindMs();  // for how long, continuously
int audioStateNow();           // last A2DP audio state the stack reported (-1 = none)
void audioStateCallback(esp_a2d_audio_state_t state, void*);   // register before start()
void audioLinkVerdictClear();      // forget the accumulated "behind" time
bool audioLinkRecoveryService();   // restarts a throttled stream (see the .cpp)
int audioLinkRebuildCount();

// Earbud (re)connection.  btStartSource() is the one place a2dp_source.start() is
// called from: it switches auto-reconnect on and hands the library the remembered
// earbud address, so the earbuds connect without being put in pairing mode.
// btRememberPeer() stores the address once a link is up.
void btStartSource();
void btRememberPeer();
bool btSavedPeer(uint8_t out[6]);
void btForgetPeer();       // full session rebuilds since boot
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
