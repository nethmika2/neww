#include "EarbudControls.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "AudioApp.h"
#include "esp_avrc_api.h"
#include "esp_log.h"

// The library hands passthrough commands to us, but it never answers an AVRCP
// "register notification" request and it ignores absolute volume commands.
// Both matter in practice: a controller (the earbuds) that does not get the
// interim notification response it asked for gives up on the target device and
// stops sending its buttons at all, and buds that drive the volume through the
// absolute volume profile have no other way to change the loudness.
//
// The ESP32-A2DP library owns the AVRCP target callback slot, so the calls it
// is missing are added by installing our own callback *after* the stack is up
// and forwarding every event to the library afterwards, which keeps its
// passthrough handling (and the filter setup that makes taps arrive) intact.
extern "C" void ccall_app_rc_tg_callback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

static const char *EB_TAG = "buds";

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
  EB_STOP = 32,
  EB_VOL_SET = 64
};

static volatile uint8_t earbudPending = 0;
static volatile uint8_t earbudVolumeRequest = 0;  // 0..100, valid when EB_VOL_SET
static uint8_t lastKeyCode = 0;
static unsigned long lastKeyTime = 0;
static bool earbudEnabled = true;

// Notification bookkeeping (written in the Bluetooth task, read in the loop).
static volatile bool notifyVolume = false;
static volatile bool notifyPlayStatus = false;
static bool tgCallbackAttached = false;
static bool loggedConnection = false;

// Playback state we report back to the buds, and the last state we told them.
static uint8_t reportedPlayState = ESP_AVRC_PLAYBACK_STOPPED;
static uint8_t lastSentPlayState = 0xFF;
static int lastSentVolume = -1;

static volatile int eventCount = 0;
static char lastEventName[18] = "none";

static void recordEvent(const char *name) {
  eventCount++;
  strncpy(lastEventName, name, sizeof(lastEventName) - 1);
  lastEventName[sizeof(lastEventName) - 1] = 0;
}

int earbudEventCount() {
  return eventCount;
}

String earbudLastEvent() {
  return String(lastEventName);
}

bool earbudControlsEnabled() {
  return earbudEnabled;
}

void earbudControlsSetEnabled(bool on) {
  earbudEnabled = on;
  prefs.putBool("earbud", on);
  if (!on) earbudPending = 0;
}

static uint8_t volumeToWire(int percent) {
  int wire = (percent * 0x7F) / 100;
  return (uint8_t)constrain(wire, 0, 0x7F);
}

// Tells the buds about a change they asked to be notified about.  Only sent for
// events the controller actually registered, as required by AVRCP.
static void notifyPlaybackState(uint8_t state) {
  if (!notifyPlayStatus) return;
  esp_avrc_rn_param_t rn;
  rn.playback = (esp_avrc_playback_stat_t)state;
  esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_PLAY_STATUS_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn);
  notifyPlayStatus = false;  // the controller re-registers after a change
  lastSentPlayState = state;
}

static void notifyVolumeChange(int percent) {
  if (!notifyVolume) return;
  esp_avrc_rn_param_t rn;
  rn.volume = volumeToWire(percent);
  esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn);
  notifyVolume = false;
  lastSentVolume = percent;
}

// ==========================================
// AVRCP TARGET CALLBACK (Bluetooth task context)
// ==========================================
static void earbudTgCallback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
  switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
      loggedConnection = param->conn_stat.connected;
      ESP_LOGI(EB_TAG, "AVRCP %s", param->conn_stat.connected ? "connected" : "disconnected");
      if (!param->conn_stat.connected) {
        notifyVolume = false;
        notifyPlayStatus = false;
      }
      break;

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
      uint8_t id = param->reg_ntf.event_id;
      esp_avrc_rn_param_t rn;
      memset(&rn, 0, sizeof(rn));
      if (id == ESP_AVRC_RN_VOLUME_CHANGE) {
        notifyVolume = true;
        rn.volume = volumeToWire(currentVolume);
      } else if (id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) {
        notifyPlayStatus = true;
        rn.playback = (esp_avrc_playback_stat_t)reportedPlayState;
      }
      // The interim response has to arrive quickly (T_mtp, 1 s); without it the
      // controller marks the target as unresponsive and the touch controls stop
      // working even though the audio link is fine.
      esp_err_t err = esp_avrc_tg_send_rn_rsp((esp_avrc_rn_event_ids_t)id, ESP_AVRC_RN_RSP_INTERIM, &rn);
      ESP_LOGI(EB_TAG, "register notify %d -> %s", id, err == ESP_OK ? "interim sent" : "send failed");
      break;
    }

    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
      // Volume taps from the buds arrive here on absolute-volume devices.
      ESP_LOGI(EB_TAG, "absolute volume %d", param->set_abs_vol.volume);
      earbudAbsoluteVolumeHandler(param->set_abs_vol.volume);
      break;

    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
      // The library answers this one, but it is logged here as well so a
      // capture of the serial output shows exactly which buttons the buds send.
      ESP_LOGI(EB_TAG, "key 0x%02x state %d", param->psth_cmd.key_code, param->psth_cmd.key_state);
      break;

    default:
      break;
  }
  // Keep the library's own handling (passthrough filter on connect, dispatch of
  // the passthrough callback we registered before start()).
  if (actual_bluetooth_a2dp_common) ccall_app_rc_tg_callback(event, param);
}

static void earbudControlsAttachTarget() {
  if (tgCallbackAttached) return;
  if (esp_avrc_tg_register_callback(earbudTgCallback) == ESP_OK) {
    tgCallbackAttached = true;
    ESP_LOGI(EB_TAG, "AVRCP target notifications enabled");
  }
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
  const char *name = "other";
  switch (key) {
    case ESP_AVRC_PT_CMD_PLAY:
      bit = EB_PLAY_PAUSE;
      name = "PLAY";
      break;
    case ESP_AVRC_PT_CMD_PAUSE:
      bit = EB_PLAY_PAUSE;
      name = "PAUSE";
      break;
    case ESP_AVRC_PT_CMD_FORWARD:
      bit = EB_NEXT;
      name = "NEXT";
      break;
    case ESP_AVRC_PT_CMD_FAST_FORWARD:
      bit = EB_NEXT;
      name = "FFWD";
      break;
    case ESP_AVRC_PT_CMD_BACKWARD:
      bit = EB_PREV;
      name = "PREV";
      break;
    case ESP_AVRC_PT_CMD_REWIND:
      bit = EB_PREV;
      name = "REWIND";
      break;
    case ESP_AVRC_PT_CMD_VOL_UP:
      bit = EB_VOL_UP;
      name = "VOL+";
      break;
    case ESP_AVRC_PT_CMD_VOL_DOWN:
      bit = EB_VOL_DOWN;
      name = "VOL-";
      break;
    case ESP_AVRC_PT_CMD_STOP:
      bit = EB_STOP;
      name = "STOP";
      break;
    default:
      return;
  }
  recordEvent(name);
  earbudPending |= bit;
}

void earbudAbsoluteVolumeHandler(uint8_t wireVolume) {
  if (!earbudEnabled) return;
  int percent = constrain(((int)wireVolume * 100 + 63) / 127, 0, 100);
  earbudVolumeRequest = (uint8_t)percent;
  recordEvent("ABS VOL");
  earbudPending |= EB_VOL_SET;
}

void earbudControlsPrepare() {
  earbudEnabled = prefs.getBool("earbud", true);
  // Registered even while the feature is switched off: the AVRCP target has to
  // exist before the stack starts, and the handler itself checks the setting,
  // so the Settings toggle takes effect immediately.
  a2dp_source.set_avrc_passthru_command_callback(earbudPassthruHandler);
  // Advertise the notifications the buds rely on.  The library pushes this list
  // into the AVRCP target during start().
  a2dp_source.set_avrc_rn_events({ ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_PLAY_STATUS_CHANGE });
}

// ==========================================
// MAIN LOOP HANDLING
// ==========================================
void earbudControlsPoll() {
  // The stack has finished coming up by the time a device is connected, so this
  // is the safe moment to take over the target callback without racing the
  // library's own registration.
  if (btInitialized && a2dp_source.is_connected()) earbudControlsAttachTarget();

  uint8_t pending = earbudPending;
  if (!pending) {
    // Even without a fresh button press there can be a state change to report.
    uint8_t state = isPlaying ? ESP_AVRC_PLAYBACK_PLAYING : (audioFile ? ESP_AVRC_PLAYBACK_PAUSED : ESP_AVRC_PLAYBACK_STOPPED);
    if (state != reportedPlayState) {
      reportedPlayState = state;
      if (state != lastSentPlayState) notifyPlaybackState(state);
    }
    if (currentVolume != lastSentVolume) notifyVolumeChange(currentVolume);
    return;
  }
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
  if (pending & EB_VOL_SET) {
    int percent = earbudVolumeRequest;
    if (percent != currentVolume) {
      currentVolume = percent;
      applyVolume();
      prefs.putInt("volume", currentVolume);
    }
    msg = "Buds: volume " + String(currentVolume) + "%";
  }
  if (pending & (EB_VOL_UP | EB_VOL_DOWN)) {
    int step = (pending & EB_VOL_UP) ? 5 : -5;
    currentVolume = constrain(currentVolume + step, 0, 100);
    applyVolume();
    prefs.putInt("volume", currentVolume);
    msg = "Buds: volume " + String(currentVolume) + "%";
  }

  // Report the new state back to the buds so their own display/announcements
  // stay in step with what the player is doing.
  uint8_t state = isPlaying ? ESP_AVRC_PLAYBACK_PLAYING : (audioFile ? ESP_AVRC_PLAYBACK_PAUSED : ESP_AVRC_PLAYBACK_STOPPED);
  if (state != reportedPlayState) {
    reportedPlayState = state;
    if (state != lastSentPlayState) notifyPlaybackState(state);
  }
  if (currentVolume != lastSentVolume) notifyVolumeChange(currentVolume);

  if (msg.length() == 0) return;

  // Feedback only where the player is already on screen; a button press should
  // never disturb the grapher or another app.
  if (displayActive() && (currentState == STATE_MUSIC || currentState == STATE_MUSIC_LIST)) {
    showToast(msg);
    redrawCurrentScreen();
  }
}
