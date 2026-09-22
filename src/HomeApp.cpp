#include "HomeApp.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "AudioApp.h"
#include "GraphApp.h"
#include "PomodoroApp.h"
#include "SettingsApp.h"

void drawHomeClock() {
  int h, m;
  getClock(h, m);
  homeLastMinute = m;
  tft.fillRect(0, 0, 90, 26, SURFACE_COLOR);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(timeSynced ? TEXT_COLOR : MUTED_COLOR);
  tft.setCursor(10, 18);
  tft.print(getTimeString());
  tft.setFont(NULL);
}

void drawHomeScreen() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 26, SURFACE_COLOR);
  tft.drawFastHLine(0, 26, 320, BTN_OUTLINE);
  printCentered("SMARTPAD OS", 160, 18, &FreeSansBold9pt7b, TEXT_COLOR);
  drawGearIcon(300, 13, 8, TEXT_COLOR);
  drawHomeClock();
  drawModernButton(30, 50, 80, 80, RADIUS_LG, F1_COLOR, true);
  printCentered("f(x)", 70, 100, &FreeSansBold18pt7b, TEXT_COLOR);
  printCentered("Grapher", 70, 148, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(130, 50, 80, 80, RADIUS_LG, F2_COLOR, true);
  drawWaveIcon(170, 90, 22, 13, TEXT_COLOR);
  printCentered("Music", 170, 148, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(230, 50, 80, 80, RADIUS_LG, DEL_COLOR, true);
  drawClockIcon(270, 90, 18, TEXT_COLOR);
  printCentered("Timer", 270, 148, &FreeSans9pt7b, TEXT_COLOR);
}

void handleHomeTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  unsigned long t = millis();
  while (ts.touched() && millis() - t < 500) delay(10);
  if (inRect(sx, sy, 30, 50, 80, 80)) {
    flashButton(30, 50, 80, 80, RADIUS_LG);
    currentState = STATE_GRAPH;
    tft.fillScreen(BG_COLOR);
    drawGraphScreen(true);
  } else if (inRect(sx, sy, 130, 50, 80, 80)) {
    flashButton(130, 50, 80, 80, RADIUS_LG);
    if (!btInitialized) {
      tft.fillScreen(BG_COLOR);
      printCentered("Initializing Audio...", 160, 100, &FreeSansBold9pt7b, TEXT_COLOR);
      if (!audioRingBuffer) {
        audioRingBuffer = (uint8_t*)malloc(RING_BUF_SIZE);
        if (audioRingBuffer) memset(audioRingBuffer, 0, RING_BUF_SIZE);
        else {
          printCentered("Out of memory!", 160, 130, &FreeSans9pt7b, DEL_COLOR);
          delay(2000);
          drawHomeScreen();
          return;
        }
      }
      if (ESP.getFreeHeap() < BT_MIN_HEAP) {
        printCentered("Not enough memory for Bluetooth", 160, 130, &FreeSans9pt7b, DEL_COLOR);
        delay(2500);
        drawHomeScreen();
        return;
      }
      if (!audioTaskHandle) {
        xTaskCreatePinnedToCore(audioFeederTask, "AudioFeeder", 4096, NULL, 2, &audioTaskHandle, 1);
        delay(150);
      }
      printCentered("Starting Bluetooth...", 160, 130, &FreeSans9pt7b, MUTED_COLOR);
      audioSystemReady = true;
      delay(50);
      a2dp_source.start(EARBUD_NAME, get_audio_data);
      delay(1200);
      a2dp_source.set_volume(127);
      applyVolume();
      btInitialized = true;
    }
    loadPlaylist();
    if (numTracks > 0 && !audioFile) {
      playTrack(currentTrack);
      isPlaying = false;
    }
    currentState = STATE_MUSIC;
    drawMusicScreen(true);
  } else if (inRect(sx, sy, 230, 50, 80, 80)) {
    flashButton(230, 50, 80, 80, RADIUS_LG);
    currentState = STATE_POMODORO;
    tft.fillScreen(BG_COLOR);
    drawPomodoroScreen(true);
  } else if (inRect(sx, sy, 280, 0, 40, 30)) {
    flashButton(280, 0, 40, 30, 0);
    currentState = STATE_SETTINGS;
    drawSettingsScreen();
  }
}
