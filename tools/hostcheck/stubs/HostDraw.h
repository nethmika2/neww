#pragma once
// ---------------------------------------------------------------------------
// Draw call recorder.  Adafruit_GFX on the host writes every primitive into a
// text log, one call per line, which render.py turns back into a PNG.  That is
// how the screens can be reviewed without the panel attached.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include <cstdio>

struct HostDraw {
  static std::vector<std::string> &log() {
    static std::vector<std::string> lines;
    return lines;
  }
  static void reset() { log().clear(); }
  static void add(const std::string &line) { log().push_back(line); }
  static void dump(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (auto &l : log()) fputs((l + "\n").c_str(), f);
    fclose(f);
  }
};

inline std::string hostColor(uint16_t c) {
  int r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  char buf[16];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
  return buf;
}
