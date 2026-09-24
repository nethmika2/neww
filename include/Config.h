#pragma once
#include <Arduino.h>

// ==========================================
// 1. HARDWARE PINS & LIMITS
// ==========================================
// The CYD has enough heap for a larger expression list while keeping the
// renderer small enough to stay responsive.  Empty slots cost almost no
// flash/storage and make the graph editor feel much closer to Desmos.
#define NUM_FUNCS 6
#define MAX_TRACKS 100
#define MAX_POINTS 10
#define NUM_CUSTOM_VARS 8

// Display SPI (HSPI)
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_CLK 14
#define TFT_CS 15
#define TFT_DC 2
#define TFT_RST -1
#define TFT_BL 21

// Touch Soft-SPI
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25
#define TOUCH_CS 33
#define TOUCH_IRQ 36

#define USE_TOUCH_IRQ 0
#define TOUCH_Z_MIN 150

// SD SPI
#define SD_MOSI 23
#define SD_MISO 19
#define SD_CLK 18
#define SD_CS 5
#define SD_SPI_HZ 20000000

// Wi-Fi & NTP Configuration
static const char* const WIFI_SSID = "Dialog 4G New";
static const char* const WIFI_PASS = "tgrd-0241320-";
static const long GMT_OFFSET_SEC = 5 * 3600 + 30 * 60;
static const int DST_OFFSET_SEC = 0;
static const char* const NTP_1 = "ltentp1.dialog.lk";
static const char* const NTP_2 = "ltentp2.dialog.lk";
static const char* const NTP_3 = "pool.ntp.org";

// ==========================================
// 2. THEME & COLORS (16-bit 565)
// ==========================================
#define BG_COLOR 0x10A3
#define SURFACE_COLOR 0x18E5
#define SURFACE_HI 0x2007
#define SHADOW_COLOR 0x0841
#define BTN_COLOR 0x18E5
#define BTN_OUTLINE 0x2967
#define TEXT_COLOR 0xF7BF
#define MUTED_COLOR 0x8C74
#define AXIS_COLOR 0x3A0A
#define GRID_COLOR 0x2147
#define PLOT_COLOR 0x3E34
#define DEL_COLOR 0xFB4D
#define FUNC_COLOR 0xA37A
#define VAR_COLOR 0x5C7D
#define ACCENT_COLOR 0x5C7D
#define PRESS_COLOR 0x2967
#define POINT_COLOR 0xFFE0

#define F1_COLOR 0x5C7D
#define F2_COLOR 0xFCE8
#define F3_COLOR 0xA37A
#define F4_COLOR 0x3E34
#define F5_COLOR 0x07FF
#define F6_COLOR 0xF81F

#define RADIUS_SM 6
#define RADIUS_MD 10
#define RADIUS_LG 16

// ==========================================
// 3. AUDIO & TIMING CONSTANTS
// ==========================================
#define RING_BUF_SIZE (16 * 1024)
#define FEEDER_CHUNK 2048
#define BT_MIN_HEAP 120000
#define AUDIO_TEST_TONE 0

#define SCREEN_TIMEOUT_MS 60000UL
#define SAVER_OFF_MS 600000UL
// Six compact rows fit comfortably above the music list pager.
#define TRACKS_PER_PAGE 6
#define TRACK_ROW_HEIGHT 27

static const int PAN_THRESHOLD = 8;

// ==========================================
// 4. POMODORO APP
// ==========================================
// The defaults seed the runtime values (pomoWorkTime & co.) the first time the
// timer is started; after that the user's own routine is restored from NVS.
static const int DEFAULT_WORK_TIME = 25 * 60;
static const int DEFAULT_SHORT_BREAK = 5 * 60;
static const int DEFAULT_LONG_BREAK = 15 * 60;
static const int DEFAULT_CYCLES_BEFORE_LONG = 4;
static const int DEFAULT_DAILY_GOAL = 8;

// Durations are edited in whole minutes, so keep the steppers inside a sane
// band instead of letting a stray long press run away.
static const int MIN_WORK_MINUTES = 5;
static const int MAX_WORK_MINUTES = 90;
static const int MIN_SHORT_MINUTES = 1;
static const int MAX_SHORT_MINUTES = 30;
static const int MIN_LONG_MINUTES = 5;
static const int MAX_LONG_MINUTES = 60;
static const int MIN_CYCLES = 2;
static const int MAX_CYCLES = 8;
static const int MIN_DAILY_GOAL = 1;
static const int MAX_DAILY_GOAL = 16;

#define MAX_POMO_TEMPLATES 6
#define MAX_POMO_TASKS 8
#define MAX_TASK_BLOCKS 12  // upper limit for a task's block estimate
#define POMO_HISTORY_DAYS 90
#define POMO_NAME_LEN 22
static const char* const EARBUD_NAME = "soundcore R50i NC";
