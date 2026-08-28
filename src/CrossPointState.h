#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  enum class VanNhanSoUpdateResult : uint8_t {
    NEVER = 0,
    IN_PROGRESS = 1,
    SUCCESS = 2,
    FAILED = 3,
    CANCELLED = 4,
  };
  enum class VanNhanSoUpdateError : uint8_t {
    NONE = 0,
    NO_WIFI = 1,
    WIFI_TIMEOUT = 2,
    DOWNLOAD = 3,
    CHECKSUM_MISSING = 4,
    CHECKSUM_MISMATCH = 5,
    INVALID_IMAGE = 6,
    INSTALL = 7,
    METADATA = 8,
    CONNECT = 9,
    HTTP_RATE_LIMIT = 10,
    HTTP_SERVER = 11,
    INCOMPLETE = 12,
  };

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentSleepPos = 0;
  uint8_t recentSleepFill = 0;
  uint16_t recentOverlaySleepImages[SLEEP_RECENT_COUNT] = {};
  uint8_t recentOverlaySleepPos = 0;
  uint8_t recentOverlaySleepFill = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  VanNhanSoUpdateResult vanNhanSoUpdateResult = VanNhanSoUpdateResult::NEVER;
  VanNhanSoUpdateError vanNhanSoUpdateError = VanNhanSoUpdateError::NONE;
  uint32_t vanNhanSoLastAttemptDate = 0;
  uint32_t vanNhanSoLastSuccessDate = 0;
  uint16_t vanNhanSoLastAttemptMinute = UINT16_MAX;
  uint16_t vanNhanSoLastSuccessMinute = UINT16_MAX;
  uint8_t vanNhanSoConsecutiveFailures = 0;
  uint16_t vanNhanSoLastHttpStatus = 0;
  uint32_t vanNhanSoPendingProfileHash = 0;
  uint32_t vanNhanSoFailureProfileHash = 0;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;
  bool isRecentOverlaySleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  void pushRecentOverlaySleep(uint16_t idx);
};

#define APP_STATE CrossPointState::getInstance()
