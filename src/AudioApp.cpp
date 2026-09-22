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
  if (!sdReady || xSemaphoreTake(audioMutex, portMAX_DELAY) != pdTRUE) return;
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
          playlist[numTracks] = name;
          numTracks++;
        }
      }
      entry.close();
    }
    root.close();
  }
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

void drawMusicScreen(bool fullWipe) {
  static int lastFillW = -1;
  static uint32_t lastSec = 0xFFFFFFFF;
  if (numTracks == 0) {
    if (fullWipe) {
      tft.fillScreen(BG_COLOR);
      tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
      drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
      drawBackChevron(20, 15, TEXT_COLOR);
      printCentered("SD AUDIO PLAYER", 160, 20, &FreeSansBold9pt7b, TEXT_COLOR);
      if (!sdReady) {
        printCentered("SD card not detected!", 160, 120, &FreeSans9pt7b, DEL_COLOR);
        printCentered("Insert card and restart.", 160, 150, &FreeSans9pt7b, MUTED_COLOR);
      } else {
        printCentered("No .WAV files found!", 160, 120, &FreeSans9pt7b, DEL_COLOR);
        printCentered("Add 16-bit stereo 44.1kHz WAVs.", 160, 150, &FreeSans9pt7b, MUTED_COLOR);
      }
    }
    return;
  }
  if (fullWipe) {
    tft.fillScreen(BG_COLOR);
    tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
    drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
    drawBackChevron(20, 15, TEXT_COLOR);
    tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);
    drawModernButton(280, 0, 40, 30, 0, SURFACE_HI, false);
    drawListIcon(300, 15, TEXT_COLOR);
    printCentered("PLAYING " + String(currentTrack + 1) + " OF " + String(numTracks), 160, 20, &FreeSansBold9pt7b, TEXT_COLOR);
    String tName = playlist[currentTrack];
    if (tName.length() > 22) tName = tName.substring(0, 19) + "...";
    printCentered(tName, 160, 90, &FreeSansBold18pt7b, TEXT_COLOR);
    drawModernButton(10, 180, 50, 45, RADIUS_MD, SURFACE_HI, true);
    printCentered("V-", 35, 208, &FreeSansBold9pt7b, MUTED_COLOR);
    drawModernButton(70, 180, 50, 45, RADIUS_MD, SURFACE_COLOR, true);
    drawSkipRevIcon(95, 202, TEXT_COLOR);
    uint16_t pColor = isPlaying ? FUNC_COLOR : PLOT_COLOR;
    drawModernButton(130, 175, 60, 55, RADIUS_LG, pColor, true);
    if (isPlaying) drawPauseIcon(160, 202, TEXT_COLOR);
    else drawPlayIcon(160, 202, TEXT_COLOR);
    drawModernButton(200, 180, 50, 45, RADIUS_MD, SURFACE_COLOR, true);
    drawSkipFwdIcon(225, 202, TEXT_COLOR);
    drawModernButton(260, 180, 50, 45, RADIUS_MD, SURFACE_HI, true);
    printCentered("V+", 285, 208, &FreeSansBold9pt7b, MUTED_COLOR);
    printCentered("VOL " + String(currentVolume) + "%", 160, 165, &FreeSans9pt7b, currentVolume == 0 ? DEL_COLOR : MUTED_COLOR);
    lastBtState = !btConnected;
    lastFillW = -1;
    lastSec = 0xFFFFFFFF;
  }
  if (lastBtState != btConnected) {
    tft.fillRect(10, 45, 300, 20, BG_COLOR);
    if (btConnected) printCentered("Earbuds Connected", 160, 60, &FreeSans9pt7b, PLOT_COLOR);
    else printCentered("Searching for Earbuds...", 160, 60, &FreeSans9pt7b, MUTED_COLOR);
    lastBtState = btConnected;
  }
  if (audioFile && currentWav.valid) {
    uint32_t byteRate = currentWav.sampleRate * currentWav.numChannels * (currentWav.bitsPerSample / 8);
    uint32_t cur_sec = byteRate ? audioStreamPos / byteRate : 0;
    uint32_t total_sec = byteRate ? currentWav.dataSize / byteRate : 0;
    float pct = currentWav.dataSize ? (float)audioStreamPos / (float)currentWav.dataSize : 0;
    int fillW = constrain((int)(pct * 260), 0, 260);
    if (cur_sec != lastSec) {
      tft.fillRect(60, 100, 200, 19, BG_COLOR);
      printCentered(formatTime(cur_sec) + " / " + formatTime(total_sec), 160, 115, &FreeSans9pt7b, MUTED_COLOR);
      lastSec = cur_sec;
    }
    if (fillW != lastFillW) {
      if (lastFillW >= 0) tft.fillCircle(30 + lastFillW, 134, 8, BG_COLOR);
      tft.fillRoundRect(30, 130, 260, 8, 4, SURFACE_COLOR);
      if (fillW > 0) tft.fillRoundRect(30, 130, fillW, 8, 4, PLOT_COLOR);
      tft.fillCircle(30 + fillW, 134, 7, TEXT_COLOR);
      lastFillW = fillW;
    }
  }
}

void handleMusicTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (inRect(sx, sy, 10, 110, 300, 45) && numTracks > 0 && audioFile && currentWav.valid) {
    float pct = constrain((float)(sx - 30) / 260.0, 0.0, 1.0);
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
    flashButton(0, 0, 40, 30, 0);
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
  if (inRect(sx, sy, 10, 180, 50, 45)) {
    flashButton(10, 180, 50, 45, RADIUS_MD);
    currentVolume -= 10;
    applyVolume();
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 70, 180, 50, 45)) {
    flashButton(70, 180, 50, 45, RADIUS_MD);
    currentTrack--;
    if (currentTrack < 0) currentTrack = numTracks - 1;
    playTrack(currentTrack);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 130, 175, 60, 55)) {
    flashButton(130, 175, 60, 55, RADIUS_LG);
    if (!audioFile || !currentWav.valid) playTrack(currentTrack);
    else isPlaying = !isPlaying;
    if (isPlaying && !btConnected) showToast("Earbuds not connected yet");
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 200, 180, 50, 45)) {
    flashButton(200, 180, 50, 45, RADIUS_MD);
    currentTrack++;
    if (currentTrack >= numTracks) currentTrack = 0;
    playTrack(currentTrack);
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  } else if (inRect(sx, sy, 260, 180, 50, 45)) {
    flashButton(260, 180, 50, 45, RADIUS_MD);
    currentVolume += 10;
    applyVolume();
    drawMusicScreen(true);
    lastMusicBtnPress = millis();
  }
}

void drawMusicList() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
  drawModernButton(0, 0, 40, 30, 0, DEL_COLOR, false);
  drawBackChevron(20, 15, TEXT_COLOR);
  tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);
  printCentered("TRACKS (" + String(numTracks) + ")", 160, 20, &FreeSansBold9pt7b, TEXT_COLOR);
  int totalPages = max(1, (numTracks + TRACKS_PER_PAGE - 1) / TRACKS_PER_PAGE);
  if (listPage >= totalPages) listPage = totalPages - 1;
  if (listPage < 0) listPage = 0;
  for (int i = 0; i < TRACKS_PER_PAGE; i++) {
    int idx = listPage * TRACKS_PER_PAGE + i;
    if (idx >= numTracks) break;
    int y = 36 + i * 32;
    bool cur = (idx == currentTrack);
    tft.fillRoundRect(8, y, 304, 29, RADIUS_SM, cur ? SURFACE_HI : SURFACE_COLOR);
    tft.drawRoundRect(8, y, 304, 29, RADIUS_SM, cur ? PLOT_COLOR : BTN_OUTLINE);
    if (cur) {
      if (isPlaying) drawPauseIcon(24, y + 14, PLOT_COLOR);
      else drawPlayIcon(24, y + 14, PLOT_COLOR);
    } else {
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(MUTED_COLOR);
      tft.setCursor(16, y + 20);
      tft.print(idx + 1);
      tft.setFont(NULL);
    }
    String name = playlist[idx];
    if (name.length() > 26) name = name.substring(0, 24) + "..";
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(42, y + 20);
    tft.print(name);
    tft.setFont(NULL);
  }
  drawModernButton(10, 200, 80, 35, RADIUS_MD, listPage > 0 ? SURFACE_HI : SURFACE_COLOR, true);
  printCentered("PREV", 50, 223, &FreeSans9pt7b, listPage > 0 ? TEXT_COLOR : MUTED_COLOR);
  printCentered("Page " + String(listPage + 1) + " / " + String(totalPages), 160, 223, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(230, 200, 80, 35, RADIUS_MD, listPage < totalPages - 1 ? SURFACE_HI : SURFACE_COLOR, true);
  printCentered("NEXT", 270, 223, &FreeSans9pt7b, listPage < totalPages - 1 ? TEXT_COLOR : MUTED_COLOR);
}

void handleMusicListTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  if (millis() - lastMusicBtnPress < 350) return;
  lastMusicBtnPress = millis();
  int totalPages = max(1, (numTracks + TRACKS_PER_PAGE - 1) / TRACKS_PER_PAGE);
  if (inRect(sx, sy, 0, 0, 40, 30)) {
    flashButton(0, 0, 40, 30, 0);
    currentState = STATE_MUSIC;
    drawMusicScreen(true);
    return;
  }
  if (inRect(sx, sy, 10, 200, 80, 35) && listPage > 0) {
    flashButton(10, 200, 80, 35, RADIUS_MD);
    listPage--;
    drawMusicList();
    return;
  }
  if (inRect(sx, sy, 230, 200, 80, 35) && listPage < totalPages - 1) {
    flashButton(230, 200, 80, 35, RADIUS_MD);
    listPage++;
    drawMusicList();
    return;
  }
  for (int i = 0; i < TRACKS_PER_PAGE; i++) {
    int idx = listPage * TRACKS_PER_PAGE + i;
    if (idx >= numTracks) break;
    int y = 36 + i * 32;
    if (inRect(sx, sy, 8, y, 304, 29)) {
      flashButton(8, y, 304, 29, RADIUS_SM);
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
