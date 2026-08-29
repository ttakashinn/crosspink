#pragma once

#include <HalStorage.h>

#include <filesystem>
#include <functional>
#include <string>

class BookStub {
 public:
  BookStub(std::string path, std::string cacheRoot, std::string prefix)
      : path_(std::move(path)),
        cacheRoot_((std::filesystem::path(path_).parent_path() / ".crosspoint").string()),
        prefix_(std::move(prefix)) {
    (void)cacheRoot;
  }

  std::string getCachePath() const {
    return cacheRoot_ + "/" + prefix_ + "_" + std::to_string(std::hash<std::string>{}(path_));
  }

  void clearCache() const {
    std::error_code error;
    std::filesystem::remove_all(getCachePath(), error);
  }

 private:
  std::string path_;
  std::string cacheRoot_;
  std::string prefix_;
};
