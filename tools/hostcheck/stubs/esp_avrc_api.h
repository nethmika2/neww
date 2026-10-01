#pragma once
// AVRCP target / controller API stand-in (ESP-IDF v4.4 shape).  The harness
// models the parts the earbud code touches: passthrough command codes, the
// target callback, absolute volume and the notification responses.
#include <Arduino.h>
#include <vector>

typedef enum {
  ESP_AVRC_TG_CONNECTION_STATE_EVT = 0,
  ESP_AVRC_TG_REMOTE_FEATURES_EVT,
  ESP_AVRC_TG_PASSTHROUGH_CMD_EVT,
  ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,
  ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT,
  ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT,
} esp_avrc_tg_cb_event_t;

typedef enum {
  ESP_AVRC_PT_CMD_PLAY = 0x44,
  ESP_AVRC_PT_CMD_STOP = 0x45,
  ESP_AVRC_PT_CMD_PAUSE = 0x46,
  ESP_AVRC_PT_CMD_REWIND = 0x48,
  ESP_AVRC_PT_CMD_FAST_FORWARD = 0x49,
  ESP_AVRC_PT_CMD_FORWARD = 0x4B,
  ESP_AVRC_PT_CMD_BACKWARD = 0x4C,
  ESP_AVRC_PT_CMD_VOL_UP = 0x41,
  ESP_AVRC_PT_CMD_VOL_DOWN = 0x42,
  ESP_AVRC_PT_CMD_MUTE = 0x43,
} esp_avrc_pt_cmd_t;

#define ESP_AVRC_PT_CMD_STATE_PRESSED 0
#define ESP_AVRC_PT_CMD_STATE_RELEASED 1

typedef enum {
  ESP_AVRC_PLAYBACK_STOPPED = 0,
  ESP_AVRC_PLAYBACK_PLAYING = 1,
  ESP_AVRC_PLAYBACK_PAUSED = 2,
  ESP_AVRC_PLAYBACK_FWD_SEEK = 3,
  ESP_AVRC_PLAYBACK_REV_SEEK = 4,
  ESP_AVRC_PLAYBACK_ERROR = 0xFF,
} esp_avrc_playback_stat_t;

typedef enum {
  ESP_AVRC_RN_PLAY_STATUS_CHANGE = 0x01,
  ESP_AVRC_RN_TRACK_CHANGE = 0x02,
  ESP_AVRC_RN_PLAY_POS_CHANGED = 0x05,
  ESP_AVRC_RN_BATTERY_STATUS_CHANGE = 0x06,
  ESP_AVRC_RN_VOLUME_CHANGE = 0x0d,
} esp_avrc_rn_event_ids_t;

typedef enum {
  ESP_AVRC_RN_RSP_INTERIM = 13,
  ESP_AVRC_RN_RSP_CHANGED = 15,
} esp_avrc_rn_rsp_t;

typedef union {
  uint8_t volume;
  esp_avrc_playback_stat_t playback;
  uint8_t elm_id[8];
  uint32_t play_pos;
} esp_avrc_rn_param_t;

typedef uint8_t esp_bd_addr_t[6];

struct esp_avrc_rn_evt_cap_mask_t {
  uint32_t bits;
  bool has(esp_avrc_rn_event_ids_t e) const { return (bits >> e) & 1; }
};
struct esp_avrc_psth_bit_mask_t {
  uint32_t bits;
};
typedef enum { ESP_AVRC_BIT_MASK_OP_SET = 0, ESP_AVRC_BIT_MASK_OP_CLEAR, ESP_AVRC_BIT_MASK_OP_TEST } esp_avrc_bit_mask_op_t;
typedef enum { ESP_AVRC_PSTH_FILTER_ALLOWED_CMD = 0, ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD } esp_avrc_psth_filter_t;
typedef enum { ESP_AVRC_RN_EVT_CAP_SUPPORTED = 0, ESP_AVRC_RN_EVT_CAP_NOTIFY } esp_avrc_rn_evt_cap_t;

inline void esp_avrc_rn_evt_bit_mask_operation(esp_avrc_bit_mask_op_t op, esp_avrc_rn_evt_cap_mask_t *set, esp_avrc_rn_event_ids_t e) {
  if (op == ESP_AVRC_BIT_MASK_OP_SET) set->bits |= (1u << e);
  else if (op == ESP_AVRC_BIT_MASK_OP_CLEAR) set->bits &= ~(1u << e);
}

typedef struct {
  struct {
    bool connected;
    esp_bd_addr_t remote_bda;
  } conn_stat;
  struct {
    uint32_t feat_mask;
    uint16_t ct_feat_flag;
    esp_bd_addr_t remote_bda;
  } rmt_feats;
  struct {
    uint8_t key_code;
    uint8_t key_state;
  } psth_cmd;
  struct {
    uint8_t volume;
  } set_abs_vol;
  struct {
    uint8_t event_id;
    uint32_t event_parameter;
  } reg_ntf;
  struct {
    uint8_t num_val;
    void *p_vals;
  } set_app_value;
} esp_avrc_tg_cb_param_t;

typedef void (*esp_avrc_tg_cb_t)(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

esp_err_t esp_avrc_tg_init();
esp_err_t esp_avrc_tg_deinit();
esp_err_t esp_avrc_tg_register_callback(esp_avrc_tg_cb_t callback);
esp_err_t esp_avrc_tg_set_rn_evt_cap(const esp_avrc_rn_evt_cap_mask_t *evt_set);
esp_err_t esp_avrc_tg_get_psth_cmd_filter(esp_avrc_psth_filter_t filter, esp_avrc_psth_bit_mask_t *cmd_set);
esp_err_t esp_avrc_tg_set_psth_cmd_filter(esp_avrc_psth_filter_t filter, const esp_avrc_psth_bit_mask_t *cmd_set);
esp_err_t esp_avrc_tg_send_rn_rsp(esp_avrc_rn_event_ids_t event_id, esp_avrc_rn_rsp_t rsp, esp_avrc_rn_param_t *param);

// ---- harness side model ---------------------------------------------------
// Records what the target callback answered, so a test can assert that the
// buds get their interim/short responses.
struct HostAvrc {
  esp_avrc_tg_cb_t registered = nullptr;
  std::vector<std::pair<int, int>> rnResponses;  // (event id, rsp)
  int rnResponseCount(esp_avrc_rn_event_ids_t id, int rsp) {
    int n = 0;
    for (auto &r : rnResponses) if (r.first == (int)id && r.second == rsp) n++;
    return n;
  }
  void reset() { rnResponses.clear(); }
  void deliver(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param) {
    if (registered) registered(event, param);
  }
};
extern HostAvrc hostAvrc;
