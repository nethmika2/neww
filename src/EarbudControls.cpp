#include "EarbudControls.h"
#include "Globals.h"
#include "DisplayUtils.h"
#include "AudioApp.h"
#include "esp_avrc_api.h"

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
//
// The callback itself only records what happened.  Replies are sent from a
// small task instead, because the callback runs in the Bluetooth task: its
// stack is a few KB, and the reply APIs must not be called from inside it.
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

// Notification bookkeeping.  Set in the Bluetooth task, acted on by the
// notifier task: volatile flags only, no calls, no printing.
enum EarbudNotifyBits { EB_NTF_VOLUME_INTERIM = 1, EB_NTF_PLAY_INTERIM = 2 };
static volatile uint8_t interimWanted = 0;   // a registration is waiting to be answered
static volatile uint8_t notifyWanted = 0;    // a change is waiting to be reported
static volatile bool notifyVolume = false;   // the controller holds an open interim
static volatile bool notifyPlayStatus = false;
static TaskHandle_t earbudNotifyTaskHandle = nullptr;
static bool tgCallbackAttached = false;
static bool loggedConnection = false;

// Playback state we report back to the buds, and the last state we told them.
static uint8_t reportedPlayState = ESP_AVRC_PLAYBACK_STOPPED;
static uint8_t lastSentPlayState = 0xFF;
static int lastSentVolume = -1;
// Last reply the notifier task sent, for the Settings diagnostics + the log.
static volatile int lastNotifyError = 0;
static volatile int lastNotifyEvent = 0;

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

// Sends one notification response.  Called from the notifier task, never from
// the Bluetooth callback.
static void sendRnResponse(uint8_t id, esp_avrc_playback_stat_t playState, int percent, esp_avrc_rn_rsp_t rsp) {
  esp_avrc_rn_param_t rn;
  memset(&rn, 0, sizeof(rn));
  if (id == ESP_AVRC_RN_VOLUME_CHANGE) rn.volume = volumeToWire(percent);
  else rn.playback = playState;
  esp_err_t err = esp_avrc_tg_send_rn_rsp((esp_avrc_rn_event_ids_t)id, rsp, &rn);
  lastNotifyError = (int)err;
  lastNotifyEvent = id;
}

static uint8_t currentPlayState() {
  return isPlaying ? ESP_AVRC_PLAYBACK_PLAYING
                   : (audioFile ? ESP_AVRC_PLAYBACK_PAUSED : ESP_AVRC_PLAYBACK_STOPPED);
}

// Answers a registration (INTERIM) and, once an interim is outstanding, a change
// (CHANGED) - the controller re-registers after every change.  Both are sent
// promptly: the spec gives the target one second (T_mtp) to answer, and a
// controller that is kept waiting stops sending its buttons.
// One pass of the notifier: answers a waiting registration and reports a waiting
// change.  Exposed so the host tests can step it without a real task.
void earbudNotifyStep() {
  {
    uint8_t wanted = interimWanted;
    if (wanted & EB_NTF_VOLUME_INTERIM) {
      interimWanted &= (uint8_t)~EB_NTF_VOLUME_INTERIM;
      notifyVolume = true;
      sendRnResponse(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_PLAYBACK_STOPPED, currentVolume,
                     ESP_AVRC_RN_RSP_INTERIM);
    }
    if (wanted & EB_NTF_PLAY_INTERIM) {
      interimWanted &= (uint8_t)~EB_NTF_PLAY_INTERIM;
      notifyPlayStatus = true;
      sendRnResponse(ESP_AVRC_RN_PLAY_STATUS_CHANGE, (esp_avrc_playback_stat_t)reportedPlayState, 0,
                     ESP_AVRC_RN_RSP_INTERIM);
    }
    uint8_t change = notifyWanted;
    if (change & EB_NTF_VOLUME_INTERIM) {
      notifyWanted &= (uint8_t)~EB_NTF_VOLUME_INTERIM;
      if (notifyVolume) {
        sendRnResponse(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_PLAYBACK_STOPPED, currentVolume,
                       ESP_AVRC_RN_RSP_CHANGED);
        notifyVolume = false;   // the controller re-registers for the next change
      }
      lastSentVolume = currentVolume;
    }
    if (change & EB_NTF_PLAY_INTERIM) {
      notifyWanted &= (uint8_t)~EB_NTF_PLAY_INTERIM;
      if (notifyPlayStatus) {
        sendRnResponse(ESP_AVRC_RN_PLAY_STATUS_CHANGE,
                       (esp_avrc_playback_stat_t)reportedPlayState, 0, ESP_AVRC_RN_RSP_CHANGED);
        notifyPlayStatus = false;
      }
      lastSentPlayState = reportedPlayState;
    }
  }
}

static void earbudNotifyTask(void*) {
  while (true) {
    earbudNotifyStep();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// ==========================================
// AVRCP TARGET CALLBACK (Bluetooth task context)
// ==========================================
static void earbudTgCallback(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
  // Bluetooth task context: only record what happened here.  Everything that
  // talks to the stack (the notification replies, the volume reply) is done by
  // the notifier task and the main loop, and nothing is printed.
  switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
      loggedConnection = param->conn_stat.connected;
      if (!param->conn_stat.connected) {
        notifyVolume = false;
        notifyPlayStatus = false;
        interimWanted = 0;
        notifyWanted = 0;
      }
      break;

    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
      uint8_t id = param->reg_ntf.event_id;
      if (id == ESP_AVRC_RN_VOLUME_CHANGE) interimWanted |= EB_NTF_VOLUME_INTERIM;
      else if (id == ESP_AVRC_RN_PLAY_STATUS_CHANGE) interimWanted |= EB_NTF_PLAY_INTERIM;
      break;
    }

    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
      // Volume taps from the buds arrive here on absolute-volume devices; the
      // library ignores them, so the new level is queued for the main loop.
      earbudAbsoluteVolumeHandler(param->set_abs_vol.volume);
      break;

    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
      // Handled by the library through the forward below; recorded here so the
      // Settings counter and the log can show which buttons the buds send.
      recordEvent(param->psth_cmd.key_state == 0 ? "KEY DOWN" : "KEY UP");
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
    Serial.printf("[I][buds] AVRCP target notifications enabled\n");
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
  // Replies go out from here, not from the Bluetooth callback: the callback's
  // task has a few KB of stack and is not the place to call stack APIs from.
  if (!earbudNotifyTaskHandle) {
    xTaskCreatePinnedToCore(earbudNotifyTask, "BudsNotify", 2048, NULL, 3, &earbudNotifyTaskHandle, 1);
  }
}

int earbudNotifyLastError() { return lastNotifyError; }
int earbudNotifyLastEvent() { return lastNotifyEvent; }

// ==========================================
// MAIN LOOP HANDLING
// ==========================================
void earbudControlsPoll() {
  // The stack has finished coming up by the time a device is connected, so this
  // is the safe moment to take over the target callback without racing the
  // library's own registration.  With the feature switched off the library's own
  // callback is left in place, which is the stock behaviour if anything here is
  // ever suspected of misbehaving.
  if (earbudEnabled && btInitialized && a2dp_source.is_connected()) earbudControlsAttachTarget();

  // Tell the notifier task what the buds should hear about, then get on with the
  // button handling; the task does the actual replies.
  uint8_t stateNow = currentPlayState();
  if (stateNow != reportedPlayState) reportedPlayState = stateNow;
  if (reportedPlayState != lastSentPlayState) notifyWanted |= EB_NTF_PLAY_INTERIM;
  if (currentVolume != lastSentVolume) notifyWanted |= EB_NTF_VOLUME_INTERIM;

  // One line per new event, from the loop rather than from the Bluetooth task.
  static int loggedEvents = -1;
  static unsigned long lastEventLog = 0;
  if (eventCount != loggedEvents && millis() - lastEventLog >= 1000) {
    loggedEvents = eventCount;
    lastEventLog = millis();
    Serial.printf("[I][buds] %s (event %d)\n", lastEventName, eventCount);
  }

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
  stateNow = currentPlayState();
  if (stateNow != reportedPlayState) reportedPlayState = stateNow;
  if (reportedPlayState != lastSentPlayState) notifyWanted |= EB_NTF_PLAY_INTERIM;
  if (currentVolume != lastSentVolume) notifyWanted |= EB_NTF_VOLUME_INTERIM;

  if (msg.length() == 0) return;

  // Feedback only where the player is already on screen; a button press should
  // never disturb the grapher or another app.
  if (displayActive() && (currentState == STATE_MUSIC || currentState == STATE_MUSIC_LIST)) {
    showToast(msg);
    redrawCurrentScreen();
  }
}
