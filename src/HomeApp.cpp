#include "HomeApp.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "AudioApp.h"
#include "GraphApp.h"
#include "PomodoroApp.h"
#include "SettingsApp.h"
#include "PomodoroStore.h"
#include "StudyApp.h"

// The header keeps the clock on the left and the gear on the right; the four
// app cards sit on one row, then a quiet status line at the bottom.  Four
// cards share the 300 px content width: 4 x 69 + 3 x 8 gap.
static const int HOME_CARD_Y = 58;
static const int HOME_CARD_W = 69;
static const int HOME_CARD_H = 112;
static const int HOME_CARD_X[4] = { 10, 87, 164, 241 };

void drawHomeClock() {
  int h, m;
  if (!getClock(h, m)) m = homeLastMinute;
  homeLastMinute = m;
  tft.fillRect(8, 0, 104, 34, SURFACE_COLOR);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(timeSynced ? TEXT_COLOR : MUTED_COLOR);
  tft.setCursor(14, 25);
  tft.print(getTimeString());
  tft.setFont(NULL);
}

static void drawHomeStatus() {
  // Two rows of status pills answer "is everything ready?" without opening an
  // app: storage and earbuds on top, today's progress and the date below.
  const int y = 182;
  tft.fillRect(0, y, 320, 58, BG_COLOR);
  String storage = sdReady ? (String(numTracks) + (numTracks == 1 ? " track" : " tracks")) : String("no SD card");
  drawStatusPill(12, y, 148, storage.c_str(), sdReady ? PLOT_COLOR : MUTED_COLOR, sdReady ? TEXT_COLOR : MUTED_COLOR);
  drawStatusPill(168, y, 140, btConnected ? "earbuds linked" : "earbuds off",
                 btConnected ? PLOT_COLOR : MUTED_COLOR, btConnected ? TEXT_COLOR : MUTED_COLOR);
  if (pomoRunning) {
    String left = formatTime(pomoSeconds) + " left";
    drawStatusPill(12, y + 22, 148, left.c_str(), ACCENT_COLOR, TEXT_COLOR);
  } else {
    int done = pomoStatBlocks(pomoTodayDay());
    String goal = String(done) + "/" + String(pomoDailyGoal) + " blocks today";
    drawStatusPill(12, y + 22, 148, goal.c_str(), 0, MUTED_COLOR);
  }
  drawStatusPill(168, y + 22, 140, dateLabelShort().c_str(), 0, MUTED_COLOR);
}

void drawHomeScreen() {
  tft.fillScreen(BG_COLOR);
  // Same chrome as every other screen; the clock and the gear ride on top of
  // the shared bar.
  drawScreenHeader("POCKET HUB", false);
  drawModernButton(280, 5, 34, 24, RADIUS_SM, SURFACE_HI, false);
  drawGearIcon(297, 17, 8, TEXT_COLOR);
  drawHomeClock();

  // Each tile: the app colour lives in the icon, the frame stays neutral, so
  // the row reads as one set instead of three competing colours.
  const int iconCY = HOME_CARD_Y + 42;

  drawIconTile(HOME_CARD_X[0], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, F1_COLOR);
  printCentered("f(x)", HOME_CARD_X[0] + (HOME_CARD_W / 2), iconCY + 7, &FreeSansBold18pt7b, F1_COLOR);
  printCentered("Grapher", HOME_CARD_X[0] + (HOME_CARD_W / 2), HOME_CARD_Y + 86, &FreeSans9pt7b, TEXT_COLOR);

  drawIconTile(HOME_CARD_X[1], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, F2_COLOR);
  drawWaveIcon(HOME_CARD_X[1] + (HOME_CARD_W / 2), iconCY, 16, 10, F2_COLOR);
  printCentered("Music", HOME_CARD_X[1] + (HOME_CARD_W / 2), HOME_CARD_Y + 86, &FreeSans9pt7b, TEXT_COLOR);

  drawIconTile(HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, ACCENT_COLOR);
  drawClockIcon(HOME_CARD_X[2] + (HOME_CARD_W / 2), iconCY, 13, ACCENT_COLOR);
  printCentered("Timer", HOME_CARD_X[2] + (HOME_CARD_W / 2), HOME_CARD_Y + 86, &FreeSans9pt7b, TEXT_COLOR);

  // Study: the card count is on the tile itself, so the notes are one tap away
  // and the home screen still says whether anything is on the card.
  drawIconTile(HOME_CARD_X[3], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, F4_COLOR);
  drawBookIcon(HOME_CARD_X[3] + (HOME_CARD_W / 2), iconCY, 22, F4_COLOR);
  printCentered("Study", HOME_CARD_X[3] + (HOME_CARD_W / 2), HOME_CARD_Y + 86, &FreeSans9pt7b, TEXT_COLOR);

  drawHomeStatus();
}

void handleHomeTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  unsigned long t = millis();
  while (ts.touched() && millis() - t < 500) delay(10);
  if (inRect(sx, sy, HOME_CARD_X[0], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H)) {
    flashButton(HOME_CARD_X[0], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG);
    currentState = STATE_GRAPH;
    tft.fillScreen(BG_COLOR);
    drawGraphScreen(true);
  } else if (inRect(sx, sy, HOME_CARD_X[1], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H)) {
    flashButton(HOME_CARD_X[1], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG);
    if (!btInitialized) {
      tft.fillScreen(BG_COLOR);
      printCentered("Initializing Audio...", 160, 100, &FreeSansBold9pt7b, TEXT_COLOR);
      // The check comes first and counts the ring buffer, so a failed attempt
      // costs nothing: the 16 KB is not left allocated for the next try.
      uint32_t heap = (uint32_t)ESP.getFreeHeap();
      uint32_t need = (uint32_t)BT_MIN_HEAP + (uint32_t)RING_BUF_SIZE;
      Serial.printf("[I][music] heap %u, Bluetooth needs %u\n", (unsigned)heap, (unsigned)need);
      if (heap < need) {
        printCentered("Not enough memory for Bluetooth", 160, 130, &FreeSans9pt7b, DEL_COLOR);
        printCentered("Close other apps and try again", 160, 148, &FreeSans9pt7b, MUTED_COLOR);
        delay(2500);
        drawHomeScreen();
        return;
      }
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
  } else if (inRect(sx, sy, HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H)) {
    flashButton(HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG);
    currentState = STATE_POMODORO;
    tft.fillScreen(BG_COLOR);
    drawPomodoroScreen(true);
  } else if (inRect(sx, sy, HOME_CARD_X[3], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H)) {
    flashButton(HOME_CARD_X[3], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG);
    studyEnterApp();
  } else if (inRect(sx, sy, 280, 0, 40, 30)) {
    flashButton(280, 3, 34, 24, RADIUS_SM);
    currentState = STATE_SETTINGS;
    drawSettingsScreen();
  }
}
