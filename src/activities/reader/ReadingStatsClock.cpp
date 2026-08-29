#include "ReadingStatsClock.h"

#include "CrossPointSettings.h"
#include "ReadingStats.h"

#if defined(SIMULATOR)
#include <HalClock.h>

#include <ctime>
#else
#include <HalClock.h>
#endif

namespace ReadingStatsClock {

bool currentLocalDateTime(ReadingStatsLocalDateTime& out) {
#if defined(SIMULATOR)
  const std::time_t epoch = std::time(nullptr);
  std::tm utc{};
  if (epoch <= 0 || gmtime_r(&epoch, &utc) == nullptr) {
    out = {};
    return false;
  }
  const int offsetMinutes = (static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48) * 15;
  out = ReadingStatsMath::localDateTime(static_cast<uint32_t>(utc.tm_year + 1900), static_cast<uint8_t>(utc.tm_mon + 1),
                                        static_cast<uint8_t>(utc.tm_mday), static_cast<uint8_t>(utc.tm_hour),
                                        static_cast<uint8_t>(utc.tm_min), static_cast<uint8_t>(utc.tm_sec),
                                        offsetMinutes);
#else
  Rtc::DateTime now{};
  const bool haveTime = halClock.isAvailable() ? halClock.getDateTime(now) : halClock.getSystemDateTime(now);
  if (!haveTime) {
    out = {};
    return false;
  }
  const int offsetMinutes = (static_cast<int>(SETTINGS.clockUtcOffsetQ) - 48) * 15;
  out = ReadingStatsMath::localDateTime(now.year, now.month, now.day, now.hour, now.minute, now.second, offsetMinutes);
#endif
  return out.isValid();
}

uint32_t currentLocalDateKey() {
  ReadingStatsLocalDateTime now;
  return currentLocalDateTime(now) ? now.dateKey : 0;
}

bool hasPersistentWallClock() { return halClock.isAvailable(); }

}  // namespace ReadingStatsClock
