#pragma once
#include <Arduino.h>

// ==========================================
// 1. HARDWARE PINS & LIMITS
// ==========================================
#define NUM_FUNCS 4
#define MAX_TRACKS 20
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
#define TRACKS_PER_PAGE 5

static const int WORK_TIME = 25 * 60;
static const int SHORT_BREAK_TIME = 5 * 60;
static const int LONG_BREAK_TIME = 15 * 60;
static const int POMOS_BEFORE_LONG = 4;
static const int PAN_THRESHOLD = 8;
static const char* const EARBUD_NAME = "soundcore R50i NC";
