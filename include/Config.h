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

// On-board RGB LED (active LOW on this board) and the two PWM levels the idle
// modes use.  The LED is the "still powered" load when the screen is dark; the
// DIM level is a low backlight duty, which is the larger of the two loads
// without lighting the panel up.
#define LED_R_PIN 4
#define LED_G_PIN 16
#define LED_B_PIN 17
#define LED_ACTIVE_LOW 1
#define LED_PWM_FREQ 5000
#define LED_PWM_BITS 8
#define LED_PWM_MAX ((1 << LED_PWM_BITS) - 1)
#define LED_CH_R 0
#define LED_CH_G 1
#define LED_CH_B 2
#define TFT_BL_CH 3
#define TFT_BL_FULL_DUTY 255
#define TFT_BL_DIM_DUTY 70    // ~27%: keeps the pack loaded, panel stays almost black

// SD SPI
#define SD_MOSI 23
#define SD_MISO 19
#define SD_CLK 18
#define SD_CS 5
#define SD_SPI_HZ 10000000

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
// One coherent dark theme: a near black canvas, two raised surfaces, a single
// blue accent for "selected/active" and one green for "good/running".  Every
// screen draws from these names only, so the whole UI can be re-styled from
// this block.
#define BG_COLOR       0x1083  // #0D1017  canvas
#define SURFACE_COLOR  0x18C4  // #161A23  cards
#define SURFACE_HI     0x2146  // #1F2733  raised cards / pressed
#define SHADOW_COLOR   0x0841  // #05070C  button drop shadow
#define BTN_COLOR      0x2146  // #1F2733
#define BTN_OUTLINE    0x29A8  // #2C3644  hairline borders
#define TEXT_COLOR     0xE77E  // #EAF0F8  primary text
#define MUTED_COLOR    0x8CB4  // #8B95A8  secondary text
#define AXIS_COLOR     0x4AAD  // #46536B  graph axes
#define GRID_COLOR     0x1926  // #1B2331  graph grid
#define PLOT_COLOR     0x35F0  // #2FBF87  success / play
#define DEL_COLOR      0xE249  // #E5484D  destructive
#define FUNC_COLOR     0x7B7D  // #7C6CF0  function keys
#define VAR_COLOR      0x4C7F  // #4C8DFF  variables / accent
#define ACCENT_COLOR   0x4C7F  // #4C8DFF  selection
#define PRESS_COLOR    0x3A4B  // #3A4759  pressed feedback
#define POINT_COLOR    0xF524  // #F5A623  plotted points
// One colour per function slot, chosen to stay apart on the dark canvas.
#define F1_COLOR       0x4C7F  // #4C8DFF
#define F2_COLOR       0xFD64  // #FFB020
#define F3_COLOR       0xE249  // #E5484D
#define F4_COLOR       0x35F0  // #2FBF87
#define F5_COLOR       0x265A  // #22C9D8
#define F6_COLOR       0xB37F  // #B56CFF

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
