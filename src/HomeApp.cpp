#include "HomeApp.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "TimeService.h"
#include "AudioApp.h"
#include "GraphApp.h"
#include "PomodoroApp.h"
#include "SettingsApp.h"
#include "PomodoroStore.h"

// The header keeps the clock on the left and the gear on the right; the three
// app cards sit on one row, then a quiet status line at the bottom.
static const int HOME_CARD_Y = 58;
static const int HOME_CARD_W = 96;
static const int HOME_CARD_H = 112;
static const int HOME_CARD_X[3] = { 12, 116, 220 };

void drawHomeClock() {
  int h, m;
  if (!getClock(h, m)) m = homeLastMinute;
  homeLastMinute = m;
  tft.fillRect(8, 0, 96, 30, SURFACE_COLOR);
  tft.setFont(&FreeSansBold9pt7b);
  tft.setTextColor(timeSynced ? TEXT_COLOR : MUTED_COLOR);
  tft.setCursor(14, 20);
  tft.print(getTimeString());
  tft.setFont(NULL);
}

static void drawHomeStatus() {
  // One line that answers "is everything ready?" without opening an app.
  int y = 190;
  tft.fillRect(0, y, 320, 40, BG_COLOR);
  String storage = sdReady ? (String(numTracks) + (numTracks == 1 ? " track" : " tracks")) : String("no SD card");
  drawStatusPill(12, y + 2, 148, storage.c_str(), sdReady ? PLOT_COLOR : MUTED_COLOR, sdReady ? TEXT_COLOR : MUTED_COLOR);
  drawStatusPill(168, y + 2, 140, btConnected ? "earbuds connected" : "earbuds off", btConnected ? PLOT_COLOR : MUTED_COLOR,
                 btConnected ? TEXT_COLOR : MUTED_COLOR);
  if (pomoRunning) {
    String left = formatTime(pomoSeconds) + " left";
    drawStatusPill(12, y + 24, 148, left.c_str(), ACCENT_COLOR, ACCENT_COLOR);
  } else {
    int done = pomoStatBlocks(pomoTodayDay());
    String goal = String(done) + "/" + String(pomoDailyGoal) + " blocks today";
    drawStatusPill(12, y + 24, 148, goal.c_str(), 0, MUTED_COLOR);
  }
  String today = dateLabelShort();
  drawStatusPill(168, y + 24, 140, today.c_str(), 0, MUTED_COLOR);
}

void drawHomeScreen() {
  tft.fillScreen(BG_COLOR);
  tft.fillRect(0, 0, 320, 30, SURFACE_COLOR);
  tft.drawFastHLine(0, 30, 320, BTN_OUTLINE);
  printCentered("SMARTPAD", 160, 20, &FreeSansBold9pt7b, MUTED_COLOR);
  drawModernButton(280, 3, 34, 24, RADIUS_SM, SURFACE_HI, false);
  drawGearIcon(297, 15, 8, TEXT_COLOR);
  drawHomeClock();

  // Each card: one icon in the upper half, the name on the 9 pt line.  No
  // subtitle, so the tiles stay quiet and nothing can crowd the edges.
  const int iconCY = HOME_CARD_Y + 40;

  // Grapher
  drawIconTile(HOME_CARD_X[0], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, F1_COLOR);
  printCentered("f(x)", HOME_CARD_X[0] + (HOME_CARD_W / 2), iconCY + 9, &FreeSansBold18pt7b, F1_COLOR);
  printCentered("Grapher", HOME_CARD_X[0] + (HOME_CARD_W / 2), HOME_CARD_Y + 84, &FreeSans9pt7b, TEXT_COLOR);

  // Music
  drawIconTile(HOME_CARD_X[1], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, F2_COLOR);
  drawWaveIcon(HOME_CARD_X[1] + (HOME_CARD_W / 2), iconCY, 20, 12, F2_COLOR);
  printCentered("Music", HOME_CARD_X[1] + (HOME_CARD_W / 2), HOME_CARD_Y + 84, &FreeSans9pt7b, TEXT_COLOR);

  // Timer
  drawIconTile(HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG, ACCENT_COLOR);
  drawClockIcon(HOME_CARD_X[2] + (HOME_CARD_W / 2), iconCY, 15, ACCENT_COLOR);
  printCentered("Timer", HOME_CARD_X[2] + (HOME_CARD_W / 2), HOME_CARD_Y + 84, &FreeSans9pt7b, TEXT_COLOR);

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
  } else if (inRect(sx, sy, HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H)) {
    flashButton(HOME_CARD_X[2], HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H, RADIUS_LG);
    currentState = STATE_POMODORO;
    tft.fillScreen(BG_COLOR);
    drawPomodoroScreen(true);
  } else if (inRect(sx, sy, 280, 0, 40, 30)) {
    flashButton(280, 3, 34, 24, RADIUS_SM);
    currentState = STATE_SETTINGS;
    drawSettingsScreen();
  }
}
