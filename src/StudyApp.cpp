#include "StudyApp.h"
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "Globals.h"
#include "DisplayUtils.h"
#include "Storage.h"

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
static const int ST_BUF_BYTES = 16384;      // preferred pool, not a reserved block
static const int ST_MAX_LINES = 400;
static const int ST_MAX_TOPICS = 96;
static const int ST_MAX_CARDS = 128;
static const int ST_MAX_SUBJECTS = 12;
static const int ST_NAME_LEN = 26;
static const int ST_PATH_LEN = 44;
static const char* ST_DIR = "/study";

// Hard limits so the app can never sit on the SD card forever: a card that
// answers slowly, or a folder with a huge number of entries, costs a bounded
// amount of time and then the screen is drawn with what was found.  yield()
// inside both walks keeps the idle task fed, so the task watchdog cannot fire
// while a long read is in progress.
static const int ST_MAX_DIR_ENTRIES = 64;
static const unsigned long ST_DIR_BUDGET_MS = 1500;
// The budget only starts applying after a handful of entries: a normal /study
// folder is small, and those notes must always get their real titles.
static const int ST_DIR_BUDGET_GRACE = 8;
// Heap left free when the note pool is taken, so the apps that need big blocks
// (the Bluetooth stack) can still start after a visit to Study.
static const int ST_HEAP_RESERVE = 24000;
static const unsigned long ST_LOAD_BUDGET_MS = 2500;

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
  uint16_t sizeKB;      // from the directory entry: no file read needed
  uint16_t topics;
  uint16_t cards;
  bool counted;         // true once the note has been opened (or was cached)
};

static StudySubject stSubjects[ST_MAX_SUBJECTS];
static int stSubjectCount = 0;
static bool stFromCardRoot = false;   // notes came from the root, not /study
static bool stMountRetried = false;   // /study mount retried once per session

// Topic and card counts are only known after a note has been parsed, so the
// list remembers them for the rest of the session instead of re-reading every
// file each time the screen is opened.
struct StudyCount {
  char path[ST_PATH_LEN];
  uint16_t topics;
  uint16_t cards;
};
static StudyCount stCountCache[ST_MAX_SUBJECTS];
static int stCountCacheUsed = 0;

// The note text itself is the only large buffer in the app, and it is taken
// from the heap when a note is opened and handed back when the app is left.
// Keeping it out of BSS matters: 16 KB of DRAM is what the Bluetooth stack
// wants when the music app starts, and Study must not hold it for the rest of
// the session.  A card that cannot give the full block still works, just with
// shorter notes - the pool is tried at 16, 8 and 4 KB.
static char* stPool = nullptr;
static int stPoolCap = 0;
static uint16_t stLineOff[ST_MAX_LINES + 1];
static uint8_t stLineStyle[ST_MAX_LINES];
static int stLineCount = 0;
static int stPoolUsed = 0;
static bool stTruncated = false;
static bool stNonAsciiDropped = false;

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
// CARD WORK GUARD
// ==========================================
// Everything the study app does with the card is bookended by a flag that
// lives in RTC memory: it survives a reset (which is exactly the case worth
// catching) and costs no flash writes.  A board that resets in the middle of a
// read leaves it set, and the next entry into the app says so and waits for a
// retry instead of repeating whatever upset it.
static RTC_NOINIT_ATTR uint32_t stCardGuard;
static const uint32_t ST_GUARD_MAGIC = 0x5301C0DEu;

static void studyCardWork(bool active) { stCardGuard = active ? ST_GUARD_MAGIC : 0; }
static bool studyCardWorkPending() { return stCardGuard == ST_GUARD_MAGIC; }

void studySetCardGuard(bool active) { studyCardWork(active); }
bool studyCardGuardPending() { return studyCardWorkPending(); }

// ==========================================
// SMALL HELPERS
// ==========================================

// The panel fonts cover ASCII only: a byte above 0x7F (Sinhala, or any other
// non-Latin script) would be drawn as garbage glyphs.  Text is filtered to
// ASCII here and the app says so on screen, pointing at tools/mkstudy.py, which
// transliterates Sinhala into Latin before the note reaches the card.
static int studyToAscii(const char* src, int len, char* dst, int dstLen, bool* dropped) {
  int n = 0;
  for (int i = 0; i < len && n < dstLen - 1; i++) {
    unsigned char c = (unsigned char)src[i];
    if (c >= 0x80) {
      if (dropped) *dropped = true;
      continue;
    }
    dst[n++] = (char)c;
  }
  dst[n] = 0;
  return n;
}

// A subject's display name comes from the file name: no folder, no .txt,
// underscores become spaces.
static void studyNameFromPath(const char* path, char* out, int outLen) {
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  int n = 0;
  while (base[n] && base[n] != '.' && n < outLen - 1) out[n++] = base[n];
  out[n] = 0;

  char clean[ST_NAME_LEN];
  studyToAscii(out, n, clean, ST_NAME_LEN, nullptr);
  n = 0;
  bool lastSpace = true;
  for (int i = 0; clean[i] && n < outLen - 1; i++) {
    char c = (clean[i] == '_' || clean[i] == '-' && i == 0) ? ' ' : clean[i];
    if (c == ' ') {
      if (lastSpace) continue;
      lastSpace = true;
    } else {
      lastSpace = false;
    }
    out[n++] = c;
  }
  out[n] = 0;
  if (n == 0) {
    strncpy(out, "Note", outLen - 1);
    out[outLen - 1] = 0;
  }
}

// Cards can hand back either "note.txt" or "/study/note.txt" from name(), so the
// folder is always rebuilt from the entry's own base name.
static void studyMakePath(const char* dir, const char* name, char* out, int outLen) {
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (strcmp(dir, "/") == 0) snprintf(out, outLen, "/%s", base);
  else snprintf(out, outLen, "%s/%s", dir, base);
  out[outLen - 1] = 0;
}

static bool studyIsNoteFile(const char* path) {
  size_t len = strlen(path);
  if (len < 5) return false;
  const char* ext = path + len - 4;
  if (ext[0] != '.') return false;
  return (tolower(ext[1]) == 't' && tolower(ext[2]) == 'x' && tolower(ext[3]) == 't');
}

// The "# Title" line carries the proper subject name, and reading a couple of
// hundred bytes for it is cheap even on a slow card, so the list shows real
// titles without parsing the whole note.
// Fills `out` with the note's "# Title" line when it has one.  A note without a
// title line keeps whatever name it already had: clearing it first is how a
// plain "hello world" note ended up as a nameless row.
static void studyReadTitle(const char* path, char* out, int outLen) {
  unsigned long t0 = millis();
  bool locked = sdLockBegin(250);
  File f = SD.open(path, FILE_READ);
  if (!f) {
    if (locked) sdLockEnd();
    Serial.printf("[W][study] cannot open %s (%lu ms, lock %d)\n", path, millis() - t0, (int)locked);
    return;
  }
  char buf[192];
  int n = (int)f.read((uint8_t*)buf, sizeof(buf) - 1);
  f.close();
  if (locked) sdLockEnd();
  Serial.printf("[I][study] title %s -> %d bytes in %lu ms\n", path, n, millis() - t0);
  if (n <= 0) return;
  buf[n] = 0;
  char* line = buf;
  while (*line) {
    char* eol = strchr(line, '\n');
    int len = eol ? (int)(eol - line) : (int)strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) len--;
    if (line[0] == '#' && len > 2 && line[1] != '#') {
      char clean[ST_NAME_LEN];
      studyToAscii(line + 2, len - 2, clean, ST_NAME_LEN, nullptr);
      if (clean[0]) {
        strncpy(out, clean, outLen - 1);
        out[outLen - 1] = 0;
      }
      return;                                    // a title line: nothing to add
    }
    if (!eol) return;
    line = eol + 1;
  }
}

// Directory handles: name() may or may not include the folder, so only the base
// name is trusted.  The walk is capped and reports what it hit on the serial
// port, which is how a card problem is told apart from a firmware one.
static int studyListDir(const char* dir) {
  Serial.printf("[I][study] list %s (heap %u)\n", dir, (unsigned)ESP.getFreeHeap());
  studyCardWork(true);
  bool locked = sdLockBegin(500);
  if (!locked) Serial.println("[W][study] card busy - listing anyway");
  File handle = SD.open(dir, FILE_READ);
  if (!handle || !handle.isDirectory()) {
    Serial.printf("[I][study] no folder %s\n", dir);
    if (locked) sdLockEnd();
    studyCardWork(false);
    return 0;
  }
  // Names only, with the folder handle owned for as short a time as possible.
  // The notes are opened for their titles in studyReadTitles(), once the list is
  // on screen and this handle is closed: the card then sees one operation at a
  // time, which is what some cards need to stay reliable.
  int examined = 0;
  unsigned long start = millis();
  File entry = handle.openNextFile();
  while (entry) {
    if (++examined > ST_MAX_DIR_ENTRIES ||
        (examined > ST_DIR_BUDGET_GRACE && millis() - start > ST_DIR_BUDGET_MS)) {
      Serial.printf("[W][study] listing %s truncated after %d entries\n", dir, examined - 1);
      break;
    }
    if (!entry.isDirectory() && stSubjectCount < ST_MAX_SUBJECTS) {
      const char* raw = entry.name();
      if (raw && studyIsNoteFile(raw)) {
        StudySubject& s = stSubjects[stSubjectCount];
        memset(&s, 0, sizeof(s));
        studyMakePath(dir, raw, s.path, ST_PATH_LEN);
        studyNameFromPath(raw, s.name, ST_NAME_LEN);
        uint32_t bytes = (uint32_t)entry.size();
        s.sizeKB = (uint16_t)min(9999UL, (unsigned long)(bytes / 1024UL) + (bytes % 1024 ? 1 : 0));
        stSubjectCount++;
      }
    }
    entry.close();
    yield();
    entry = handle.openNextFile();
  }
  handle.close();
  if (locked) sdLockEnd();
  studyCardWork(false);
  Serial.printf("[I][study] %d entries in %s listed in %lu ms\n", examined, dir, millis() - start);
  return stSubjectCount;
}

// One bounded 192-byte read per note for its "# Title" line, plus the counts a
// note collected earlier in the session.  Returns true when a name changed, so
// the caller knows whether the list needs repainting.
static bool studyReadTitles() {
  bool changed = false;
  for (int i = 0; i < stSubjectCount; i++) {
    StudySubject& s = stSubjects[i];
    char title[ST_NAME_LEN];
    title[0] = 0;
    studyReadTitle(s.path, title, ST_NAME_LEN);
    for (int c = 0; c < stCountCacheUsed; c++) {
      if (strcmp(stCountCache[c].path, s.path) == 0) {
        s.topics = stCountCache[c].topics;
        s.cards = stCountCache[c].cards;
        s.counted = true;
        break;
      }
    }
    if (title[0] && strcmp(title, s.name) != 0) {
      strncpy(s.name, title, ST_NAME_LEN - 1);
      s.name[ST_NAME_LEN - 1] = 0;
      changed = true;
    }
    yield();
  }
  return changed;
}

// Counts for a note that was just parsed are remembered for the list.
static void studyRememberCount(const char* path, int topics, int cards) {
  for (int c = 0; c < stCountCacheUsed; c++) {
    if (strcmp(stCountCache[c].path, path) == 0) {
      stCountCache[c].topics = (uint16_t)topics;
      stCountCache[c].cards = (uint16_t)cards;
      return;
    }
  }
  if (stCountCacheUsed >= ST_MAX_SUBJECTS) return;
  strncpy(stCountCache[stCountCacheUsed].path, path, ST_PATH_LEN - 1);
  stCountCache[stCountCacheUsed].path[ST_PATH_LEN - 1] = 0;
  stCountCache[stCountCacheUsed].topics = (uint16_t)topics;
  stCountCache[stCountCacheUsed].cards = (uint16_t)cards;
  stCountCacheUsed++;
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
  if (!f) {
    Serial.printf("[W][study] cannot open %s\n", path);
    return false;
  }
  studyScanStream(f, topics, cards, nullptr, 0);
  f.close();
  return true;
}

// ==========================================
// SUBJECT LIST
// ==========================================
int studySubjectCount() { return stSubjectCount; }

// Lists the notes folder (falling back to the card root) and keeps the entries,
// without opening a single note.  Returns false when the card is not answering.
static bool studyBuildList() {
  stSubjectCount = 0;
  // The card is mounted at boot; retrying once here means a card pushed in
  // later works after a RELOAD instead of a reboot.  The bus itself is left as
  // the boot code configured it.
  if (!sdReady && !stMountRetried) {
    stMountRetried = true;
    sdReady = SD.begin(SD_CS, SPI, SD_SPI_HZ) || SD.begin(SD_CS, SPI, 4000000);
    Serial.printf("[I][study] remount -> %s\n", sdReady ? "ok" : "failed");
  }
  if (!sdReady) {
    Serial.println("[I][study] no card");
    return false;
  }

  // Listing a folder only stats the entries: no note is read here, so opening
  // the app is immediate however long the notes are.
  studyListDir(ST_DIR);
  stFromCardRoot = false;
  if (stSubjectCount == 0) {
    // Notes dropped straight onto the card instead of into /study still work.
    studyListDir("/");
    stFromCardRoot = stSubjectCount > 0;
  }

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

  Serial.printf("[I][study] %d notes from %s\n", stSubjectCount, stFromCardRoot ? "card root" : ST_DIR);
  return true;
}

void studyRefreshSubjects() {
  if (!studyBuildList()) return;
  studyReadTitles();
}

// Entry and RELOAD: the list goes up from the folder entries first, so the
// screen is never empty while notes are being read.
static void studyListThenReadTitles() {
  if (!studyBuildList()) {
    drawStudyScreen(true);
    return;
  }
  drawStudyScreen(true);
  if (studyReadTitles()) drawStudyScreen(true);
}

// ==========================================
// LOADING ONE SUBJECT
// ==========================================
// Takes the text pool from the heap the first time a note is opened.  Smaller
// blocks are tried in turn, so a cramped heap costs note length, not the app.
static bool studyEnsurePool() {
  if (stPool) return true;
  const int steps[] = {ST_BUF_BYTES, ST_BUF_BYTES / 2, ST_BUF_BYTES / 4, ST_BUF_BYTES / 8};
  for (int i = 0; i < 4; i++) {
    // Leave the Bluetooth stack its room: a smaller pool costs note length and
    // says so on screen, while a starved heap stops the music app working.
    if (i < 3 && (int)ESP.getFreeHeap() < steps[i] + ST_HEAP_RESERVE) continue;
    stPool = (char*)malloc(steps[i]);
    if (stPool) {
      stPoolCap = steps[i];
      Serial.printf("[I][study] pool %d bytes (heap %u)\n", stPoolCap, (unsigned)ESP.getFreeHeap());
      return true;
    }
  }
  Serial.printf("[W][study] no pool available (heap %u)\n", (unsigned)ESP.getFreeHeap());
  return false;
}

int studyPoolBytes() { return stPoolCap; }

static void studyDropPool() {
  if (stPool) free(stPool);
  stPool = nullptr;
  stPoolCap = 0;
  stPoolUsed = 0;
}

static void studyResetDocument() {
  stLineCount = 0;
  stNonAsciiDropped = false;
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
  if (!stPool) {
    stTruncated = true;
    return;
  }
  if (stLineCount >= ST_MAX_LINES) {
    stTruncated = true;
    return;
  }
  // Only ASCII reaches the panel: the fonts have no other glyphs, so a byte
  // above 0x7F is dropped and reported instead of drawn as garbage.
  char clean[ST_WRAP + 1];
  if (len > ST_WRAP) len = ST_WRAP;               // never write past clean[]
  int n = studyToAscii(text, len, clean, (int)sizeof(clean), &stNonAsciiDropped);
  if (n == 0 && len > 0) {
    // The whole line was non-ASCII (a Sinhala-only heading, say): keep the gap
    // so the page still has a shape.
    if (style == ST_BLANK) return;
  }
  if (stPoolUsed + n + 1 > stPoolCap) {
    stTruncated = true;
    return;
  }
  stLineOff[stLineCount] = (uint16_t)stPoolUsed;
  if (n > 0) memcpy(stPool + stPoolUsed, clean, n);
  stPoolUsed += n;
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

  // Fold to ASCII first so the wrap width matches what will be drawn: a byte
  // count of UTF-8 Sinhala would be roughly three times the glyphs.
  char ascii[256];
  bool dropped = false;
  int asciiLen = studyToAscii(text, len, ascii, (int)sizeof(ascii), &dropped);
  if (dropped) stNonAsciiDropped = true;
  if (asciiLen == 0) {
    studyAddLine("", 0, ST_BLANK);
    return;
  }
  text = ascii;
  len = asciiLen;
  while (lead < len && text[lead] == ' ') lead++;

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
    if (stLineCount >= ST_MAX_LINES || stPoolUsed + ST_WRAP + 1 > stPoolCap) {
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
  if (!studyEnsurePool()) return false;

  Serial.printf("[I][study] open %s (heap %u)\n", stSubjects[index].path, (unsigned)ESP.getFreeHeap());
  studyCardWork(true);
  bool locked = sdLockBegin(500);
  File f = SD.open(stSubjects[index].path, FILE_READ);
  if (!f) {
    if (locked) sdLockEnd();
    studyCardWork(false);
    return false;
  }

  char line[256];
  int n = 0;
  long bytes = 0;
  unsigned long started = millis();
  // Read until the driver says there is nothing left.  The byte counter bounds
  // the work and yield() keeps the rest of the system (and the task watchdog)
  // alive while a long note is being pulled off a slow card.
  for (;;) {
    int c = f.read();
    if (c < 0) break;
    if ((++bytes & 1023) == 0) {
      yield();
      if (millis() - started > ST_LOAD_BUDGET_MS) {
        Serial.printf("[W][study] read budget hit after %ld bytes\n", bytes);
        stTruncated = true;
        break;
      }
    }
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
  if (locked) sdLockEnd();
  studyCardWork(false);

  stLineOff[stLineCount] = (uint16_t)stPoolUsed;
  stLoaded = stLineCount > 0;
  Serial.printf("[I][study] %s: %ld bytes -> %d lines, %d topics, %d cards%s\n",
                stSubjects[index].path, bytes, stLineCount, stTopicCount, stCardCount,
                stTruncated ? " (truncated)" : "");
  if (stSubject >= 0 && stSubject < stSubjectCount) {
    stSubjects[stSubject].topics = (uint16_t)stTopicCount;
    stSubjects[stSubject].cards = (uint16_t)stCardCount;
    stSubjects[stSubject].counted = true;
    studyRememberCount(stSubjects[stSubject].path, stTopicCount, stCardCount);
  }
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
  Serial.println("[I][study]   header up");

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
    char info[40];
    if (stSubjects[i].counted) {
      snprintf(info, sizeof(info), "%u topics   %u cards", (unsigned)stSubjects[i].topics, (unsigned)stSubjects[i].cards);
    } else {
      // Counting topics means reading the note, which is exactly the work to
      // avoid on the way in, so the list shows the size and opens instantly.
      snprintf(info, sizeof(info), "%u KB   tap to open", (unsigned)stSubjects[i].sizeKB);
    }
    tft.print(info);
    // Right hand action: jump straight to the flashcards of this subject.
    drawModernButton(252, y + 5, 54, 24, RADIUS_SM, SURFACE_HI, false);
    printCentered("CARDS", 279, y + 18, NULL, ACCENT_COLOR);
  }
  tft.endWrite();
  Serial.println("[I][study]   rows up");

  // Footer: reload action, scroll control and the subject count.
  tft.fillRect(0, ST_PAGE_BOTTOM, 320, 240 - ST_PAGE_BOTTOM, BG_COLOR);
  drawModernButton(8, 208, 104, 26, RADIUS_SM, SURFACE_HI, false);
  printCentered("RELOAD", 60, 225, &FreeSans9pt7b, TEXT_COLOR);
  char footer[24];
  if (stFromCardRoot) snprintf(footer, sizeof(footer), "from card root");
  else snprintf(footer, sizeof(footer), "%d subject%s", stSubjectCount, stSubjectCount == 1 ? "" : "s");
  printRight(footer, 254, 225, NULL, stFromCardRoot ? ACCENT_COLOR : MUTED_COLOR);
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
    char label[32];
    strncpy(label, text, sizeof(label) - 1);
    label[sizeof(label) - 1] = 0;
    if (strlen(label) > 30) { label[29] = '.'; label[30] = 0; }
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
  tft.setTextSize(1);
  if (stNonAsciiDropped) {
    // Actionable, not decoration: this note needs converting before it can be
    // read on the panel.
    tft.setTextColor(DEL_COLOR);
    tft.setCursor(8, 205);
    tft.print("non-ASCII text removed - see tools/mkstudy.py");
  } else {
    char topicName[30];
    const char* src = (topTopic >= 0) ? (stPool + stLineOff[topTopic]) : "Start of note";
    strncpy(topicName, src, sizeof(topicName) - 1);
    topicName[sizeof(topicName) - 1] = 0;
    if (strlen(topicName) > 28) { topicName[27] = '.'; topicName[28] = 0; }
    tft.setTextColor(MUTED_COLOR);
    tft.setCursor(8, 205);
    tft.print(topicName);
  }
  int maxScroll = max(1, stContentH - ST_PAGE_H);
  int pct = (int)((float)stScrollY * 100.0f / (float)maxScroll);
  pct = constrain(pct, 0, 100);
  char pctText[8];
  snprintf(pctText, sizeof(pctText), "%d%%", pct);
  printRight(pctText, 254, 205, NULL, MUTED_COLOR);
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
  char l1[28], l2[32];
  strncpy(l1, text, sizeof(l1) - 1);
  l1[sizeof(l1) - 1] = 0;
  l2[0] = 0;
  if (strlen(text) > 26) {
    int cut = 26;
    for (int i = 26; i > 8; i--) {
      if (text[i] == ' ') { cut = i; break; }
    }
    strncpy(l1, text, cut);
    l1[cut] = 0;
    const char* rest = text + cut;
    while (*rest == ' ') rest++;
    strncpy(l2, rest, sizeof(l2) - 1);
    l2[sizeof(l2) - 1] = 0;
    if (strlen(l2) > 30) { l2[29] = '.'; l2[30] = 0; }
  }
  int y = ST_CARD_Y + (ST_CARD_H / 2) - (l2[0] ? 14 : 4);
  printCentered(l1, 160, y, &FreeSansBold9pt7b, TEXT_COLOR);
  if (l2[0]) printCentered(l2, 160, y + 18, &FreeSansBold9pt7b, TEXT_COLOR);
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
  char counter[40];
  snprintf(counter, sizeof(counter), "CARD %d / %d  -  %s", stDeckPos + 1, stDeckCount,
           stFlipped ? "ANSWER" : "QUESTION");
  tft.print(counter);
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
  // One line per paint: if a board ever stops answering again, the serial log
  // names the step it stopped on and the next fix is a small one.
  unsigned long t0 = millis();
  Serial.printf("[I][study] draw view %d (%d subjects, heap %u)\n", (int)stView, stSubjectCount,
                (unsigned)ESP.getFreeHeap());
  // A reader or drill without a loaded note falls back to the list, which is
  // also the state a wake from the screensaver redraws into.
  if (!stLoaded && (stView == STUDY_VIEW_READER || stView == STUDY_VIEW_CARDS)) stView = STUDY_VIEW_SUBJECTS;
  if (stView == STUDY_VIEW_SUBJECTS) drawStudySubjects();
  else if (stView == STUDY_VIEW_TOPICS) drawStudyTopics();
  else if (stView == STUDY_VIEW_READER) drawStudyReader();
  else drawStudyCards();
  Serial.printf("[I][study] draw done in %lu ms\n", millis() - t0);
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
// Drawn immediately when the app opens, before the card is touched, so a slow
// or empty card shows the app responding instead of a blank screen.
// Shown when the board reset in the middle of a card operation, so the app does
// not repeat it silently.  The retry is explicit and the log says what happened.
static void drawStudyCardTrouble() {
  tft.fillScreen(BG_COLOR);
  drawScreenHeader("STUDY", true);
  drawPanel(10, 44, 300, 122, nullptr);
  printCentered("The card read stopped last time", 160, 70, &FreeSansBold9pt7b, DEL_COLOR);
  printCentered("Your notes are untouched - the app", 160, 96, &FreeSans9pt7b, MUTED_COLOR);
  printCentered("just did not finish reading the card.", 160, 112, &FreeSans9pt7b, MUTED_COLOR);
  printCentered("RETRY lists them again; the serial", 160, 136, &FreeSans9pt7b, MUTED_COLOR);
  printCentered("log says where it stopped.", 160, 152, &FreeSans9pt7b, MUTED_COLOR);
  drawModernButton(8, 208, 104, 26, RADIUS_SM, SURFACE_HI, false);
  printCentered("RETRY", 60, 225, &FreeSans9pt7b, TEXT_COLOR);
  printRight("card parked", 254, 225, NULL, MUTED_COLOR);
}

void studyEnterApp() {
  Serial.printf("[I][study] enter (heap %u)\n", (unsigned)ESP.getFreeHeap());
  currentState = STATE_STUDY;
  stView = STUDY_VIEW_SUBJECTS;
  tft.fillScreen(BG_COLOR);
  drawScreenHeader("STUDY", true);
  printCentered("Reading the card...", 160, 108, &FreeSans9pt7b, MUTED_COLOR);
  Serial.println("[I][study] splash up");
  // A finished read clears the flag; a set flag means the previous one never
  // finished, so say so and wait for a retry instead of repeating it.
  if (studyCardWorkPending()) {
    Serial.println("[W][study] previous card read did not finish - waiting for RETRY");
    studyDropPool();
    stSubjectCount = 0;                 // nothing has been listed this session
    stFromCardRoot = false;
    stLoaded = false;
    drawStudyCardTrouble();
    return;
  }
  studyListThenReadTitles();
}

void studyRelease() {
  stLoaded = false;
  stSubject = -1;
  stView = STUDY_VIEW_SUBJECTS;
  stCardScrollMax = 0;
  stListScrollMax = 0;
  stDeckCount = 0;
  stDeckPos = 0;
  stCountCacheUsed = 0;
  studyResetDocument();
  studyDropPool();          // the 16 KB goes back to the heap for the other apps
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
    tft.fillScreen(BG_COLOR);
    drawScreenHeader("STUDY", true);
    printCentered("Reading the card...", 160, 108, &FreeSans9pt7b, MUTED_COLOR);
    Serial.println("[I][study] splash up (reload)");
    studyListThenReadTitles();
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
