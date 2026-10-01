#include "AudioApp.h"
#include <esp_bt.h>
#include "Globals.h"
#include "DisplayUtils.h"

// Forward declaration
void drawHomeScreen();

// ==========================================
// RING BUFFER & AUDIO BACKGROUND TASK
// ==========================================
int getRingBufferAvailableWrite() {
  int head = ringHead, tail = ringTail;
  if (head >= tail) return (audioRingBytes - 1) - (head - tail);
  return tail - head - 1;
}

int getRingBufferAvailableRead() {
  int head = ringHead, tail = ringTail;
  if (head >= tail) return head - tail;
  return audioRingBytes - (tail - head);
}


// ==========================================
// EARBUD AUTO-RECONNECT
// ==========================================
// Why the earbuds used to need pairing mode: the A2DP library starts with
// auto-reconnect OFF (whatever its header comment says), so every start() went
// straight to scanning for the earbuds by name.  A scan only finds a device that
// is discoverable, i.e. in pairing mode.  It did not even save the address of the
// earbuds it had connected to, and its end() wipes the address it holds, which a
// session rebuild calls.  So the address is kept here, in our own preferences.
//
// With the address known the stack *pages* the earbuds directly.  That needs
// no pairing mode, only the link key from the first pairing, which Bluedroid
// keeps in flash.
static const char* const BT_PEER_KEY = "btpeer";

bool btSavedPeer(uint8_t out[6]) {
  if (prefs.getBytesLength(BT_PEER_KEY) != 6) return false;
  if (prefs.getBytes(BT_PEER_KEY, out, 6) != 6) return false;
  for (int i = 0; i < 6; i++) if (out[i]) return true;
  return false;                       // all zero is "none", not an address
}

void btForgetPeer() { prefs.remove(BT_PEER_KEY); }

void btStartSource() {
  uint8_t addr[6];
  if (btSavedPeer(addr)) {
    // Page the remembered earbuds first; fall back to scanning only after
    // BT_RECONNECT_TRIES attempts.
    a2dp_source.set_auto_reconnect(addr, BT_RECONNECT_TRIES);
    Serial.printf("[I][bt] reconnecting to saved earbuds %02X:%02X:%02X:%02X:%02X:%02X\n",
                  addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  } else {
    // Nothing saved yet: the first connection is made by scanning (earbuds in
    // pairing mode), then btRememberPeer() stores the address.
    a2dp_source.set_auto_reconnect(true, BT_RECONNECT_TRIES);
    Serial.printf("[I][bt] no saved earbuds, scanning for \"%s\" (use pairing mode once)\n", EARBUD_NAME);
  }
  a2dp_source.start(EARBUD_NAME, get_audio_data);
}

// Called when the link comes up.  The library records the peer it connected to;
// copy it into our preferences if it is new.
void btRememberPeer() {
  esp_bd_addr_t* peer = a2dp_source.get_last_peer_address();
  if (!peer) return;
  uint8_t cur[6];
  bool zero = true;
  for (int i = 0; i < 6; i++) { cur[i] = (*peer)[i]; if (cur[i]) zero = false; }
  if (zero) return;
  uint8_t saved[6];
  if (btSavedPeer(saved) && memcmp(saved, cur, 6) == 0) return;
  prefs.putBytes(BT_PEER_KEY, cur, 6);
  Serial.printf("[I][bt] saved earbuds %02X:%02X:%02X:%02X:%02X:%02X for automatic reconnect\n",
                cur[0], cur[1], cur[2], cur[3], cur[4], cur[5]);
}

// ==========================================
// TELEMETRY
// ==========================================
// Cheap counters, so the serial log can say whether a stutter came from the SD
// side (fed KB/s well under 176), from the Bluetooth side (starve count rising
// while the feed rate is fine) or from nowhere (both healthy - then the stutter
// is on the earbud side).
// Data path in use.  Direct reads are the older, working design: the audio
// callback reads the SD file itself.  The ring path stays for comparison and is
// still exercised by the host tests.
static bool audioDirect = (AUDIO_DIRECT_READ != 0);

void audioSetSourceDirect(bool direct) { audioDirect = direct; }
bool audioSourceDirect() { return audioDirect; }

// Set while the main loop reopens or seeks the file: the callback must not touch
// a File that is being closed underneath it.  (The older builds relied on luck
// here; a flag costs nothing.)
static volatile bool audioFileBusy = false;

static volatile uint32_t stBytesFed = 0;
// What the Bluetooth stack pulled from us.  The A2DP source asks for 44100*4
// bytes a second while the link is healthy, so this is the figure that says
// whether a stutter is ours (low) or the radio's (the stack asking for less).
static volatile uint32_t stOutBytes = 0;    // cumulative, never reset by logging
static volatile uint32_t stOutCalls = 0;
static unsigned long stLogBytes = 0;        // snapshot the log line measures from
static unsigned long stLogCalls = 0;
static unsigned long stLogAt = 0;
// The "is the link keeping up" verdict has its own window.  It used to share the
// telemetry window, which meant every report reset it - so a link that was
// behind for minutes never accumulated the continuous time the recovery gate
// waits for, and the rebuild could never fire.
static uint32_t stLinkBytes = 0;
static unsigned long stLinkAt = 0;
static unsigned long stBehindMs = 0;
static volatile uint32_t stStarve = 0;
static volatile uint32_t stSilenceBytes = 0;
static volatile uint32_t stPasses = 0;
static volatile int stLastStarveFree = 0;   // last shortfall, read by the loop
static volatile int stLastStarveNeed = 0;
static unsigned long stLastLog = 0;
static uint32_t stLastLogBytes = 0;

uint32_t audioBytesFed() { return stBytesFed; }
uint32_t audioStarveCount() { return stStarve; }
uint32_t audioSilenceBytes() { return stSilenceBytes; }
uint32_t audioFeederPasses() { return stPasses; }

// ==========================================
// AUDIO STATE (what the sink told the stack)
// ==========================================
// The library keeps its own copy of the A2DP audio state, but it only updates
// that copy when a callback is registered - so until now our "state N" in the
// log was the library's unset default and told us nothing.  This records it, and
// it is set from the Bluetooth task, so it does nothing but store a byte.
static volatile int stAudioState = -1;
void audioStateCallback(esp_a2d_audio_state_t state, void*) {
  stAudioState = (int)state;
}
int audioStateNow() { return stAudioState; }
uint32_t audioOutBytes() { return stOutBytes; }
uint32_t audioOutCalls() { return stOutCalls; }

// Rate over the current telemetry window (what the log line reports).
uint32_t audioOutKBps() {
  unsigned long dt = millis() - stLogAt;
  if (dt == 0) return 0;
  return (uint32_t)(((uint64_t)(stOutBytes - stLogBytes) * 1000ULL) / ((uint64_t)dt * 1024ULL));
}

// How long the link has been continuously behind.  Judged over 8 second windows
// so a single slow moment cannot trip it, and kept independent of the logging.
static void audioLinkVerdictUpdate() {
  unsigned long now = millis();
  if (!isPlaying || !btInitialized) {
    stBehindMs = 0;
    stLinkBytes = stOutBytes;
    stLinkAt = now;
    return;
  }
  unsigned long dt = now - stLinkAt;
  if (dt < 8000) return;
  uint32_t got = stOutBytes - stLinkBytes;
  uint32_t kbps = (uint32_t)(((uint64_t)got * 1000ULL) / ((uint64_t)dt * 1024ULL));
  if (kbps < AUDIO_LINK_WARN_KBPS) {
    stBehindMs += dt;
    if (stBehindMs > 600000UL) stBehindMs = 600000UL;
  } else {
    stBehindMs = 0;
  }
  stLinkBytes = stOutBytes;
  stLinkAt = now;
}

unsigned long audioLinkBehindMs() { return stBehindMs; }

// Forgets the accumulated degradation, so a recovery action can be judged on
// what happens next rather than on the history that triggered it.
void audioLinkVerdictClear() {
  stBehindMs = 0;
  stLinkBytes = stOutBytes;
  stLinkAt = millis();
}

// True while the stack is pulling well under real time: the A2DP transmit queue
// is backed up, which means the air link cannot drain it.  The earbuds starve in
// cycles when this lasts, and it is not something the app can feed its way out
// of - the radio is the limit.
bool audioLinkBehind() {
  return isPlaying && btInitialized && stBehindMs >= 8000;
}

void audioStatsReset() {
  stBytesFed = 0;
  stOutBytes = 0;
  stOutCalls = 0;
  stLogBytes = 0;
  stLogCalls = 0;
  stLogAt = millis();
  stLinkBytes = 0;
  stLinkAt = millis();
  stBehindMs = 0;
  stStarve = 0;
  stSilenceBytes = 0;
  stPasses = 0;
  stLastLogBytes = 0;
  stLastLog = millis();
}

// Records a shortfall for the main loop's telemetry line.
static void audioNoteStarve(int have, int need) {
  // NOTE: this runs in the Bluetooth task (the A2DP data callback).  That task
  // has a few KB of stack and Serial.printf needs the better part of one, so
  // nothing is printed from here - the main loop reports the counters instead.
  stStarve++;
  stLastStarveFree = have;
  stLastStarveNeed = need;
}

void audioLogStats(const char* why) {
  unsigned long now = millis();
  unsigned long dt = now - stLastLog;
  if (dt == 0) dt = 1;
  uint32_t fed = stBytesFed;
  uint32_t fedKbps = (uint32_t)(((uint64_t)(fed - stLastLogBytes) * 1000ULL) / (dt * 1024ULL));
  int fill = 0;
  if (audioRingBuffer && audioRingBytes > 0) fill = (getRingBufferAvailableRead() * 100) / audioRingBytes;
  // "out" is what the Bluetooth stack took; "fed" is what the SD side delivered.
  // A stutter with out < ~170 KB/s is the link, with out healthy it is not.
  // The calls figure has to be a delta too: the counter is cumulative, and
  // dividing it by the window made it climb without bound ("1698 calls/s").
  uint32_t callsKbps = (uint32_t)((stOutCalls - stLogCalls) * 1000UL / (dt ? dt : 1));
  Serial.printf("[I][audio] %s: out %lu KB/s (%lu calls/s), fed %lu KB/s, ring %d%%, starve %lu (%lu ms silence), behind %lu s, state %d\n",
                why, (unsigned long)audioOutKBps(),
                (unsigned long)callsKbps,
                (unsigned long)fedKbps, fill,
                (unsigned long)stStarve, (unsigned long)(stSilenceBytes / 176),
                (unsigned long)(stBehindMs / 1000),
                (int)a2dp_source.get_audio_state());
  stLastLog = now;
  stLastLogBytes = fed;
  stLogBytes = stOutBytes;
  stLogCalls = stOutCalls;
  stLogAt = now;
}

void audioLogStatsIfDue() {
  // Runs every loop iteration, so the verdict keeps its own clock whether or not
  // anything is being logged.
  audioLinkVerdictUpdate();
  if (!btInitialized || !isPlaying) return;
  unsigned long since = millis() - stLastLog;
  // Quiet by default (every 30 s) - the serial monitor should not be a firehose
  // for a healthy stream - but speak up quickly while the link is behind, which
  // is exactly when the log is worth reading.
  if (audioLinkBehind()) {
    if (since < 5000) return;
    audioLogStats(stBehindMs >= LINK_BEHIND_MS ? "behind, recovery due" : "link behind");
    return;
  }
  if (since < AUDIO_LOG_PERIOD_MS) return;
  audioLogStats("30s");
}

// ==========================================
// FEEDER
// ==========================================
// One pass: read a block from the file and push it into the ring.  The mutex is
// taken here so the step is safe to call from the task and from a test alike.
int audioFeederStep() {
  static uint8_t tempBuf[FEEDER_CHUNK];
  if (!isPlaying || !audioRingBuffer || audioRingBytes <= 0 || !audioFile) return 0;
  if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
  int moved = 0;
  if (audioFile) {
    if (!fileReadDone) {
      int space = getRingBufferAvailableWrite();
      // Fill whenever there is room for a useful block.  The old code waited for
      // 512 bytes of room and then slept 1 ms after every read, which capped the
      // feed rate for no reason.
      if (space >= 256) {
        int want = min(space, (int)sizeof(tempBuf));
        int got = audioFile.read(tempBuf, want);
        if (got > 0) {
          for (int i = 0; i < got; i++) {
            audioRingBuffer[ringHead] = tempBuf[i];
            ringHead = (ringHead + 1) % audioRingBytes;
          }
          audioStreamPos += got;
          stBytesFed += got;
          moved = got;
        }
        if (got <= 0 || audioFile.available() == 0) fileReadDone = true;
      }
    } else if (getRingBufferAvailableRead() < 16) {
      trackFinished = true;
      isPlaying = false;
      fileReadDone = false;
    }
  } else {
    isPlaying = false;
  }
  xSemaphoreGive(audioMutex);
  stPasses++;
  return moved;
}

void audioFeederTask(void* pvParameters) {
  (void)pvParameters;
  while (true) {
    // Keep reading while there is room; only sleep when the ring is full or
    // there is nothing to play, which is when a delay costs nothing.
    if (audioFeederStep() > 0) taskYIELD();
    else vTaskDelay(pdMS_TO_TICKS(1));
  }
}

int32_t get_audio_data(Frame* channels, int32_t frame_count) {
  int bytesNeeded = frame_count * sizeof(Frame);
  if (!channels || frame_count <= 0) return frame_count;

  if (audioDirect) {
    // Read the file right here, the way the working builds did.  Nothing sits
    // between the card and the radio: no ring to keep full, no feeder task
    // competing for the CPU and no extra heap held while the stack connects.
    if (!audioSystemReady || audioFileBusy || !audioFile || !isPlaying) {
      memset(channels, 0, bytesNeeded);
      return frame_count;
    }
    int got = audioFile.read((uint8_t*)channels, bytesNeeded);
    if (got < 0) got = 0;
    if (got < bytesNeeded) {
      memset(((uint8_t*)channels) + got, 0, bytesNeeded - got);
      // Once per file: the main loop advances to the next track.
      if (!fileReadDone) {
        fileReadDone = true;
        trackFinished = true;
      }
    }
    int g = audioGainQ8;
    if (g != 256 && got > 0) {
      int16_t* smp = (int16_t*)channels;
      int n = got / 2;
      for (int i = 0; i < n; i++) smp[i] = (int16_t)((smp[i] * g) >> 8);
    }
    audioStreamPos += got;
    stBytesFed += got;
    stOutBytes += (uint32_t)bytesNeeded;
    stOutCalls++;
    return frame_count;
  }

  if (!audioSystemReady || !audioRingBuffer || audioRingBytes <= 0) {
    memset(channels, 0, bytesNeeded);
    return frame_count;
  }
  uint8_t* dest = (uint8_t*)channels;
  int avail = isPlaying ? getRingBufferAvailableRead() : 0;
  if (avail > 0) {
    // Whole frames only: a file that ends mid-frame must not shift the stereo
    // pairs of everything that follows.
    int n = min(avail, bytesNeeded) & ~3;
    for (int i = 0; i < n; i++) {
      dest[i] = audioRingBuffer[ringTail];
      ringTail = (ringTail + 1) % audioRingBytes;
    }
    if (n < bytesNeeded) {
      // Hand over what is there instead of a whole packet of silence: the link
      // stays fed and the earbuds cover the shortfall from their own buffer.
      memset(dest + n, 0, bytesNeeded - n);
      stSilenceBytes += (uint32_t)(bytesNeeded - n);
      audioNoteStarve(n, bytesNeeded);
    }
    stOutBytes += (uint32_t)bytesNeeded;
    stOutCalls++;
    int g = audioGainQ8;
    if (g != 256) {
      int16_t* s = (int16_t*)channels;
      for (int i = 0; i < frame_count * 2; i++) s[i] = (int16_t)((s[i] * g) >> 8);
    }
    return frame_count;
  }
  memset(channels, 0, bytesNeeded);
  stSilenceBytes += (uint32_t)bytesNeeded;
  audioNoteStarve(0, bytesNeeded);
  stOutBytes += (uint32_t)bytesNeeded;
  stOutCalls++;
  return frame_count;
}

// ==========================================
// MUSIC PLAYER APP
// ==========================================
WavInfo parseWavHeader(File& f) {
  WavInfo info;
  uint8_t hdr[12];
  if (f.read(hdr, 12) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return info;
  uint32_t fileSize = f.size();
  while (f.available() >= 8) {
    uint8_t chunkHdr[8];
    if (f.read(chunkHdr, 8) != 8) break;
    uint32_t chunkSize = chunkHdr[4] | (chunkHdr[5] << 8) | (chunkHdr[6] << 16) | ((uint32_t)chunkHdr[7] << 24);
    if (memcmp(chunkHdr, "fmt ", 4) == 0) {
      uint8_t fmt[16] = { 0 };
      uint32_t toRead = min((uint32_t)16, chunkSize);
      f.read(fmt, toRead);
      info.numChannels = fmt[2] | (fmt[3] << 8);
      info.sampleRate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      info.bitsPerSample = fmt[14] | (fmt[15] << 8);
      if (chunkSize > toRead) f.seek(f.position() + (chunkSize - toRead));
    } else if (memcmp(chunkHdr, "data", 4) == 0) {
      info.dataStart = f.position();
      info.dataSize = chunkSize;
      uint32_t bytesLeft = (fileSize > info.dataStart) ? (fileSize - info.dataStart) : 0;
      if (info.dataSize == 0 || info.dataSize > bytesLeft) info.dataSize = bytesLeft;
      break;
    } else {
      if (chunkSize > 100000000) break;
      f.seek(f.position() + chunkSize);
    }
    if (chunkSize % 2 == 1) f.seek(f.position() + 1);
  }
  info.valid = (info.dataStart > 0 && info.numChannels > 0 && info.sampleRate > 0 && info.bitsPerSample > 0);
  return info;
}

void loadPlaylist() {
  numTracks = 0;
  if (!sdReady || !audioMutex || xSemaphoreTake(audioMutex, portMAX_DELAY) != pdTRUE) return;

  File root = SD.open("/");
  if (root) {
    while (numTracks < MAX_TRACKS) {
      File entry = root.openNextFile();
      if (!entry) break;
      if (!entry.isDirectory()) {
        String name = entry.name();
        String nameLower = name;
        nameLower.toLowerCase();
        if (nameLower.endsWith(".wav")) {
          if (name.startsWith("/")) name = name.substring(1);
          playlist[numTracks++] = name;
        }
      }
      entry.close();
    }
    root.close();
  }

  // A card can be changed while the player is open.  Keep the selected index
  // valid after a refresh so the larger playlist cannot cause an out-of-range
  // title lookup on the player screen.
  if (numTracks == 0) currentTrack = 0;
  else currentTrack = constrain(currentTrack, 0, numTracks - 1);
  xSemaphoreGive(audioMutex);
}

// ==========================================
// RING BUFFER ALLOCATION
// ==========================================
// Picks the biggest ring that still leaves the Bluetooth stack its floor: the
// stack allocates its own queues and buffers when streaming starts, and a ring
// that ate into that margin is how "playing X" turns into an assert.  Steps a
// size down rather than fail, and returns the size that was taken (0 = none).
// Handles a crash that was traced, on hardware, to the moment the earbuds
// connect: the stack allocates its AVRCP/L2CAP/SDP blocks then, one of the fixed
// queues could not get its semaphore, and the IDF cleanup path for that failure
// calls vSemaphoreDelete(NULL) - an assert and a reboot.  The allocation that
// fails is only ~100 bytes, so the heap has to be nearly gone at that instant.
//
// The ring is therefore NOT held while a connection is being set up.  It is
// taken back once the setup has settled (or the stream has started), which hands
// the stack the whole heap for the one moment it needs it.  Music before that
// point is silence from get_audio_data(), which is harmless: nothing is pulling
// audio until the stream is up.
// State of the ring service.  File scope rather than function-local so the host
// tests can put it back to its power-on values between suites.
static bool ringSawConnected = false;
static unsigned long ringConnectedAt = 0;
static unsigned long ringNextTry = 0;
static int ringAttempt = 0;

void audioRingServiceReset() {
  ringSawConnected = false;
  ringConnectedAt = 0;
  ringNextTry = 0;
  ringAttempt = 0;
}

void audioRingService() {
  bool& sawConnected = ringSawConnected;
  unsigned long& connectedAt = ringConnectedAt;
  unsigned long& nextTry = ringNextTry;
  int& attempt = ringAttempt;
  if (audioDirect) return;   // nothing to allocate: the callback reads the card
  if (audioRingBuffer || !audioSystemReady || !btInitialized) return;
  if (!a2dp_source.is_connected()) return;
  if (!sawConnected) {
    sawConnected = true;
    connectedAt = millis();
    Serial.printf("[I][buds] connected, free heap %u min %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  }
  bool streaming = a2dp_source.get_audio_state() == ESP_A2D_AUDIO_STATE_STARTED;
  if (!streaming && millis() - connectedAt < 2500) return;   // let the setup finish
  if (millis() < nextTry) return;                            // do not spin on the allocator

  attempt++;
  // Only the headroom the *stream* needs is required here: the stack has already
  // allocated everything it needs to connect, so holding out for the whole
  // BT_MIN_HEAP again would never allocate on this board and the music would
  // never start.  (The pre-start check in HomeApp still uses the big floor.)
  if (allocAudioRing(RING_RESERVE_POST_CONNECT, attempt == 1) > 0) {
    // The ring is the last thing the player was waiting for: start playing.
    if (!isPlaying && audioFile && currentWav.valid) isPlaying = true;
    Serial.printf("[I][music] ring ready, free heap %u min %u\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
    return;
  }
  nextTry = millis() + 2000;
  if (attempt == 1) {
    Serial.printf("[I][music] ring not yet (%u free, %u wanted spare), retrying\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)RING_RESERVE_POST_CONNECT);
  }
}

int allocAudioRing(uint32_t reserveBytes, bool verbose) {
  const int sizes[3] = { RING_BUF_SIZE, RING_BUF_SIZE_ALT, RING_BUF_SIZE_MIN };
  uint32_t heap = (uint32_t)ESP.getFreeHeap();
  for (int i = 0; i < 3; i++) {
    if (heap < (uint32_t)sizes[i] + reserveBytes) {
      if (verbose) {
        Serial.printf("[I][music] ring %d leaves %u, want %u spare\n",
                      sizes[i], (unsigned)(heap - sizes[i]), (unsigned)reserveBytes);
      }
      continue;
    }
    uint8_t *buf = (uint8_t *)malloc(sizes[i]);
    if (!buf) continue;
    memset(buf, 0, sizes[i]);
    audioRingBuffer = buf;
    audioRingBytes = sizes[i];
    Serial.printf("[I][music] ring buffer %d bytes, %u free after\n",
                  sizes[i], (unsigned)ESP.getFreeHeap());
    return sizes[i];
  }
  return 0;
}

void playTrack(int index) {
  if (numTracks == 0) return;
  index = ((index % numTracks) + numTracks) % numTracks;
  for (int attempts = 0; attempts < numTracks; attempts++) {
    int idx = (index + attempts) % numTracks;
    isPlaying = false;
    audioFileBusy = true;      // the callback hands out silence meanwhile
    if (xSemaphoreTake(audioMutex, portMAX_DELAY) == pdTRUE) {
      if (audioFile) audioFile.close();
      File f = SD.open(("/" + playlist[idx]).c_str());
      if (!f) {
        audioFileBusy = false;
        xSemaphoreGive(audioMutex);
        continue;
      }
      WavInfo info = parseWavHeader(f);
      if (!info.valid || info.numChannels != 2 || info.bitsPerSample != 16) {
        f.close();
        audioFileBusy = false;
        xSemaphoreGive(audioMutex);
        continue;
      }
      audioFile = f;
      audioFile.seek(info.dataStart);
      currentWav = info;
      currentTrack = idx;
      ringHead = 0;
      ringTail = 0;
      audioStreamPos = 0;
      fileReadDone = false;
      audioStatsReset();
      isPlaying = true;
      xSemaphoreGive(audioMutex);
      audioFileBusy = false;
      Serial.printf("[I][audio] playing %s (%lu bytes, %lu Hz)\n", playlist[idx].c_str(),
                    (unsigned long)info.dataSize, (unsigned long)info.sampleRate);
    }
    if (attempts > 0 && currentState == STATE_MUSIC && displayActive()) {
      showToast("Skipped unsupported file");
      drawMusicScreen(true);
    }
    return;
  }
  isPlaying = false;
  currentWav = WavInfo();
  if (currentState == STATE_MUSIC && displayActive()) showToast("No playable 16-bit stereo WAV");
}

void applyVolume() {
  currentVolume = constrain(currentVolume, 0, 100);
  audioGainQ8 = (currentVolume * 256) / 100;
}

// Player layout: 34 px title bar, an artwork tile with the track name beside
// it, the seek bar on the 8 px grid, then one transport row.  Left/right of the
// play button are volume and track buttons, mirrored for symmetry.
static const int MUS_ART_X = 20;
static const int MUS_ART_Y = 58;
static const int MUS_ART_W = 76;
static const int MUS_ART_H = 76;
static const int MUS_INFO_X = 108;
static const int MUS_SEEK_X = 20;
static const int MUS_SEEK_W = 280;
static const int MUS_SEEK_Y = 150;
static const int MUS_SIDE_W = 48;
static const int MUS_SIDE_H = 48;
static const int MUS_SIDE_Y = 172;
static const int MUS_PLAY_X = 128;
static const int MUS_PLAY_Y = 164;
static const int MUS_PLAY_W = 64;
static const int MUS_PLAY_H = 64;

static void drawMusicTransport(bool playing) {
  drawModernButton(MUS_SEEK_X, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawMinusIcon(MUS_SEEK_X + (MUS_SIDE_W / 2), MUS_SIDE_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(74, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawSkipRevIcon(98, MUS_SIDE_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(MUS_PLAY_X, MUS_PLAY_Y, MUS_PLAY_W, MUS_PLAY_H, RADIUS_LG, playing ? ACCENT_COLOR : PLOT_COLOR, false);
  if (playing) drawPauseIcon(MUS_PLAY_X + (MUS_PLAY_W / 2), MUS_PLAY_Y + (MUS_PLAY_H / 2), TEXT_COLOR);
  else drawPlayIcon(MUS_PLAY_X + (MUS_PLAY_W / 2), MUS_PLAY_Y + (MUS_PLAY_H / 2), TEXT_COLOR);
  drawModernButton(198, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawSkipFwdIcon(222, MUS_SIDE_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(252, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawPlusIcon(276, MUS_SIDE_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
}

static void drawMusicEmpty(bool sdOk) {
  drawScreenHeader("MUSIC", true);
  drawIconTile(136, 70, 48, 48, RADIUS_LG, sdOk ? ACCENT_COLOR : DEL_COLOR);
  if (sdOk) drawNoteIcon(160, 94, 22, MUTED_COLOR);
  else drawMinusIcon(160, 94, MUTED_COLOR);
  printCentered(sdOk ? "No playable WAV files" : "SD card not detected", 160, 146, &FreeSans9pt7b, sdOk ? TEXT_COLOR : DEL_COLOR);
  printCentered(sdOk ? "Add 16-bit stereo 44.1 kHz WAVs to the card" : "Insert a card and restart the device", 160, 168, &FreeSans9pt7b, MUTED_COLOR);
}

void drawMusicScreen(bool fullWipe) {
  static int lastFillW = -1;
  static uint32_t lastSec = 0xFFFFFFFF;
  // The empty state is a static screen: it is not redrawn on the periodic
  // refresh, so it costs nothing until something actually changes.
  static bool emptyDrawn = false;
  if (numTracks == 0) {
    if (fullWipe || !emptyDrawn) {
      drawMusicEmpty(sdReady);
      emptyDrawn = true;
    }
    return;
  }
  emptyDrawn = false;
  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    drawScreenHeader("NOW PLAYING", true);
    drawModernButton(280, 5, 34, 24, RADIUS_SM, SURFACE_HI, false);
    drawListIcon(297, 17, TEXT_COLOR);

    // Artwork tile: a note on a tinted square, the way a player should look.
    drawIconTile(MUS_ART_X, MUS_ART_Y, MUS_ART_W, MUS_ART_H, RADIUS_LG, ACCENT_COLOR);
    drawNoteIcon(MUS_ART_X + (MUS_ART_W / 2), MUS_ART_Y + (MUS_ART_H / 2), 34, ACCENT_COLOR);

    String tName = playlist[currentTrack];
    if (tName.length() > 16) tName = tName.substring(0, 14) + "..";
    tft.setFont(&FreeSansBold18pt7b);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(MUS_INFO_X, MUS_ART_Y + 34);
    tft.print(tName);
    tft.setFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(MUS_INFO_X, MUS_ART_Y + 54);
    tft.print(numTracks > 1 ? ("TRACK " + String(currentTrack + 1) + " OF " + String(numTracks)) : "THE ONLY TRACK");

    drawMusicTransport(isPlaying);
    drawProgressBar(MUS_SEEK_X, MUS_SEEK_Y, MUS_SEEK_W, 6, 0.0f, PLOT_COLOR);
    lastBtState = !btConnected;
    lastFillW = -1;
    lastSec = 0xFFFFFFFF;
  }
  // Called on every repaint: it returns early unless the connection state or the
  // volume really changed, so the 1 Hz refresh costs nothing extra.
  drawMusicStatus();
  if (audioFile && currentWav.valid) {
    uint32_t byteRate = currentWav.sampleRate * currentWav.numChannels * (currentWav.bitsPerSample / 8);
    uint32_t cur_sec = byteRate ? audioStreamPos / byteRate : 0;
    uint32_t total_sec = byteRate ? currentWav.dataSize / byteRate : 0;
    float pct = currentWav.dataSize ? (float)audioStreamPos / (float)currentWav.dataSize : 0;
    int fillW = constrain((int)(pct * (MUS_SEEK_W - 4)), 0, MUS_SEEK_W - 4);
    if (cur_sec != lastSec) {
      // Remaining on the left, elapsed on the right, on their own line under
      // the artwork: the two numbers stay put instead of shifting as the time
      // runs, and the band between them is the only thing cleared.
      tft.fillRect(MUS_SEEK_X, MUS_SEEK_Y - 18, MUS_SEEK_W, 12, BG_COLOR);
      printCentered("-" + formatTime(total_sec > cur_sec ? total_sec - cur_sec : 0), MUS_SEEK_X + 46, MUS_SEEK_Y - 8, &FreeSans9pt7b, MUTED_COLOR);
      printRight(formatTime(cur_sec), MUS_SEEK_X + MUS_SEEK_W, MUS_SEEK_Y - 8, &FreeSans9pt7b, TEXT_COLOR);
      lastSec = cur_sec;
    }
    if (fillW != lastFillW) {
      // Repaint the bar and the scrub handle from scratch each time.
      drawProgressBar(MUS_SEEK_X, MUS_SEEK_Y, MUS_SEEK_W, 6, pct, PLOT_COLOR);
      if (lastFillW >= 0) tft.fillCircle(MUS_SEEK_X + 2 + lastFillW, MUS_SEEK_Y + 3, 8, BG_COLOR);
      tft.fillCircle(MUS_SEEK_X + 2 + fillW, MUS_SEEK_Y + 3, 6, TEXT_COLOR);
      lastFillW = fillW;
    }
  }
}

// Connection state and volume, repainted only when they change.
void drawMusicStatus() {
  static int lastVolume = -1;
  // The pill mirrors the live volume, so a change made with the earbuds (or on
  // the settings side) shows up on the next refresh.
  if (lastVolume == currentVolume && lastBtState == btConnected) return;
  tft.fillRect(0, 38, 320, 26, BG_COLOR);
  String state = btConnected ? "Earbuds connected" : "Searching for earbuds";
  int w = 24 + state.length() * 6;
  drawStatusPill(12, 40, w, state.c_str(), btConnected ? PLOT_COLOR : MUTED_COLOR, btConnected ? TEXT_COLOR : MUTED_COLOR);
  String vol = "VOL " + String(currentVolume) + "%";
  int vw = 56;
  drawStatusPill(320 - 12 - vw, 40, vw, vol.c_str(), 0, currentVolume == 0 ? DEL_COLOR : MUTED_COLOR);
  lastVolume = currentVolume;
  lastBtState = btConnected;
}

void handleMusicTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (inRect(sx, sy, MUS_SEEK_X - 10, MUS_SEEK_Y - 14, MUS_SEEK_W + 20, 32) && numTracks > 0 && audioFile && currentWav.valid) {
    float pct = constrain((float)(sx - MUS_SEEK_X) / (float)MUS_SEEK_W, 0.0, 1.0);
    uint32_t target_byte = pct * currentWav.dataSize;
    uint16_t frameBytes = currentWav.numChannels * (currentWav.bitsPerSample / 8);
    if (frameBytes > 0) target_byte = (target_byte / frameBytes) * frameBytes;
    if (xSemaphoreTake(audioMutex, portMAX_DELAY) == pdTRUE) {
      bool wasPlaying = isPlaying;
      isPlaying = false;
      audioFileBusy = true;
      ringHead = 0;
      ringTail = 0;
      audioFile.seek(currentWav.dataStart + target_byte);
      audioStreamPos = target_byte;
      fileReadDone = false;
      isPlaying = wasPlaying;
      audioFileBusy = false;
      xSemaphoreGive(audioMutex);
    }
    drawMusicScreen(false);
    delay(40);
    return;
  }
  if (millis() - lastMusicBtnPress < 400) return;
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(6, 5, 34, 24, RADIUS_SM);
    currentState = STATE_HOME;
    drawHomeScreen();
    lastMusicBtnPress = millis();
    return;
  }
  if (numTracks == 0) return;
  if (inRect(sx, sy, 280, 0, 40, 30)) {
    flashButton(280, 0, 40, 30, 0);
    listPage = currentTrack / TRACKS_PER_PAGE;
    currentState = STATE_MUSIC_LIST;
    drawMusicList();
    lastMusicBtnPress = millis();
    return;
  }
  if (inRect(sx, sy, MUS_SEEK_X, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(MUS_SEEK_X, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentVolume -= 10;
    applyVolume();
    prefs.putInt("volume", currentVolume);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 74, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(74, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentTrack--;
    if (currentTrack < 0) currentTrack = numTracks - 1;
    playTrack(currentTrack);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, MUS_PLAY_X, MUS_PLAY_Y, MUS_PLAY_W, MUS_PLAY_H)) {
    flashButton(MUS_PLAY_X, MUS_PLAY_Y, MUS_PLAY_W, MUS_PLAY_H, RADIUS_LG);
    if (!audioFile || !currentWav.valid) playTrack(currentTrack);
    else isPlaying = !isPlaying;
    if (isPlaying && !btConnected) showToast("Earbuds not connected yet");
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 198, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(198, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentTrack++;
    if (currentTrack >= numTracks) currentTrack = 0;
    playTrack(currentTrack);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 252, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(252, MUS_SIDE_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentVolume += 10;
    applyVolume();
    prefs.putInt("volume", currentVolume);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  }
}

void drawMusicList() {
  tft.fillScreen(BG_COLOR);
  String listTitle = "TRACKS (" + String(numTracks) + ")";
  drawScreenHeader(listTitle.c_str(), true);
  int totalPages = max(1, (numTracks + TRACKS_PER_PAGE - 1) / TRACKS_PER_PAGE);
  if (listPage >= totalPages) listPage = totalPages - 1;
  if (listPage < 0) listPage = 0;
  for (int i = 0; i < TRACKS_PER_PAGE; i++) {
    int idx = listPage * TRACKS_PER_PAGE + i;
    if (idx >= numTracks) break;
    int y = 42 + i * TRACK_ROW_HEIGHT;
    bool cur = (idx == currentTrack);
    drawCard(10, y, 300, TRACK_ROW_HEIGHT - 3, cur, RADIUS_SM);
    if (cur) tft.fillRect(11, y + 6, 3, TRACK_ROW_HEIGHT - 15, ACCENT_COLOR);
    // The row number doubles as the playing indicator.
    if (cur) {
      if (isPlaying) drawPauseIcon(26, y + (TRACK_ROW_HEIGHT / 2) - 1, PLOT_COLOR);
      else drawPlayIcon(26, y + (TRACK_ROW_HEIGHT / 2) - 1, PLOT_COLOR);
    } else {
      printCentered(String(idx + 1), 26, y + 16, NULL, MUTED_COLOR);
    }
    String name = playlist[idx];
    if (name.length() > 30) name = name.substring(0, 28) + "..";
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(cur ? TEXT_COLOR : (isPlaying ? TEXT_COLOR : TEXT_COLOR));
    tft.setCursor(44, y + 19);
    tft.print(name);
    tft.setFont(NULL);
  }
  drawModernButton(10, 206, 88, 28, RADIUS_MD, listPage > 0 ? SURFACE_HI : SURFACE_COLOR, false);
  printCentered("PREV", 54, 225, &FreeSans9pt7b, listPage > 0 ? TEXT_COLOR : MUTED_COLOR);
  drawModernButton(222, 206, 88, 28, RADIUS_MD, listPage < totalPages - 1 ? SURFACE_HI : SURFACE_COLOR, false);
  printCentered("NEXT", 266, 225, &FreeSans9pt7b, listPage < totalPages - 1 ? TEXT_COLOR : MUTED_COLOR);
  printCentered("Page " + String(listPage + 1) + " / " + String(totalPages), 160, 225, &FreeSans9pt7b, MUTED_COLOR);
}

void handleMusicListTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (millis() - lastMusicBtnPress < 350) return;
  lastMusicBtnPress = millis();
  int totalPages = max(1, (numTracks + TRACKS_PER_PAGE - 1) / TRACKS_PER_PAGE);
  if (inRect(sx, sy, 0, 0, 44, 34)) {
    flashButton(6, 5, 34, 24, RADIUS_SM);
    currentState = STATE_MUSIC;
    drawMusicScreen(true);
    return;
  }
  if (inRect(sx, sy, 10, 206, 88, 28) && listPage > 0) {
    flashButton(10, 206, 88, 28, RADIUS_MD);
    listPage--;
    drawMusicList();
    return;
  }
  if (inRect(sx, sy, 222, 206, 88, 28) && listPage < totalPages - 1) {
    flashButton(222, 206, 88, 28, RADIUS_MD);
    listPage++;
    drawMusicList();
    return;
  }
  for (int i = 0; i < TRACKS_PER_PAGE; i++) {
    int idx = listPage * TRACKS_PER_PAGE + i;
    if (idx >= numTracks) break;
    int y = 42 + i * TRACK_ROW_HEIGHT;
    if (inRect(sx, sy, 10, y, 300, TRACK_ROW_HEIGHT - 3)) {
      flashButton(10, y, 300, TRACK_ROW_HEIGHT - 3, RADIUS_SM);
      if (idx == currentTrack && audioFile && currentWav.valid) {
        isPlaying = !isPlaying;
        drawMusicList();
      } else {
        playTrack(idx);
        currentState = STATE_MUSIC;
        drawMusicScreen(true);
      }
      return;
    }
  }
}

// ==========================================
// LINK RECOVERY
// ==========================================
// The report from hardware is specific: a fresh stream runs at full rate
// (~172 KiB/s out, 325 kbps of SBC) and later the same link only delivers
// ~116 KiB/s, with the transmit queue full and the stack throttled.  The bitpool
// that sets the air rate is negotiated inside Bluedroid and cannot be lowered
// from here, so the lever we have is to start the stream again - which is
// exactly the state the user reports as good.
//
// Two stages, cheapest first:
//   1. media-level restart: esp_a2d_media_ctrl(SUSPEND) then (START) - the
//      Bluetooth link stays up and only the audio stream is rebuilt, which
//      flushes the sink's jitter buffer and drains the backed-up queue.
//   2. session rebuild: a2dp end/start - effective, but the earbuds see a
//      disconnect and reconnect, so it is rare and capped per boot.
static unsigned long linkLastAction = 0;
static unsigned long linkLastRebuild = 0;
static int linkGentleAttempts = 0;
static int linkRebuilds = 0;

void audioLinkRecoveryReset() {
  linkLastAction = 0;
  linkLastRebuild = 0;
  linkGentleAttempts = 0;
  linkRebuilds = 0;
  audioLinkVerdictClear();
}

int audioLinkRebuildCount() { return linkRebuilds; }

bool audioLinkRecoveryService() {
  if (audioLinkBehindMs() < LINK_BEHIND_MS) {
    // The link is keeping up again: forget the attempts made so far.
    linkGentleAttempts = 0;
    return false;
  }
  unsigned long now = millis();
  // Never act on a stream that is already recovering: a higher rate after the
  // last action means it worked.
  if (linkLastAction != 0 && now - linkLastAction < LINK_RECOVER_GAP_MS) return false;

  if (linkGentleAttempts < 2) {
    linkGentleAttempts++;
    linkLastAction = now;
    Serial.printf("[I][bt] link behind %lu s (out %lu KB/s), restarting the stream\n",
                  (unsigned long)(audioLinkBehindMs() / 1000), (unsigned long)audioOutKBps());
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
    delay(400);
    esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
    audioLinkVerdictClear();     // judge the result on the next window
    return true;
  }

  // Two media restarts did not hold.  Rebuild the session - but rarely, and only
  // a few times per boot: each one is a disconnect the user hears.
  if (linkRebuilds >= LINK_MAX_REBUILDS) return false;
  if (linkLastRebuild != 0 && now - linkLastRebuild < LINK_REBUILD_GAP_MS) return false;
  linkRebuilds++;
  linkGentleAttempts = 0;
  linkLastAction = now;
  linkLastRebuild = now;
  Serial.printf("[I][bt] link behind %lu s (out %lu KB/s), rebuilding the session (%d/%d)\n",
                (unsigned long)(audioLinkBehindMs() / 1000), (unsigned long)audioOutKBps(),
                linkRebuilds, LINK_MAX_REBUILDS);
  a2dp_source.end();
  delay(300);
  btStartSource();                 // end() forgot the earbuds; hand them back
  delay(1200);
  a2dp_source.set_volume(127);
  esp_bt_sleep_disable();
  audioStatsReset();
  audioLinkVerdictClear();
  Serial.printf("[I][bt] session rebuilt, free heap %u\n", (unsigned)ESP.getFreeHeap());
  return true;
}

#ifdef HOSTCHECK
void audioSetFileBusyForTest(bool busy) { audioFileBusy = busy; }
#endif
