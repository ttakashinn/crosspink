#pragma once

#include <filesystem>
#include <string>

class HalStorage {
 public:
  static HalStorage& getInstance() {
    static HalStorage storage;
    return storage;
  }

  bool exists(const char* path) const { return std::filesystem::exists(path); }

  bool rename(const char* source, const char* destination) {
    if (failedSource_ == source && failedDestination_ == destination) {
      clearFailure();
      return false;
    }
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    return !error;
  }

  void failNextRename(std::string source, std::string destination) {
    failedSource_ = std::move(source);
    failedDestination_ = std::move(destination);
  }

  void clearFailure() {
    failedSource_.clear();
    failedDestination_.clear();
  }

 private:
  std::string failedSource_;
  std::string failedDestination_;
};

#define Storage HalStorage::getInstance()
