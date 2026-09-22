#include "Globals.h"

// ==========================================
// HARDWARE INSTANCES
// ==========================================
SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft = Adafruit_ILI9341(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
Preferences prefs;
SoftTouch ts;
BluetoothA2DPSource a2dp_source;

// ==========================================
// SYSTEM & CALIBRATION STATE
// ==========================================
AppState currentState = STATE_HOME;
bool touch_swap_xy = false;
int touch_x_min = 200, touch_x_max = 3800, touch_y_min = 200, touch_y_max = 3800, calibStep = 0;
TS_Point calTL, calTR, calBR;

// ==========================================
// AUDIO SYSTEM STATE
// ==========================================
File audioFile;
WavInfo currentWav;
volatile bool isPlaying = false;
bool btConnected = false, lastBtState = false, btInitialized = false, audioSystemReady = false, sdReady = false;
String playlist[MAX_TRACKS];
int numTracks = 0, currentTrack = 0, currentVolume = 80, listPage = 0;
volatile int audioGainQ8 = 256;
volatile bool trackFinished = false, fileReadDone = false;
volatile uint32_t audioStreamPos = 0;
unsigned long lastUiUpdateTime = 0, lastMusicBtnPress = 0;

uint8_t* audioRingBuffer = nullptr;
volatile int ringHead = 0, ringTail = 0;
SemaphoreHandle_t audioMutex = NULL;
TaskHandle_t audioTaskHandle = NULL;

// ==========================================
// POMODORO TIMER STATE
// ==========================================
PomoMode pomoMode = MODE_WORK;
bool pomoRunning = false;
int pomodorosCompleted = 0, pomoSeconds = 25 * 60;
unsigned long lastPomoTick = 0;

// ==========================================
// POWER, SCREENSAVER & CLOCK STATE
// ==========================================
bool screenOn = true, screensaverEnabled = true, screensaverActive = false, timeSynced = false, autoSyncBoot = true;
unsigned long lastActivityTime = 0, lastSaverTick = 0;
int clockX = 160, clockY = 130, lastDrawnMinute = -1, homeLastMinute = -1;

// ==========================================
// MATH ENGINE & GRAPHER STATE
// ==========================================
double math_x = 0, math_y = 0, math_t = 0;
CustomVar sliders[NUM_CUSTOM_VARS] = {
  { "a", 1.0, -10, 10, false }, { "b", 1.0, -10, 10, false }, { "c", 1.0, -10, 10, false }, { "k", 1.0, -10, 10, false },
  { "m", 1.0, -10, 10, false }, { "n", 1.0, -10, 10, false }, { "p", 1.0, -10, 10, false }, { "q", 1.0, -10, 10, false }
};

te_variable vars[] = {
  { "x", &math_x }, { "y", &math_y }, { "t", &math_t },
  { "a", &sliders[0].value }, { "b", &sliders[1].value }, { "c", &sliders[2].value }, { "k", &sliders[3].value },
  { "m", &sliders[4].value }, { "n", &sliders[5].value }, { "p", &sliders[6].value }, { "q", &sliders[7].value }
};
const int NUM_TE_VARS = sizeof(vars) / sizeof(vars[0]);

FuncSlot funcs[NUM_FUNCS] = {
  { "y=x^2", EQ_EXPLICIT, nullptr, nullptr, true, F1_COLOR },
  { "x^2+y^2=25", EQ_IMPLICIT, nullptr, nullptr, false, F2_COLOR },
  { "y=m*x+c", EQ_EXPLICIT, nullptr, nullptr, false, F3_COLOR },
  { "(t, t^2)", EQ_PARAMETRIC, nullptr, nullptr, false, F4_COLOR },
  { "", EQ_EMPTY, nullptr, nullptr, false, F5_COLOR },
  { "", EQ_EMPTY, nullptr, nullptr, false, F6_COLOR }
};

PlotPoint points[MAX_POINTS];
int numPoints = 0;
String pointInput = "";
int pointCursor = 0, activeSlot = 0, cursor_idx = 5;
double centerWorldX = 0.0, centerWorldY = 0.0, zoom = 15.0;
int16_t prev_y[NUM_FUNCS][321];
int old_ax = -1000, old_ay = -1000, old_tsx = -1, old_tsy = -1, old_boxW = 0;
bool tabsVisible = true, xAxisPi = false, yAxisPi = false, needsFullWipe = false;
bool traceActive = false, touchActive = false, isPanning = false, isTracing = false;
int traceSlot = -1;
double traceWorldX = 0, traceWorldY = 0;
int touchStartX = 0, touchStartY = 0, lastTouchX = 0, lastTouchY = 0;
float smoothed_x = -1, smoothed_y = -1;
unsigned long lastPanTime = 0;

bool varPanelOpen = false;
int activeVarIdx = -1;

// Slider playback state.  animDirection flips at the min/max stops so the
// value ping-pongs instead of wrapping with a visible jump.
bool varAnimating = false;
int animDirection = 1;
unsigned long lastAnimTime = 0;
int animSpeedIdx = 1;

// ==========================================
// KEYBOARD MATRIX LAYOUTS
// ==========================================
const char* main_keys[5][6] = {
  { "x", "y", "t", "=", ",", "DEL" },
  { "7", "8", "9", "/", "sqrt()", "AC" },
  { "4", "5", "6", "*", "^2", "FUNC" },
  { "1", "2", "3", "-", "(", "VAR" },
  { "0", ".", "pi", "+", ")", "PLOT" }
};

const char* func_keys[5][6] = {
  { "sin()", "cos()", "tan()", "ln()", "log()", "BACK" },
  { "asin()", "acos()", "atan()", "abs()", "exp()", "DEL" },
  { "sec()", "cosec()", "cot()", "^", "!", "AC" },
  { "<-", "->", " ", " ", " ", " " },
  { " ", " ", " ", " ", " ", "PLOT" }
};

const char* var_keys[5][6] = {
  { "a", "b", "c", "k", " ", "BACK" },
  { "m", "n", "p", "q", " ", "DEL" },
  { "<-", "->", " ", " ", " ", "AC" },
  { " ", " ", " ", " ", " ", " " },
  { " ", " ", " ", " ", " ", "PLOT" }
};

const char* point_keys[5][6] = {
  { "7", "8", "9", ",", " ", "DEL" },
  { "4", "5", "6", "-", " ", "AC" },
  { "1", "2", "3", "/", " ", "BACK" },
  { "0", ".", "pi", "UNDO", "CLR", " " },
  { "(", ")", "<-", "->", " ", "ADD" }
};
