#pragma once
// Preferences stand-in: a process wide string/int map, so settings survive
// between the calls a test makes.
#include <Arduino.h>
#include <map>
#include <string>

class Preferences {
 public:
  bool begin(const char *name, bool readOnly = false) { (void)readOnly; ns_ = name ? name : "default"; open_ = true; return true; }
  void end() { open_ = false; }
  bool clear() { store().clear(); return true; }
  bool remove(const char *key) { return store().erase(keyOf(key)) > 0; }

  size_t putInt(const char *key, int32_t v) { store()[keyOf(key)] = std::to_string(v); return 4; }
  size_t putUInt(const char *key, uint32_t v) { store()[keyOf(key)] = std::to_string(v); return 4; }
  size_t putBool(const char *key, bool v) { store()[keyOf(key)] = v ? "1" : "0"; return 1; }
  size_t putULong(const char *key, uint32_t v) { store()[keyOf(key)] = std::to_string(v); return 4; }
  size_t putDouble(const char *key, double v) {
    char b[40];
    snprintf(b, sizeof(b), "%.17g", v);
    store()[keyOf(key)] = b;
    return 8;
  }
  double getDouble(const char *key, double def = 0) {
    auto it = store().find(keyOf(key));
    return it == store().end() ? def : atof(it->second.c_str());
  }
  uint32_t getULong(const char *key, uint32_t def = 0) { return (uint32_t)getLong(key, (long)def); }
  size_t putFloat(const char *key, float v) {
    char b[32];
    snprintf(b, sizeof(b), "%.9g", v);
    store()[keyOf(key)] = b;
    return 4;
  }
  size_t putString(const char *key, const String &v) { store()[keyOf(key)] = v.c_str(); return v.length(); }
  size_t putBytes(const char *key, const void *buf, size_t len) {
    store()[keyOf(key)] = std::string((const char *)buf, len);
    return len;
  }

  long getLong(const char *key, long def = 0) {
    auto it = store().find(keyOf(key));
    return it == store().end() ? def : atol(it->second.c_str());
  }
  int32_t getInt(const char *key, int32_t def = 0) { return (int32_t)getLong(key, def); }
  uint32_t getUInt(const char *key, uint32_t def = 0) { return (uint32_t)getLong(key, def); }
  bool getBool(const char *key, bool def = false) { return getLong(key, def ? 1 : 0) != 0; }
  float getFloat(const char *key, float def = 0) {
    auto it = store().find(keyOf(key));
    return it == store().end() ? def : (float)atof(it->second.c_str());
  }
  String getString(const char *key, const String &def = String()) {
    auto it = store().find(keyOf(key));
    return it == store().end() ? def : String(it->second.c_str());
  }
  size_t getBytes(const char *key, void *buf, size_t len) {
    auto it = store().find(keyOf(key));
    if (it == store().end()) return 0;
    size_t n = it->second.size() < len ? it->second.size() : len;
    memcpy(buf, it->second.data(), n);
    return n;
  }
  size_t getBytesLength(const char *key) {
    auto it = store().find(keyOf(key));
    return it == store().end() ? 0 : it->second.size();
  }
  bool isKey(const char *key) { return store().count(keyOf(key)) > 0; }

  // Harness helpers
  static std::map<std::string, std::string> &store() {
    static std::map<std::string, std::string> s;
    return s;
  }
  static void resetAll() { store().clear(); }

 private:
  std::string keyOf(const char *key) { return ns_ + "/" + (key ? key : ""); }
  std::string ns_ = "default";
  bool open_ = false;
};

extern Preferences prefs;
