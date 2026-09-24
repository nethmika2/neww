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
  if (head >= tail) return (RING_BUF_SIZE - 1) - (head - tail);
  return tail - head - 1;
}

int getRingBufferAvailableRead() {
  int head = ringHead, tail = ringTail;
  if (head >= tail) return head - tail;
  return RING_BUF_SIZE - (tail - head);
}

void audioFeederTask(void* pvParameters) {
  static uint8_t tempBuf[FEEDER_CHUNK];
  while (true) {
    bool didWork = false;
    if (isPlaying && audioRingBuffer) {
      if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (audioFile) {
          if (!fileReadDone) {
            int space = getRingBufferAvailableWrite();
            if (space > 512) {
              int bytesToRead = min(space, (int)sizeof(tempBuf));
              int bytesRead = audioFile.read(tempBuf, bytesToRead);
              if (bytesRead > 0) {
                for (int i = 0; i < bytesRead; i++) {
                  audioRingBuffer[ringHead] = tempBuf[i];
                  ringHead = (ringHead + 1) % RING_BUF_SIZE;
                }
                audioStreamPos += bytesRead;
                didWork = true;
              }
              if (bytesRead <= 0 || audioFile.available() == 0) fileReadDone = true;
            }
          } else {
            if (getRingBufferAvailableRead() < 16) {
              trackFinished = true;
              isPlaying = false;
              fileReadDone = false;
            }
          }
        } else {
          isPlaying = false;
        }
        xSemaphoreGive(audioMutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(didWork ? 1 : 8));
  }
}

int32_t get_audio_data(Frame* channels, int32_t frame_count) {
  int bytesNeeded = frame_count * sizeof(Frame);
  if (!channels || frame_count <= 0) return frame_count;
  if (!audioSystemReady || !audioRingBuffer) {
    memset(channels, 0, bytesNeeded);
    return frame_count;
  }
  uint8_t* dest = (uint8_t*)channels;
  if (isPlaying && getRingBufferAvailableRead() >= bytesNeeded) {
    for (int i = 0; i < bytesNeeded; i++) {
      dest[i] = audioRingBuffer[ringTail];
      ringTail = (ringTail + 1) % RING_BUF_SIZE;
    }
    int g = audioGainQ8;
    if (g != 256) {
      int16_t* s = (int16_t*)channels;
      for (int i = 0; i < frame_count * 2; i++) s[i] = (int16_t)((s[i] * g) >> 8);
    }
    return frame_count;
  }
  memset(channels, 0, bytesNeeded);
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
      xSemaphoreGive(audioMutex);
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

// Player layout, top to bottom: title bar, connection pill, track name, seek
// bar with the elapsed time on the left and the total on the right, then the
// transport row and a volume row.  Every element sits on the 8 px grid.
static const int MUS_SEEK_X = 20;
static const int MUS_SEEK_W = 280;
static const int MUS_SEEK_Y = 132;
static const int MUS_PLAY_X = 128;
static const int MUS_PLAY_Y = 156;
static const int MUS_PLAY_W = 64;
static const int MUS_PLAY_H = 64;
static const int MUS_SIDE_W = 48;
static const int MUS_SIDE_H = 48;
static const int MUS_VOL_BTN_Y = 156;

static void drawMusicTransport(bool playing) {
  drawModernButton(MUS_SEEK_X, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawMinusIcon(MUS_SEEK_X + (MUS_SIDE_W / 2), MUS_VOL_BTN_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(76, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawSkipRevIcon(100, MUS_VOL_BTN_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(MUS_PLAY_X, MUS_PLAY_Y, MUS_PLAY_W, MUS_PLAY_H, RADIUS_LG, playing ? FUNC_COLOR : PLOT_COLOR, false);
  if (playing) drawPauseIcon(MUS_PLAY_X + (MUS_PLAY_W / 2), MUS_PLAY_Y + (MUS_PLAY_H / 2), TEXT_COLOR);
  else drawPlayIcon(MUS_PLAY_X + (MUS_PLAY_W / 2), MUS_PLAY_Y + (MUS_PLAY_H / 2), TEXT_COLOR);
  drawModernButton(196, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawSkipFwdIcon(220, MUS_VOL_BTN_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
  drawModernButton(252, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD, SURFACE_COLOR, false);
  drawPlusIcon(276, MUS_VOL_BTN_Y + (MUS_SIDE_H / 2), TEXT_COLOR);
}

static void drawMusicEmpty(bool sdOk, bool fullWipe) {
  if (!fullWipe) return;
  drawScreenHeader("SD AUDIO PLAYER", true);
  drawIconTile(136, 74, 48, 48, RADIUS_LG, sdOk ? ACCENT_COLOR : DEL_COLOR);
  if (sdOk) drawWaveIcon(160, 98, 16, 10, MUTED_COLOR);
  else drawMinusIcon(160, 98, MUTED_COLOR);
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
      drawMusicEmpty(sdReady, true);
      emptyDrawn = true;
    }
    return;
  }
  emptyDrawn = false;
  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    drawScreenHeader("NOW PLAYING", true);
    drawModernButton(280, 3, 34, 24, RADIUS_SM, SURFACE_HI, false);
    drawListIcon(297, 15, TEXT_COLOR);

    String tName = playlist[currentTrack];
    if (tName.length() > 24) tName = tName.substring(0, 22) + "..";
    printCentered(tName, 160, 88, &FreeSansBold18pt7b, TEXT_COLOR);
    String pos = "TRACK " + String(currentTrack + 1) + " OF " + String(numTracks);
    printCentered(pos, 160, 100, NULL, MUTED_COLOR);

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
      // Elapsed on the right of the seek bar, remaining on the left: the two
      // numbers stay put instead of shifting as the time runs.  Only the band
      // between the two is cleared, so the track title above stays untouched.
      tft.fillRect(MUS_SEEK_X, MUS_SEEK_Y - 18, MUS_SEEK_W, 14, BG_COLOR);
      printCentered("-" + formatTime(total_sec > cur_sec ? total_sec - cur_sec : 0), MUS_SEEK_X + 46, MUS_SEEK_Y - 6, &FreeSans9pt7b, MUTED_COLOR);
      printRight(formatTime(cur_sec), MUS_SEEK_X + MUS_SEEK_W, MUS_SEEK_Y - 6, &FreeSans9pt7b, TEXT_COLOR);
      lastSec = cur_sec;
    }
    if (fillW != lastFillW) {
      tft.fillRoundRect(MUS_SEEK_X + 2, MUS_SEEK_Y + 2, MUS_SEEK_W - 4, 2, 1, PLOT_COLOR);
      tft.fillRect(MUS_SEEK_X + 2 + min(lastFillW, fillW), MUS_SEEK_Y, 2, 6, BG_COLOR);
      tft.fillRect(MUS_SEEK_X + 2 + fillW, MUS_SEEK_Y, MUS_SEEK_W - 4 - fillW, 6, SURFACE_COLOR);
      if (fillW > 0) tft.fillRect(MUS_SEEK_X + 2, MUS_SEEK_Y, fillW, 6, PLOT_COLOR);
      tft.fillCircle(MUS_SEEK_X + 2 + fillW, MUS_SEEK_Y + 3, 7, TEXT_COLOR);
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
  tft.fillRect(0, 36, 320, 26, BG_COLOR);
  String state = btConnected ? "Earbuds connected" : "Searching for earbuds";
  int w = 24 + state.length() * 6;
  drawStatusPill(12, 38, w, state.c_str(), btConnected ? PLOT_COLOR : MUTED_COLOR, btConnected ? TEXT_COLOR : MUTED_COLOR);
  String vol = "VOL " + String(currentVolume) + "%";
  int vw = 56;
  drawStatusPill(320 - 12 - vw, 38, vw, vol.c_str(), 0, currentVolume == 0 ? DEL_COLOR : MUTED_COLOR);
  lastVolume = currentVolume;
  lastBtState = btConnected;
}

void handleMusicTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (inRect(sx, sy, MUS_SEEK_X - 10, MUS_SEEK_Y - 14, MUS_SEEK_W + 20, 34) && numTracks > 0 && audioFile && currentWav.valid) {
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
    flashButton(6, 3, 34, 24, RADIUS_SM);
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
  if (inRect(sx, sy, MUS_SEEK_X, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(MUS_SEEK_X, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentVolume -= 10;
    applyVolume();
    prefs.putInt("volume", currentVolume);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 76, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(76, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
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
  } else if (inRect(sx, sy, 196, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(196, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
    currentTrack++;
    if (currentTrack >= numTracks) currentTrack = 0;
    playTrack(currentTrack);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 252, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H)) {
    flashButton(252, MUS_VOL_BTN_Y, MUS_SIDE_W, MUS_SIDE_H, RADIUS_MD);
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
    int y = 38 + i * TRACK_ROW_HEIGHT;
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
  drawModernButton(10, 208, 88, 28, RADIUS_MD, listPage > 0 ? SURFACE_HI : SURFACE_COLOR, false);
  printCentered("PREV", 54, 227, &FreeSans9pt7b, listPage > 0 ? TEXT_COLOR : MUTED_COLOR);
  drawModernButton(222, 208, 88, 28, RADIUS_MD, listPage < totalPages - 1 ? SURFACE_HI : SURFACE_COLOR, false);
  printCentered("NEXT", 266, 227, &FreeSans9pt7b, listPage < totalPages - 1 ? TEXT_COLOR : MUTED_COLOR);
  printCentered("Page " + String(listPage + 1) + " / " + String(totalPages), 160, 227, &FreeSans9pt7b, MUTED_COLOR);
}

void handleMusicListTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (millis() - lastMusicBtnPress < 350) return;
  lastMusicBtnPress = millis();
  int totalPages = max(1, (numTracks + TRACKS_PER_PAGE - 1) / TRACKS_PER_PAGE);
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(6, 3, 34, 24, RADIUS_SM);
    currentState = STATE_MUSIC;
    drawMusicScreen(true);
    return;
  }
  if (inRect(sx, sy, 10, 208, 88, 28) && listPage > 0) {
    flashButton(10, 208, 88, 28, RADIUS_MD);
    listPage--;
    drawMusicList();
    return;
  }
  if (inRect(sx, sy, 222, 208, 88, 28) && listPage < totalPages - 1) {
    flashButton(222, 208, 88, 28, RADIUS_MD);
    listPage++;
    drawMusicList();
    return;
  }
  for (int i = 0; i < TRACKS_PER_PAGE; i++) {
    int idx = listPage * TRACKS_PER_PAGE + i;
    if (idx >= numTracks) break;
    int y = 38 + i * TRACK_ROW_HEIGHT;
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
