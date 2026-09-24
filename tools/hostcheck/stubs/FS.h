#pragma once
// In-memory file system used by the host build.  Files are byte vectors, which
// is enough for the audio player tests (the WAV helper below writes real files
// into it) and for the storage code.
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <Arduino.h>

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
class File;

class FS {
 public:
  File open(const char *path, const char *mode = FILE_READ);
  File open(const String &path, const char *mode = FILE_READ);
  bool exists(const char *path);
  bool remove(const char *path);
  bool rename(const char *from, const char *to);
  bool mkdir(const char *path);
  std::vector<std::string> &keys() { return order_; }
  std::map<std::string, std::vector<uint8_t>> &files() { return files_; }
  size_t totalBytes() { return total_; }
  void reset() { files_.clear(); order_.clear(); total_ = 0; }

 private:
  std::map<std::string, std::vector<uint8_t>> files_;
  std::vector<std::string> order_;
  size_t total_ = 0;
  friend class File;
};

class File {
 public:
  File() : fs_(nullptr) {}
  File(FS *f, const std::string &p) : fs_(f), path_(p) {
    if (f && f->files_.count(p)) { buf_ = std::make_shared<std::vector<uint8_t>>(f->files_[p]); }
    else buf_ = nullptr;
  }
  operator bool() const { return fs_ != nullptr && buf_ != nullptr; }
  bool operator==(bool b) const { return (bool)*this == b; }
  size_t size() const { return buf_ ? buf_->size() : 0; }
  size_t position() const { return pos_; }
  int available() const { return (int)(size() - pos_); }
  const char *name() const { return path_.c_str(); }
  bool isDirectory() { return false; }
  File openNextFile(const char * = FILE_READ) { return File(); }
  void close() {}
  int read() { return pos_ < size() ? (*buf_)[pos_++] : -1; }
  int peek() { return pos_ < size() ? (*buf_)[pos_] : -1; }
  size_t read(uint8_t *dst, size_t len) {
    if (!buf_) return 0;
    size_t n = std::min(len, size() - pos_);
    memcpy(dst, buf_->data() + pos_, n);
    pos_ += n;
    return n;
  }
  size_t readBytes(uint8_t *dst, size_t len) { return read(dst, len); }
  size_t readBytes(char *dst, size_t len) { return read((uint8_t *)dst, len); }
  size_t write(const uint8_t *src, size_t len) {
    if (!buf_) { buf_ = std::make_shared<std::vector<uint8_t>>(); }
    if (pos_ + len > buf_->size()) buf_->resize(pos_ + len);
    memcpy(buf_->data() + pos_, src, len);
    pos_ += len;
    commit();
    return len;
  }
  size_t write(const char *s) { return write((const uint8_t *)s, strlen(s)); }
  size_t print(const String &s) { return write(s.c_str()); }
  size_t println(const String &s) { return write(s.c_str()) + write((const uint8_t *)"\n", 1); }
  size_t printf(const char *fmt, ...) {
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    return write((const uint8_t *)b, (size_t)(n > 0 ? n : 0));
  }
  bool seek(size_t p) { pos_ = p; return true; }
  bool seekSet(size_t p) { return seek(p); }
  void flush() { commit(); }
  bool remove() { return fs_ ? fs_->remove(path_.c_str()) : false; }

 private:
  void commit() {
    if (fs_ && buf_) fs_->files_[path_] = *buf_;
  }
  FS *fs_;
  std::string path_;
  std::shared_ptr<std::vector<uint8_t>> buf_;
  size_t pos_ = 0;
};

inline File FS::open(const char *path, const char *mode) {
  (void)mode;
  if (!path) return File();
  if (!files_.count(path)) {
    if (strcmp(mode, FILE_READ) == 0) return File();
    files_[path] = std::vector<uint8_t>();
    total_ += 0;
    order_.push_back(path);
  }
  return File(this, path);
}
inline File FS::open(const String &path, const char *mode) { return open(path.c_str(), mode); }
inline bool FS::exists(const char *path) { return path && files_.count(path) > 0; }
inline bool FS::remove(const char *path) { return path && files_.erase(path) > 0; }
inline bool FS::rename(const char *from, const char *to) {
  if (!from || !to || !files_.count(from)) return false;
  files_[to] = files_[from];
  files_.erase(from);
  return true;
}
inline bool FS::mkdir(const char *) { return true; }
}  // namespace fs

using fs::File;
#ifdef FS_NO_GLOBALS
#else
#endif
