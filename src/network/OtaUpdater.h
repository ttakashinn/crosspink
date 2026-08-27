#pragma once

#include <atomic>
#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  std::string checksumUrl;
  size_t otaSize = 0;
  std::atomic<size_t> processedSize{0};
  std::atomic<size_t> totalSize{0};

 public:
  enum class InstallPhase : uint8_t { IDLE, DOWNLOADING, VERIFYING, FLASHING };

 private:
  std::atomic<InstallPhase> installPhase{InstallPhase::IDLE};

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
    CHECKSUM_ERROR,
    INVALID_IMAGE_ERROR,
    STORAGE_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize.load(); }

  size_t getTotalSize() const { return totalSize.load(); }

  InstallPhase getInstallPhase() const { return installPhase.load(); }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr);
};
