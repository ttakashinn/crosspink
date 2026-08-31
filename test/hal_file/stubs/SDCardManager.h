#pragma once

#include <FS.h>
#include <Print.h>
#include <common/FsApiConstants.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(const bool open) : open_(open) {}
  FsFile(FsFile&& other) noexcept : open_(std::exchange(other.open_, false)) {}
  FsFile& operator=(FsFile&& other) noexcept {
    open_ = std::exchange(other.open_, false);
    return *this;
  }
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;

  bool close() {
    open_ = false;
    return true;
  }
  void flush() {}
  size_t getName(char*, size_t) { return 0; }
  size_t size() const { return 0; }
  size_t fileSize() const { return 0; }
  bool seekSet(uint64_t) { return open_; }
  bool seekCur(int64_t) { return open_; }
  int available() const { return 0; }
  size_t position() const { return 0; }
  int read(void*, size_t) { return 0; }
  int read() { return -1; }
  size_t write(const uint8_t*, size_t count) { return open_ ? count : 0; }
  size_t write(const void*, size_t count) { return open_ ? count : 0; }
  size_t write(uint8_t) { return open_ ? 1 : 0; }
  bool rename(const char*) { return open_; }
  bool isDirectory() const { return false; }
  void rewindDirectory() {}
  FsFile openNextFile() { return FsFile(); }
  bool isOpen() const { return open_; }

 private:
  bool open_ = false;
};

class SDCardManager {
 public:
  static SDCardManager& getInstance() {
    static SDCardManager instance;
    return instance;
  }

  bool begin() { return true; }
  bool ready() const { return true; }
  uint64_t sdTotalBytes() const { return 32ULL * 1024ULL * 1024ULL; }
  uint64_t sdUsedBytes() { return 8ULL * 1024ULL * 1024ULL; }
  std::vector<String> listFiles(const char*, int) { return {}; }
  String readFile(const char*) { return {}; }
  bool readFileToStream(const char*, Print&, size_t) { return false; }
  size_t readFileToBuffer(const char*, char*, size_t, size_t) { return 0; }
  bool writeFile(const char*, const String&) { return true; }
  bool ensureDirectoryExists(const char*) { return true; }
  FsFile open(const char*, oflag_t) { return FsFile(true); }
  bool mkdir(const char*, bool) { return true; }
  bool exists(const char*) { return false; }
  bool remove(const char*) { return true; }
  bool rename(const char*, const char*) { return true; }
  bool rmdir(const char*) { return true; }
  bool openFileForRead(const char*, const char*, FsFile& file) {
    file = FsFile(true);
    return true;
  }
  bool openFileForWrite(const char*, const char*, FsFile& file) {
    file = FsFile(true);
    return true;
  }
  bool removeDir(const char*) { return true; }
};
