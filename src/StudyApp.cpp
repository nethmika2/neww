#include "StudyApp.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "Globals.h"
#include "DisplayUtils.h"

// Needed by the back tile / home shortcut.
void drawHomeScreen();

// ==========================================
// STORAGE BUDGET
// ==========================================
// One subject is held in RAM while it is open.  The pool is a fixed block in
// BSS (no heap churn, no fragmentation) and the line index points into it, so
// opening a note costs one sequential SD read and nothing else.  Keeping it at
// 16 KB leaves the Bluetooth stack its 120 KB of free heap when the audio app
// starts; that is roughly 330 wrapped lines, about twelve screens of notes, and
// anything longer is cut with a visible notice rather than half drawn.
static const int ST_BUF_BYTES = 16384;
static const int ST_MAX_LINES = 400;
static const int ST_MAX_TOPICS = 96;
static const int ST_MAX_CARDS = 128;
static const int ST_MAX_SUBJECTS = 12;
static const int ST_NAME_LEN = 26;
static const int ST_PATH_LEN = 44;
static const char* ST_DIR = "/study";

// ==========================================
// LAYOUT (8 px grid, same chrome as the other apps)
// ==========================================
static const int ST_PAGE_TOP = 40;
static const int ST_PAGE_BOTTOM = 204;
static const int ST_PAGE_H = ST_PAGE_BOTTOM - ST_PAGE_TOP;
static const int ST_SCROLL_X = 262;
static const int ST_SCROLL_W = 50;
static const int ST_SCROLL_H = 16;
static const int ST_SCROLL_UP_Y = 205;
static const int ST_SCROLL_DOWN_Y = 222;
static const int ST_SUBJECT_ROW_H = 40;
static const int ST_TOPIC_ROW_H = 24;
static const int ST_CARD_X = 8;
static const int ST_CARD_Y = 46;
static const int ST_CARD_W = 304;
static const int ST_CARD_H = 122;
static const int ST_CARD_SCROLL_X = 248;

// Reader metrics: the body is the built-in 6x8 font, headings and questions use
// FreeSansBold9pt7b so a topic change is obvious at a glance.
static const int ST_WRAP = 49;        // characters per body line
static const int ST_H_BODY = 10;
static const int ST_H_HEAD = 17;
static const int ST_H_QUESTION = 15;
static const int ST_H_BLANK = 7;

enum : uint8_t {
  ST_BODY = 0,
  ST_HEADING = 1,
  ST_BULLET = 2,
  ST_QUESTION = 3,
  ST_ANSWER = 4,
  ST_BLANK = 5
};

// ==========================================
// STATE
// ==========================================
struct StudySubject {
  char name[ST_NAME_LEN];
  char path[ST_PATH_LEN];
  uint16_t topics;
  uint16_t cards;
};

static StudySubject stSubjects[ST_MAX_SUBJECTS];
static int stSubjectCount = 0;

static char stPool[ST_BUF_BYTES];
static uint16_t stLineOff[ST_MAX_LINES + 1];
static uint8_t stLineStyle[ST_MAX_LINES];
static int stLineCount = 0;
static int stPoolUsed = 0;
static bool stTruncated = false;

static uint16_t stTopicLine[ST_MAX_TOPICS];
static int stTopicCount = 0;
static uint16_t stCardFront[ST_MAX_CARDS];
static uint16_t stCardBack[ST_MAX_CARDS];
static int stCardCount = 0;

static StudyView stView = STUDY_VIEW_SUBJECTS;
static int stSubject = -1;
static int stTopic = 0;
static int stScrollY = 0;
static int stContentH = 0;
static int stListScroll = 0;
static int stListScrollMax = 0;
static bool stLoaded = false;
static bool stTitleFromFile = false;
static char stTitle[ST_NAME_LEN] = "";

static uint8_t stDeck[ST_MAX_CARDS];
static int stDeckCount = 0;
static int stDeckPos = 0;
static bool stFlipped = false;
static int stCardScroll = 0;
static int stCardScrollMax = 0;

// ==========================================
// SMALL HELPERS
// ==========================================

// A subject's display name is derived from the file name: no folder, no .txt,
// underscores become spaces.
static void studyNameFromPath(const char* path, char* out, int outLen) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  int n = 0;
  while (base[n] && base[n] != '.' && n < outLen - 1) {
    out[n] = (base[n] == '_') ? ' ' : base[n];
    n++;
  }
  out[n] = 0;
  if (n == 0 && outLen > 1) {
    out[0] = '?';
    out[1] = 0;
  }
}

static bool studyIsNoteFile(const char* path) {
  size_t len = strlen(path);
  if (len < 5) return false;
  const char* ext = path + len - 4;
  if (ext[0] != '.') return false;
  return (tolower(ext[1]) == 't' && tolower(ext[2]) == 'x' && tolower(ext[3]) == 't');
}

static bool studyStartsWith(const char* s, const char* prefix) {
  while (*prefix) {
    if (*s++ != *prefix++) return false;
  }
  return true;
}

// ==========================================
// FILE SCANNING (no RAM cost: one line at a time)
// ==========================================
// Shared by the subject list (counts) and by the loader, so both agree on what
// counts as a topic or a card.
static void studyScanStream(File& f, int* topics, int* cards, char* title, int titleLen) {
  char line[192];
  int n = 0;
  if (topics) *topics = 0;
  if (cards) *cards = 0;
  bool titleSet = false;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\n') {
      line[n] = 0;
      if (n > 0 && line[n - 1] == '\r') line[--n] = 0;
      if (n > 1 && line[0] == '#') {
        if (line[1] == ' ') {
          if (!titleSet && title && titleLen > 1) {
            strncpy(title, line + 2, titleLen - 1);
            title[titleLen - 1] = 0;
            titleSet = true;
          }
        } else if (line[1] == '#') {
          if (topics) (*topics)++;
          if (cards) (*cards)++;
        }
      } else if (studyStartsWith(line, "Q: ")) {
        if (cards) (*cards)++;
      }
      n = 0;
    } else if (n < (int)sizeof(line) - 1) {
      line[n++] = (char)c;
    }
  }
}

bool studyScanFile(const char* path, int* topics, int* cards) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  studyScanStream(f, topics, cards, nullptr, 0);
  f.close();
  return true;
}

// ==========================================
// SUBJECT LIST
// ==========================================
int studySubjectCount() { return stSubjectCount; }

void studyRefreshSubjects() {
  stSubjectCount = 0;
  // The card is mounted at boot; if it was missing then, trying again here means
  // a card pushed in later works after a RELOAD instead of a reboot.
  if (!sdReady) {
    SPI.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    sdReady = SD.begin(SD_CS, SPI, SD_SPI_HZ);
    if (!sdReady) sdReady = SD.begin(SD_CS, SPI, 4000000);
    if (!sdReady) return;
  }

  File dir = SD.open(ST_DIR);
  if (!dir || !dir.isDirectory()) return;

  File entry = dir.openNextFile();
  while (entry && stSubjectCount < ST_MAX_SUBJECTS) {
    if (!entry.isDirectory()) {
      const char* path = entry.name();
      if (path && studyIsNoteFile(path)) {
        StudySubject& s = stSubjects[stSubjectCount];
        memset(&s, 0, sizeof(s));
        strncpy(s.path, path, ST_PATH_LEN - 1);
        s.path[ST_PATH_LEN - 1] = 0;
        studyNameFromPath(s.path, s.name, ST_NAME_LEN);
        stSubjectCount++;
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  // Alphabetical order keeps the list stable between visits even if the card
  // hands the files back in a different order.
  for (int i = 1; i < stSubjectCount; i++) {
    StudySubject key = stSubjects[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(stSubjects[j].name, key.name) > 0) {
      stSubjects[j + 1] = stSubjects[j];
      j--;
    }
    stSubjects[j + 1] = key;
  }

  // Counts and the optional "# Title" line come from one pass over each file.
  for (int i = 0; i < stSubjectCount; i++) {
    int topics = 0, cards = 0;
    char title[ST_NAME_LEN];
    title[0] = 0;
    File f = SD.open(stSubjects[i].path, FILE_READ);
    if (f) {
      studyScanStream(f, &topics, &cards, title, ST_NAME_LEN);
      f.close();
    }
    stSubjects[i].topics = (uint16_t)topics;
    stSubjects[i].cards = (uint16_t)cards;
    if (title[0]) {
      strncpy(stSubjects[i].name, title, ST_NAME_LEN - 1);
      stSubjects[i].name[ST_NAME_LEN - 1] = 0;
    }
  }
}

// ==========================================
// LOADING ONE SUBJECT
// ==========================================
static void studyResetDocument() {
  stLineCount = 0;
  stPoolUsed = 0;
  stTopicCount = 0;
  stCardCount = 0;
  stTruncated = false;
  stTitleFromFile = false;
  stTitle[0] = 0;
  stScrollY = 0;
  stListScroll = 0;
  stCardScroll = 0;
}

static void studyAddLine(const char* text, int len, uint8_t style) {
  if (stLineCount >= ST_MAX_LINES) {
    stTruncated = true;
    return;
  }
  if (stPoolUsed + len + 1 > ST_BUF_BYTES) {
    stTruncated = true;
    return;
  }
  stLineOff[stLineCount] = (uint16_t)stPoolUsed;
  memcpy(stPool + stPoolUsed, text, len);
  stPoolUsed += len;
  stPool[stPoolUsed++] = 0;
  stLineStyle[stLineCount] = style;
  stLineCount++;
}

// Word wrap one source line into the pool.  `indent` is the number of leading
// spaces continuation lines get, `firstIndent` the same for the first line
// (bullets hang under their marker).  Indentation that is already in the file
// is kept, so pre-wrapped notes converted from HTML line up as authored.
static void studyWrapLine(const char* text, uint8_t style, int indent, int firstIndent) {
  int lead = 0;
  while (text[lead] == ' ') lead++;
  int len = (int)strlen(text);
  while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t')) len--;
  if (len == 0 || lead >= len) {
    studyAddLine("", 0, ST_BLANK);
    return;
  }

  int lineNo = 0;
  int pos = 0;
  while (pos < len) {
    // Where the text starts on this line: still inside the file's own
    // indentation (copied verbatim), the first line of a bullet, or a wrapped
    // continuation that hangs under the text.
    int spaces;
    if (pos < lead) spaces = 0;
    else if (lineNo == 0) spaces = firstIndent;
    else spaces = indent + lead;
    int width = ST_WRAP - spaces;
    if (width < 8) width = 8;
    int take = len - pos;
    if (take > width) {
      // Break at the last space that fits, or mid word when there is none.
      int cut = -1;
      for (int i = width; i > 0; i--) {
        if (text[pos + i] == ' ') {
          cut = i;
          break;
        }
      }
      take = (cut > 0) ? cut : width;
    }
    char buf[ST_WRAP + 1];
    int write = 0;
    for (int i = 0; i < spaces && write < ST_WRAP; i++) buf[write++] = ' ';
    for (int i = 0; i < take && write < ST_WRAP; i++) buf[write++] = text[pos + i];
    buf[write] = 0;
    studyAddLine(buf, write, lineNo ? (uint8_t)ST_BODY : style);
    pos += take;
    while (pos < len && text[pos] == ' ') pos++;
    lineNo++;
    if (stLineCount >= ST_MAX_LINES || stPoolUsed + ST_WRAP + 1 > ST_BUF_BYTES) {
      stTruncated = true;
      return;
    }
  }
}

static void studyCaptureTitle(const char* text) {
  if (stTitleFromFile) return;
  int n = 0;
  while (text[n] && n < ST_NAME_LEN - 1) {
    stTitle[n] = text[n];
    n++;
  }
  stTitle[n] = 0;
  stTitleFromFile = true;
}

static void studyHandleRawLine(char* line) {
  int len = (int)strlen(line);
  while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) line[--len] = 0;

  if (len == 0) {
    studyAddLine("", 0, ST_BLANK);
    return;
  }
  if (studyStartsWith(line, "# ")) {
    studyCaptureTitle(line + 2);
    return;
  }
  if (studyStartsWith(line, "## ")) {
    if (stTopicCount < ST_MAX_TOPICS) stTopicLine[stTopicCount++] = (uint16_t)stLineCount;
    if (stCardCount < ST_MAX_CARDS) {
      // A heading needs a line to point at; add it first, then keep the index.
      char head[ST_WRAP + 1];
      int n = 0;
      const char* src = line + 3;
      while (src[n] && n < ST_WRAP) {
        head[n] = src[n];
        n++;
      }
      head[n] = 0;
      stCardFront[stCardCount] = (uint16_t)stLineCount;
      stCardBack[stCardCount] = (uint16_t)(stLineCount + 1);
      stCardCount++;
      studyAddLine(head, n, ST_HEADING);
    } else {
      studyWrapLine(line + 3, ST_HEADING, 0, 0);
    }
    return;
  }
  if (studyStartsWith(line, "Q: ")) {
    if (stCardCount < ST_MAX_CARDS) {
      char q[ST_WRAP + 1];
      int n = 0;
      const char* src = line + 3;
      while (src[n] && n < ST_WRAP - 2) {
        q[n] = src[n];
        n++;
      }
      q[n] = 0;
      stCardFront[stCardCount] = (uint16_t)stLineCount;
      stCardBack[stCardCount] = (uint16_t)(stLineCount + 1);  // the A: line follows
      stCardCount++;
      studyAddLine(q, n, ST_QUESTION);
    } else {
      studyWrapLine(line + 3, ST_QUESTION, 0, 0);
    }
    return;
  }
  if (studyStartsWith(line, "A: ")) {
    studyWrapLine(line + 3, ST_ANSWER, 2, 0);
    return;
  }
  if (studyStartsWith(line, "- ") || studyStartsWith(line, "* ")) {
    studyWrapLine(line + 2, ST_BULLET, 2, 2);
    return;
  }
  studyWrapLine(line, ST_BODY, 0, 0);
}

static bool studyLoadSubject(int index) {
  studyResetDocument();
  stLoaded = false;
  if (index < 0 || index >= stSubjectCount) return false;

  File f = SD.open(stSubjects[index].path, FILE_READ);
  if (!f) return false;

  char line[256];
  int n = 0;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\n') {
      line[n] = 0;
      studyHandleRawLine(line);
      n = 0;
      if (stLineCount >= ST_MAX_LINES) {
        stTruncated = true;
        break;
      }
    } else if (n < (int)sizeof(line) - 1) {
      line[n++] = (char)c;
    } else {
      // An over-long source line is folded rather than dropped.
      line[n] = 0;
      studyHandleRawLine(line);
      n = 0;
    }
  }
  if (n > 0) {
    line[n] = 0;
    studyHandleRawLine(line);
  }
  f.close();

  stLineOff[stLineCount] = (uint16_t)stPoolUsed;
  stLoaded = stLineCount > 0;
  return stLoaded;
}

// Falls back to the file name when the note carries no "# Title" line.
static const char* studyDisplayName() {
  if (stTitleFromFile && stTitle[0]) return stTitle;
  if (stSubject >= 0 && stSubject < stSubjectCount) return stSubjects[stSubject].name;
  return "NOTES";
}

// ==========================================
// LINE METRICS
// ==========================================
static int studyLineHeight(uint8_t style) {
  if (style == ST_HEADING) return ST_H_HEAD;
  if (style == ST_QUESTION) return ST_H_QUESTION;
  if (style == ST_BLANK) return ST_H_BLANK;
  return ST_H_BODY;
}

static int studyTotalHeight() {
  int h = 0;
  for (int i = 0; i < stLineCount; i++) h += studyLineHeight(stLineStyle[i]);
  return h + 6;
}

// Screen Y of a line relative to the top of the document.
static int studyLineTop(int line) {
  int y = 0;
  for (int i = 0; i < line && i < stLineCount; i++) y += studyLineHeight(stLineStyle[i]);
  return y;
}

static void studyClampScroll() {
  int maxScroll = max(0, stContentH - ST_PAGE_H);
  if (stScrollY < 0) stScrollY = 0;
  if (stScrollY > maxScroll) stScrollY = maxScroll;
}

// ==========================================
// DRAWING: SHARED CHROME
// ==========================================
static void studyDrawScrollChrome(bool enabled) {
  tft.fillRect(ST_SCROLL_X, 204, 58, 36, BG_COLOR);
  if (!enabled) return;
  drawModernButton(ST_SCROLL_X, ST_SCROLL_UP_Y, ST_SCROLL_W, ST_SCROLL_H, 4, SURFACE_HI, false);
  drawChevron(ST_SCROLL_X + 25, ST_SCROLL_UP_Y + 8, true, TEXT_COLOR);
  drawModernButton(ST_SCROLL_X, ST_SCROLL_DOWN_Y, ST_SCROLL_W, ST_SCROLL_H, 4, SURFACE_HI, false);
  drawChevron(ST_SCROLL_X + 25, ST_SCROLL_DOWN_Y + 8, false, TEXT_COLOR);
}

static void studyDrawHeader(const char* title, bool back) {
  drawScreenHeader(title, back);
}

// ==========================================
// SCREEN: SUBJECTS
// ==========================================
static void drawStudySubjects() {
  tft.fillScreen(BG_COLOR);
  studyDrawHeader("STUDY", true);

  // Both empty states keep the RELOAD action: it also retries the card mount.
  if (!sdReady || stSubjectCount == 0) {
    drawPanel(10, 52, 300, 104, nullptr);
    if (!sdReady) {
      printCentered("No SD card", 160, 84, &FreeSansBold9pt7b, DEL_COLOR);
      printCentered("Notes live in the /study folder", 160, 106, &FreeSans9pt7b, MUTED_COLOR);
      printCentered("of the card - push it in, then RELOAD", 160, 126, &FreeSans9pt7b, MUTED_COLOR);
    } else {
      printCentered("No notes found", 160, 84, &FreeSansBold9pt7b, TEXT_COLOR);
      printCentered("Put .txt notes in the /study folder", 160, 106, &FreeSans9pt7b, MUTED_COLOR);
      printCentered("on the SD card, then tap RELOAD", 160, 126, &FreeSans9pt7b, MUTED_COLOR);
    }
    drawModernButton(8, 208, 104, 26, RADIUS_SM, SURFACE_HI, false);
    printCentered("RELOAD", 60, 225, &FreeSans9pt7b, TEXT_COLOR);
    return;
  }

  int contentH = stSubjectCount * ST_SUBJECT_ROW_H;
  stListScrollMax = max(0, contentH - ST_PAGE_H);
  if (stListScroll > stListScrollMax) stListScroll = stListScrollMax;

  tft.startWrite();
  for (int i = 0; i < stSubjectCount; i++) {
    int y = ST_PAGE_TOP - stListScroll + (i * ST_SUBJECT_ROW_H);
    if (y + 34 < ST_PAGE_TOP || y > ST_PAGE_BOTTOM) continue;
    drawCard(8, y, 304, 34, i == stSubject, RADIUS_SM);
    tft.setFont(&FreeSansBold9pt7b);
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(16, y + 16);
    tft.print(stSubjects[i].name);
    tft.setFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(16, y + 24);
    tft.print(String(stSubjects[i].topics) + " topics   " + String(stSubjects[i].cards) + " cards");
    // Right hand action: jump straight to the flashcards of this subject.
    drawModernButton(252, y + 5, 54, 24, RADIUS_SM, SURFACE_HI, false);
    printCentered("CARDS", 279, y + 18, NULL, ACCENT_COLOR);
  }
  tft.endWrite();

  // Footer: reload action, scroll control and the subject count.
  tft.fillRect(0, ST_PAGE_BOTTOM, 320, 240 - ST_PAGE_BOTTOM, BG_COLOR);
  drawModernButton(8, 208, 104, 26, RADIUS_SM, SURFACE_HI, false);
  printCentered("RELOAD", 60, 225, &FreeSans9pt7b, TEXT_COLOR);
  printRight(String(stSubjectCount) + (stSubjectCount == 1 ? " subject" : " subjects"), 254, 225, NULL, MUTED_COLOR);
  studyDrawScrollChrome(stListScrollMax > 0);
}

// ==========================================
// SCREEN: TOPICS (table of contents)
// ==========================================
static void drawStudyTopics() {
  tft.fillScreen(BG_COLOR);
  studyDrawHeader(studyDisplayName(), true);

  if (stTopicCount == 0) {
    printCentered("This note has no headings", 160, 96, &FreeSans9pt7b, TEXT_COLOR);
    printCentered("Mark topics with ## in the .txt file", 160, 118, &FreeSans9pt7b, MUTED_COLOR);
    return;
  }

  int contentH = stTopicCount * ST_TOPIC_ROW_H;
  stListScrollMax = max(0, contentH - ST_PAGE_H);
  if (stListScroll > stListScrollMax) stListScroll = stListScrollMax;

  tft.startWrite();
  for (int i = 0; i < stTopicCount; i++) {
    int y = ST_PAGE_TOP - stListScroll + (i * ST_TOPIC_ROW_H);
    if (y + 22 < ST_PAGE_TOP || y > ST_PAGE_BOTTOM) continue;
    const char* text = stPool + stLineOff[stTopicLine[i]];
    drawCard(8, y, 304, 22, i == stTopic, RADIUS_SM);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(i == stTopic ? ACCENT_COLOR : TEXT_COLOR);
    tft.setCursor(16, y + 15);
    String label = text;
    if (label.length() > 30) label = label.substring(0, 29) + ".";
    tft.print(label);
    tft.setFont(NULL);
  }
  tft.endWrite();

  tft.fillRect(0, ST_PAGE_BOTTOM, 320, 240 - ST_PAGE_BOTTOM, BG_COLOR);
  printCentered("Tap a topic to read it", 120, 225, &FreeSans9pt7b, MUTED_COLOR);
  studyDrawScrollChrome(stListScrollMax > 0);
}

// ==========================================
// SCREEN: READER
// ==========================================
static void drawStudyReader() {
  tft.fillScreen(BG_COLOR);
  studyDrawHeader(studyDisplayName(), true);

  stContentH = studyTotalHeight();
  studyClampScroll();

  int y = ST_PAGE_TOP - stScrollY;
  int topTopic = -1;
  tft.startWrite();
  for (int i = 0; i < stLineCount; i++) {
    uint8_t style = stLineStyle[i];
    int h = studyLineHeight(style);
    int top = y;
    // The footer names the topic the top of the page belongs to.
    if (style == ST_HEADING && top <= ST_PAGE_TOP + 4) topTopic = i;
    if (top + h > ST_PAGE_TOP && top + h <= ST_PAGE_BOTTOM) {
      const char* text = stPool + stLineOff[i];
      if (style == ST_HEADING || style == ST_QUESTION) {
        tft.setFont(&FreeSansBold9pt7b);
        tft.setTextColor(style == ST_HEADING ? ACCENT_COLOR : TEXT_COLOR);
        tft.setCursor(10, top + 12);
        tft.print(text);
        tft.setFont(NULL);
      } else if (style != ST_BLANK) {
        tft.setTextSize(1);
        tft.setTextColor(style == ST_BULLET ? TEXT_COLOR : TEXT_COLOR);
        tft.setCursor(10, top + 1);
        tft.print(text);
        if (style == ST_BULLET) tft.fillRect(12, top + 3, 3, 3, ACCENT_COLOR);
      }
    }
    y += h;
    if (y >= ST_PAGE_BOTTOM) break;
  }
  tft.endWrite();

  if (stTruncated) {
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(DEL_COLOR);
    tft.setCursor(10, ST_PAGE_BOTTOM - 4);
    tft.print("(truncated - split this note)");
    tft.setFont(NULL);
  }

  // Footer: current topic, read position, progress bar and the scroll control.
  tft.fillRect(0, ST_PAGE_BOTTOM, 320, 240 - ST_PAGE_BOTTOM, BG_COLOR);
  String topicName = (topTopic >= 0) ? String(stPool + stLineOff[topTopic]) : String("Start of note");
  if (topicName.length() > 28) topicName = topicName.substring(0, 27) + ".";
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(8, 205);
  tft.print(topicName);
  int maxScroll = max(1, stContentH - ST_PAGE_H);
  int pct = (int)((float)stScrollY * 100.0f / (float)maxScroll);
  pct = constrain(pct, 0, 100);
  printRight(String(pct) + "%", 254, 205, NULL, MUTED_COLOR);
  drawProgressBar(8, 219, 246, 6, (float)stScrollY / (float)maxScroll, ACCENT_COLOR);
  studyDrawScrollChrome(true);
}

// ==========================================
// SCREEN: FLASHCARDS
// ==========================================
// The front of a card is the single line that carries it: the topic heading or
// the Q: line.  Everything up to the next card is the answer.
static const char* studyCardText(int card, int* lenOut) {
  const char* text = stPool + stLineOff[stCardFront[card]];
  if (lenOut) *lenOut = (int)strlen(text);
  return text;
}

static void studyCardBounds(int card, int* bodyStart, int* bodyEnd) {
  *bodyStart = stCardBack[card];
  *bodyEnd = (card + 1 < stCardCount) ? stCardFront[card + 1] : stLineCount;
}

static int studyCardAnswerHeight(int card) {
  int start, end;
  studyCardBounds(card, &start, &end);
  int h = 0;
  for (int i = start; i < end; i++) h += studyLineHeight(stLineStyle[i]);
  return h;
}

// The question is centred on the card face; long ones wrap onto a second line.
static void studyDrawQuestion() {
  int card = stDeckCount ? stDeck[stDeckPos] : 0;
  int len = 0;
  const char* text = studyCardText(card, &len);
  String q = String(text);
  String l1 = q, l2 = "";
  if (q.length() > 26) {
    int cut = q.lastIndexOf(' ', 26);
    if (cut < 8) cut = 26;
    l1 = q.substring(0, cut);
    l2 = q.substring(cut + (q[cut] == ' ' ? 1 : 0));
    if (l2.length() > 30) l2 = l2.substring(0, 29) + ".";
  }
  int y = ST_CARD_Y + (ST_CARD_H / 2) - (l2.length() ? 14 : 4);
  printCentered(l1, 160, y, &FreeSansBold9pt7b, TEXT_COLOR);
  if (l2.length()) printCentered(l2, 160, y + 18, &FreeSansBold9pt7b, TEXT_COLOR);
  printCentered("tap SHOW ANSWER", 160, ST_CARD_Y + ST_CARD_H - 14, &FreeSans9pt7b, MUTED_COLOR);
}

static void studyDrawAnswer() {
  int card = stDeckCount ? stDeck[stDeckPos] : 0;
  int start, end;
  studyCardBounds(card, &start, &end);
  int contentH = studyCardAnswerHeight(card);
  stCardScrollMax = max(0, contentH - (ST_CARD_H - 16));
  if (stCardScroll > stCardScrollMax) stCardScroll = stCardScrollMax;

  tft.startWrite();
  int y = ST_CARD_Y + 4 - stCardScroll;
  for (int i = start; i < end; i++) {
    uint8_t style = stLineStyle[i];
    int h = studyLineHeight(style);
    if (y + h > ST_CARD_Y && y + h <= ST_CARD_Y + ST_CARD_H) {
      if (style != ST_BLANK) {
        tft.setTextSize(1);
        tft.setTextColor(style == ST_BULLET ? TEXT_COLOR : TEXT_COLOR);
        tft.setCursor(14, y + 1);
        tft.print(stPool + stLineOff[i]);
        if (style == ST_BULLET) tft.fillRect(14, y + 3, 3, 3, PLOT_COLOR);
      }
    }
    y += h;
    if (y > ST_CARD_Y + ST_CARD_H) break;
  }
  tft.endWrite();
}

static void drawStudyCards() {
  tft.fillScreen(BG_COLOR);
  studyDrawHeader(studyDisplayName(), true);

  if (stCardCount == 0) {
    printCentered("No flashcards in this note", 160, 96, &FreeSans9pt7b, TEXT_COLOR);
    printCentered("Add ## headings or Q:/A: pairs", 160, 118, &FreeSans9pt7b, MUTED_COLOR);
    return;
  }

  // Counter on its own quiet line above the card face.
  tft.setTextSize(1);
  tft.setTextColor(MUTED_COLOR);
  tft.setCursor(8, 38);
  tft.print("CARD " + String(stDeckPos + 1) + " / " + String(stDeckCount) + (stFlipped ? "  -  ANSWER" : "  -  QUESTION"));
  drawCard(ST_CARD_X, ST_CARD_Y, ST_CARD_W, ST_CARD_H, stFlipped, RADIUS_MD);
  if (!stFlipped) studyDrawQuestion();
  else studyDrawAnswer();

  // The flip control is the primary action; a long answer gets its own scroll
  // control in the right hand column, so the deck buttons are never covered.
  bool scrollable = stFlipped && stCardScrollMax > 0;
  int flipW = scrollable ? 232 : 304;
  drawModernButton(8, 170, flipW, 30, RADIUS_MD, stFlipped ? SURFACE_HI : PLOT_COLOR, true);
  printCentered(stFlipped ? "SHOW QUESTION" : "SHOW ANSWER", 8 + (flipW / 2), 190, &FreeSansBold9pt7b, TEXT_COLOR);
  if (scrollable) {
    tft.fillRect(ST_CARD_SCROLL_X, 168, 64, 32, BG_COLOR);
    drawModernButton(ST_CARD_SCROLL_X, 168, 64, 15, RADIUS_SM, SURFACE_HI, false);
    printCentered("UP", ST_CARD_SCROLL_X + 32, 179, NULL, TEXT_COLOR);
    drawModernButton(ST_CARD_SCROLL_X, 185, 64, 15, RADIUS_SM, SURFACE_HI, false);
    printCentered("DOWN", ST_CARD_SCROLL_X + 32, 196, NULL, TEXT_COLOR);
  }
  drawModernButton(8, 206, 96, 28, RADIUS_MD, SURFACE_HI, false);
  printCentered("SHUFFLE", 56, 224, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(112, 206, 96, 28, RADIUS_MD, SURFACE_HI, false);
  printCentered("PREV", 160, 224, &FreeSans9pt7b, TEXT_COLOR);
  drawModernButton(216, 206, 96, 28, RADIUS_MD, ACCENT_COLOR, false);
  printCentered("NEXT", 264, 224, &FreeSansBold9pt7b, stFlipped ? BG_COLOR : TEXT_COLOR);
}

// ==========================================
// SCREEN DISPATCH
// ==========================================
void drawStudyScreen(bool fullWipe) {
  if (!fullWipe) return;  // everything here is redrawn wholesale on change
  // A reader or drill without a loaded note falls back to the list, which is
  // also the state a wake from the screensaver redraws into.
  if (!stLoaded && (stView == STUDY_VIEW_READER || stView == STUDY_VIEW_CARDS)) stView = STUDY_VIEW_SUBJECTS;
  if (stView == STUDY_VIEW_SUBJECTS) drawStudySubjects();
  else if (stView == STUDY_VIEW_TOPICS) drawStudyTopics();
  else if (stView == STUDY_VIEW_READER) drawStudyReader();
  else drawStudyCards();
}

// ==========================================
// NAVIGATION
// ==========================================
// Opening a subject's cards walks them in note order, so the drill follows the
// revision sheet; SHUFFLE is the explicit way to break the order up.
static void studyBuildDeck(bool shuffle) {
  stDeckCount = 0;
  for (int i = 0; i < stCardCount && i < ST_MAX_CARDS; i++) stDeck[stDeckCount++] = (uint8_t)i;
  if (shuffle && stDeckCount > 1) {
    for (int i = stDeckCount - 1; i > 0; i--) {
      int j = (int)(random(i + 1));
      uint8_t t = stDeck[i];
      stDeck[i] = stDeck[j];
      stDeck[j] = t;
    }
  }
  stDeckPos = 0;
  stFlipped = false;
  stCardScroll = 0;
}

static void studyOpenSubject(int index, StudyView target) {
  stSubject = index;
  if (!studyLoadSubject(index)) {
    showToast("Could not read note");
    stView = STUDY_VIEW_SUBJECTS;
    drawStudyScreen(true);
    return;
  }
  stTopic = 0;
  stListScroll = 0;
  if (target == STUDY_VIEW_CARDS) {
    studyBuildDeck(false);
    stView = STUDY_VIEW_CARDS;
  } else {
    stView = STUDY_VIEW_TOPICS;
  }
  drawStudyScreen(true);
}

static void studyBackToHome() {
  studyRelease();
  currentState = STATE_HOME;
  drawHomeScreen();
}

// Leaving the app drops the note and returns to the subject list, so re-entering
// (or a wake from the screensaver) can never show a half-loaded document.
void studyRelease() {
  stLoaded = false;
  stSubject = -1;
  stView = STUDY_VIEW_SUBJECTS;
  stCardScrollMax = 0;
  stListScrollMax = 0;
  stDeckCount = 0;
  stDeckPos = 0;
  studyResetDocument();
}

// ==========================================
// TOUCH
// ==========================================
static bool studyScrollButtons(int sx, int sy) {
  if (inRect(sx, sy, ST_SCROLL_X, ST_SCROLL_UP_Y, ST_SCROLL_W, ST_SCROLL_H)) {
    flashButton(ST_SCROLL_X, ST_SCROLL_UP_Y, ST_SCROLL_W, ST_SCROLL_H, 4);
    if (stView == STUDY_VIEW_CARDS) stCardScroll = max(0, stCardScroll - (ST_CARD_H / 2));
    else if (stView == STUDY_VIEW_READER) stScrollY = max(0, stScrollY - (ST_PAGE_H / 2));
    else stListScroll = max(0, stListScroll - (ST_PAGE_H / 2));
    drawStudyScreen(true);
    return true;
  }
  if (inRect(sx, sy, ST_SCROLL_X, ST_SCROLL_DOWN_Y, ST_SCROLL_W, ST_SCROLL_H)) {
    flashButton(ST_SCROLL_X, ST_SCROLL_DOWN_Y, ST_SCROLL_W, ST_SCROLL_H, 4);
    if (stView == STUDY_VIEW_CARDS) stCardScroll = min(stCardScrollMax, stCardScroll + (ST_CARD_H / 2));
    else if (stView == STUDY_VIEW_READER) stScrollY = min(max(0, stContentH - ST_PAGE_H), stScrollY + (ST_PAGE_H / 2));
    else stListScroll = min(stListScrollMax, stListScroll + (ST_PAGE_H / 2));
    drawStudyScreen(true);
    return true;
  }
  return false;
}

static void handleStudySubjectsTouch(int sx, int sy) {
  if (inRect(sx, sy, 8, 208, 104, 26)) {
    flashButton(8, 208, 104, 26, RADIUS_SM);
    showToast("Reading the card...");
    studyRefreshSubjects();
    drawStudyScreen(true);
    return;
  }
  if (stSubjectCount == 0) return;                 // nothing else to tap
  for (int i = 0; i < stSubjectCount; i++) {
    int y = ST_PAGE_TOP - stListScroll + (i * ST_SUBJECT_ROW_H);
    if (y + 34 < ST_PAGE_TOP || y > ST_PAGE_BOTTOM) continue;
    if (!inRect(sx, sy, 8, y, 304, 34)) continue;
    if (inRect(sx, sy, 252, y + 5, 54, 24)) {
      flashButton(252, y + 5, 54, 24, RADIUS_SM);
      studyOpenSubject(i, STUDY_VIEW_CARDS);
    } else {
      flashButton(8, y, 304, 34, RADIUS_SM);
      studyOpenSubject(i, STUDY_VIEW_TOPICS);
    }
    return;
  }
  if (studyScrollButtons(sx, sy)) return;
}

static void handleStudyTopicsTouch(int sx, int sy) {
  if (studyScrollButtons(sx, sy)) return;
  for (int i = 0; i < stTopicCount; i++) {
    int y = ST_PAGE_TOP - stListScroll + (i * ST_TOPIC_ROW_H);
    if (y + 22 < ST_PAGE_TOP || y > ST_PAGE_BOTTOM) continue;
    if (!inRect(sx, sy, 8, y, 304, 22)) continue;
    flashButton(8, y, 304, 22, RADIUS_SM);
    stTopic = i;
    stScrollY = studyLineTop(stTopicLine[i]);
    stContentH = studyTotalHeight();
    studyClampScroll();
    stView = STUDY_VIEW_READER;
    drawStudyScreen(true);
    return;
  }
}

static void handleStudyReaderTouch(int sx, int sy) {
  if (studyScrollButtons(sx, sy)) return;
  stView = STUDY_VIEW_TOPICS;
  drawStudyScreen(true);
}

static void studyMoveCard(int delta) {
  if (stDeckCount == 0) return;
  stDeckPos += delta;
  if (stDeckPos < 0) stDeckPos = stDeckCount - 1;
  if (stDeckPos >= stDeckCount) stDeckPos = 0;
  stFlipped = false;
  stCardScroll = 0;
}

static void handleStudyCardsTouch(int sx, int sy) {
  if (stFlipped && stCardScrollMax > 0) {
    if (inRect(sx, sy, ST_CARD_SCROLL_X, 168, 64, 15)) {
      flashButton(ST_CARD_SCROLL_X, 168, 64, 15, RADIUS_SM);
      stCardScroll = max(0, stCardScroll - (ST_CARD_H / 2));
      drawStudyScreen(true);
      return;
    }
    if (inRect(sx, sy, ST_CARD_SCROLL_X, 185, 64, 15)) {
      flashButton(ST_CARD_SCROLL_X, 185, 64, 15, RADIUS_SM);
      stCardScroll = min(stCardScrollMax, stCardScroll + (ST_CARD_H / 2));
      drawStudyScreen(true);
      return;
    }
  }
  if (inRect(sx, sy, 8, 170, stFlipped && stCardScrollMax > 0 ? 232 : 304, 30)) {
    flashButton(8, 170, stFlipped && stCardScrollMax > 0 ? 232 : 304, 30, RADIUS_MD);
    stFlipped = !stFlipped;
    stCardScroll = 0;
    drawStudyScreen(true);
    return;
  }
  if (inRect(sx, sy, 8, 206, 96, 28)) {
    flashButton(8, 206, 96, 28, RADIUS_MD);
    studyBuildDeck(true);
    drawStudyScreen(true);
    return;
  }
  if (inRect(sx, sy, 112, 206, 96, 28)) {
    flashButton(112, 206, 96, 28, RADIUS_MD);
    studyMoveCard(-1);
    drawStudyScreen(true);
    return;
  }
  if (inRect(sx, sy, 216, 206, 96, 28)) {
    flashButton(216, 206, 96, 28, RADIUS_MD);
    studyMoveCard(1);
    drawStudyScreen(true);
    return;
  }
}

void handleStudyTouch(bool touched, int sx, int sy) {
  if (!touched) return;
  waitTouchRelease();

  // Back tile: reader and flashcards step back to the topic list, the lists go
  // back to Home, so a wrong tap never costs more than one step.
  if (inRect(sx, sy, 0, 0, 44, 34)) {
    flashButton(6, 5, 34, 24, RADIUS_SM);
    if (stView == STUDY_VIEW_READER) {
      stView = STUDY_VIEW_TOPICS;
      stListScroll = 0;
      drawStudyScreen(true);
    } else if (stView == STUDY_VIEW_CARDS) {
      stView = STUDY_VIEW_TOPICS;
      drawStudyScreen(true);
    } else if (stView == STUDY_VIEW_TOPICS) {
      stView = STUDY_VIEW_SUBJECTS;
      studyRelease();
      drawStudyScreen(true);
    } else {
      studyBackToHome();
    }
    return;
  }

  if (stView == STUDY_VIEW_SUBJECTS) handleStudySubjectsTouch(sx, sy);
  else if (stView == STUDY_VIEW_TOPICS) handleStudyTopicsTouch(sx, sy);
  else if (stView == STUDY_VIEW_READER) handleStudyReaderTouch(sx, sy);
  else handleStudyCardsTouch(sx, sy);
}
