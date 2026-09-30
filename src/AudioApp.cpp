#include "AudioApp.h"
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
// TELEMETRY
// ==========================================
// Cheap counters, so the serial log can say whether a stutter came from the SD
// side (fed KB/s well under 176), from the Bluetooth side (starve count rising
// while the feed rate is fine) or from nowhere (both healthy - then the stutter
// is on the earbud side).
static volatile uint32_t stBytesFed = 0;
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

void audioStatsReset() {
  stBytesFed = 0;
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
  uint32_t kBps = (uint32_t)(((uint64_t)(fed - stLastLogBytes) * 1000ULL) / (dt * 1024ULL));
  int fill = 0;
  if (audioRingBuffer && audioRingBytes > 0) fill = (getRingBufferAvailableRead() * 100) / audioRingBytes;
  Serial.printf("[I][audio] %s: fed %lu KB (%lu KB/s), ring %d%%, starve %lu (worst %d of %d B, %lu ms silence), feeder %lu/s, playing %d\n",
                why, (unsigned long)(fed / 1024), (unsigned long)kBps, fill,
                (unsigned long)stStarve, stLastStarveFree, stLastStarveNeed,
                (unsigned long)(stSilenceBytes / 176), (unsigned long)stPasses, isPlaying ? 1 : 0);
  stLastLog = now;
  stLastLogBytes = fed;
}

void audioLogStatsIfDue() {
  if (!btInitialized || !isPlaying) return;
  if (millis() - stLastLog < AUDIO_LOG_PERIOD_MS) return;
  audioLogStats("5s");
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
int allocAudioRing() {
  const int sizes[3] = { RING_BUF_SIZE, RING_BUF_SIZE_ALT, RING_BUF_SIZE_MIN };
  uint32_t heap = (uint32_t)ESP.getFreeHeap();
  for (int i = 0; i < 3; i++) {
    if (heap < (uint32_t)sizes[i] + (uint32_t)BT_MIN_HEAP) {
      Serial.printf("[I][music] ring %d leaves %u of %u needed for Bluetooth\n",
                    sizes[i], (unsigned)(heap - sizes[i]), (unsigned)BT_MIN_HEAP);
      continue;
    }
    uint8_t *buf = (uint8_t *)malloc(sizes[i]);
    if (!buf) continue;
    memset(buf, 0, sizes[i]);
    audioRingBuffer = buf;
    audioRingBytes = sizes[i];
    Serial.printf("[I][music] ring buffer %d bytes (%u free for Bluetooth)\n",
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
    if (xSemaphoreTake(audioMutex, portMAX_DELAY) == pdTRUE) {
      if (audioFile) audioFile.close();
      File f = SD.open(("/" + playlist[idx]).c_str());
      if (!f) {
        xSemaphoreGive(audioMutex);
        continue;
      }
      WavInfo info = parseWavHeader(f);
      if (!info.valid || info.numChannels != 2 || info.bitsPerSample != 16) {
        f.close();
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
      isPlaying = true;
      audioStatsReset();
      xSemaphoreGive(audioMutex);
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
      ringHead = 0;
      ringTail = 0;
      audioFile.seek(currentWav.dataStart + target_byte);
      audioStreamPos = target_byte;
      fileReadDone = false;
      isPlaying = wasPlaying;
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
