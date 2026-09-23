#include "EarbudControls.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "AudioApp.h"
#include "esp_avrc_api.h"

// ==========================================
// PENDING ACTIONS
// ==========================================
// Set by the Bluetooth task, consumed by the main loop.  Single byte flags, so
// the |= / = pattern below cannot tear on the ESP32.
enum EarbudActionBits {
  EB_PLAY_PAUSE = 1,
  EB_NEXT = 2,
  EB_PREV = 4,
  EB_VOL_UP = 8,
  EB_VOL_DOWN = 16,
  EB_STOP = 32
};

static volatile uint8_t earbudPending = 0;
static uint8_t lastKeyCode = 0;
static unsigned long lastKeyTime = 0;
static bool earbudEnabled = true;

bool earbudControlsEnabled() {
  return earbudEnabled;
}

void earbudControlsSetEnabled(bool on) {
  earbudEnabled = on;
  prefs.putBool("earbud", on);
  if (!on) earbudPending = 0;
}

// ==========================================
// PASSTHROUGH HANDLER (Bluetooth task context)
// ==========================================
void earbudPassthruHandler(uint8_t key, bool isReleased) {
  if (!earbudEnabled) return;
  bool volumeKey = (key == ESP_AVRC_PT_CMD_VOL_UP || key == ESP_AVRC_PT_CMD_VOL_DOWN);
  if (!volumeKey) {
    // Transport buttons arrive as a press/release pair.  Acting on the press
    // only (and ignoring a repeated press) keeps a single tap to one action.
    if (isReleased) return;
    unsigned long now = millis();
    if (key == lastKeyCode && now - lastKeyTime < 150) return;
    lastKeyCode = key;
    lastKeyTime = now;
  }
  uint8_t bit = 0;
  switch (key) {
    case ESP_AVRC_PT_CMD_PLAY:
    case ESP_AVRC_PT_CMD_PAUSE:
      bit = EB_PLAY_PAUSE;
      break;
    case ESP_AVRC_PT_CMD_FORWARD:
    case ESP_AVRC_PT_CMD_FAST_FORWARD:
      bit = EB_NEXT;
      break;
    case ESP_AVRC_PT_CMD_BACKWARD:
    case ESP_AVRC_PT_CMD_REWIND:
      bit = EB_PREV;
      break;
    case ESP_AVRC_PT_CMD_VOL_UP:
      bit = EB_VOL_UP;
      break;
    case ESP_AVRC_PT_CMD_VOL_DOWN:
      bit = EB_VOL_DOWN;
      break;
    case ESP_AVRC_PT_CMD_STOP:
      bit = EB_STOP;
      break;
    default:
      return;
  }
  earbudPending |= bit;
}

void earbudControlsPrepare() {
  earbudEnabled = prefs.getBool("earbud", true);
  // Registered even while the feature is switched off: the AVRCP target has to
  // exist before the stack starts, and the handler itself checks the setting,
  // so the Settings toggle takes effect immediately.
  a2dp_source.set_avrc_passthru_command_callback(earbudPassthruHandler);
}

// ==========================================
// MAIN LOOP HANDLING
// ==========================================
void earbudControlsPoll() {
  uint8_t pending = earbudPending;
  if (!pending) return;
  earbudPending = 0;
  if (!earbudEnabled || !btInitialized) return;

  String msg = "";
  if (pending & EB_PLAY_PAUSE) {
    if (numTracks == 0) {
      if (sdReady) loadPlaylist();
      if (numTracks > 0) playTrack(0);
    }
    if (numTracks > 0) {
      if (isPlaying) {
        isPlaying = false;
        msg = "Buds: pause";
      } else {
        if (!audioFile || !currentWav.valid) playTrack(currentTrack);
        else isPlaying = true;
        msg = "Buds: play";
      }
    } else {
      msg = "Buds: no tracks";
    }
  }
  if (pending & EB_STOP) {
    isPlaying = false;
    msg = "Buds: stop";
  }
  if (pending & EB_NEXT) {
    if (numTracks == 0 && sdReady) loadPlaylist();
    if (numTracks > 0) {
      playTrack(currentTrack + 1);
      msg = "Buds: next track";
    }
  }
  if (pending & EB_PREV) {
    if (numTracks == 0 && sdReady) loadPlaylist();
    if (numTracks > 0) {
      playTrack(currentTrack - 1);
      msg = "Buds: previous track";
    }
  }
  if (pending & (EB_VOL_UP | EB_VOL_DOWN)) {
    int step = (pending & EB_VOL_UP) ? 5 : -5;
    currentVolume = constrain(currentVolume + step, 0, 100);
    applyVolume();
    prefs.putInt("volume", currentVolume);
    msg = "Buds: volume " + String(currentVolume) + "%";
  }
  if (msg.length() == 0) return;

  // Feedback only where the player is already on screen; a button press should
  // never disturb the grapher or another app.
  if (displayActive() && (currentState == STATE_MUSIC || currentState == STATE_MUSIC_LIST)) {
    showToast(msg);
    redrawCurrentScreen();
  }
}
