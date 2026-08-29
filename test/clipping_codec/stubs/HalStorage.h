#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

class HalFile {
 public:
  HalFile() = default;
  ~HalFile() { close(); }
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  bool open(const char* path, const char* mode) {
    close();
    file_ = std::fopen(path, mode);
    return file_ != nullptr;
  }

  size_t fileSize() {
    if (!file_) return 0;
    const long position = std::ftell(file_);
    if (position < 0 || std::fseek(file_, 0, SEEK_END) != 0) return 0;
    const long end = std::ftell(file_);
    std::fseek(file_, position, SEEK_SET);
    return end < 0 ? 0 : static_cast<size_t>(end);
  }

  int read(void* buffer, const size_t count) {
    return file_ ? static_cast<int>(std::fread(buffer, 1, count, file_)) : -1;
  }

  size_t write(const void* buffer, const size_t count) { return file_ ? std::fwrite(buffer, 1, count, file_) : 0; }

  void flush() {
    if (file_) std::fflush(file_);
  }

  bool close() {
    if (!file_) return false;
    const bool ok = std::fclose(file_) == 0;
    file_ = nullptr;
    return ok;
  }

 private:
  std::FILE* file_ = nullptr;
};

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char* path) const { return std::filesystem::exists(path); }
  bool mkdir(const char* path, bool = true) {
    std::error_code error;
    return std::filesystem::exists(path) || std::filesystem::create_directories(path, error);
  }
  bool remove(const char* path) {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    return removed && !error;
  }
  bool rename(const char* source, const char* destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "rb"); }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) { return file.open(path.c_str(), "wb"); }
};

#define Storage HalStorage::getInstance()
