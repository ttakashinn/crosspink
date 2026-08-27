#include "HalClock.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

HalClock halClock;  // Singleton instance

namespace {
constexpr time_t MIN_VALID_UNIX_TIME = 1704067200;  // 2024-01-01 00:00:00 UTC

bool dateTimeFromSystemClock(Rtc::DateTime& out) {
  const time_t now = time(nullptr);
  if (now < MIN_VALID_UNIX_TIME) return false;

  struct tm timeinfo = {};
  if (!gmtime_r(&now, &timeinfo)) return false;

  out.year = static_cast<uint16_t>(timeinfo.tm_year + 1900);
  out.month = static_cast<uint8_t>(timeinfo.tm_mon + 1);
  out.day = static_cast<uint8_t>(timeinfo.tm_mday);
  out.hour = static_cast<uint8_t>(timeinfo.tm_hour);
  out.minute = static_cast<uint8_t>(timeinfo.tm_min);
  out.second = static_cast<uint8_t>(timeinfo.tm_sec);
  out.weekday = static_cast<uint8_t>(timeinfo.tm_wday);
  return true;
}

time_t unixTimeFromUtc(const Rtc::DateTime& dateTime) {
  // Howard Hinnant's civil-date conversion, yielding days since 1970-01-01.
  // This avoids timegm(), which newlib on the ESP32-C3 does not expose.
  int year = dateTime.year;
  const unsigned month = dateTime.month;
  const unsigned day = dateTime.day;
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
  const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  const int64_t days = static_cast<int64_t>(era) * 146097 + dayOfEra - 719468;
  return static_cast<time_t>(days * 86400 + dateTime.hour * 3600 + dateTime.minute * 60 + dateTime.second);
}
}  // namespace

void HalClock::begin() {
  _available = _sdkRtc.begin();
  LOG_INF("CLK", _available ? "SDK RTC found" : "RTC not found");
}

bool HalClock::getTime(uint8_t& hour, uint8_t& minute) const {
  if (!_available) return false;

  const unsigned long now = millis();
  if (_lastPollMs != 0 && (now - _lastPollMs) < CLOCK_POLL_MS) {
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }

  Rtc::DateTime dt;
  if (!_sdkRtc.now(dt)) {
    if (!_hasCachedTime) return false;
    _lastPollMs = now;
    hour = _cachedHour;
    minute = _cachedMinute;
    return true;
  }
  _cachedHour = dt.hour;
  _cachedMinute = dt.minute;
  _lastPollMs = now;
  _hasCachedTime = true;
  hour = _cachedHour;
  minute = _cachedMinute;
  return true;
}

bool HalClock::getDateTime(Rtc::DateTime& out) const {
  if (!_available) return false;
  return _sdkRtc.now(out);
}

bool HalClock::getSystemDateTime(Rtc::DateTime& out) const { return dateTimeFromSystemClock(out); }

bool HalClock::formatTime(char* buf, size_t bufSize, uint8_t utcOffsetQuarterHoursBiased, bool use12Hour) const {
  if (bufSize < (use12Hour ? 9u : 6u)) return false;
  uint8_t h, m;
  if (!getTime(h, m)) return false;

  // Apply UTC offset: convert biased value to signed quarter-hours.
  // Clamp against corrupted persisted values so display time can't drift outside [-12:00, +14:00].
  if (utcOffsetQuarterHoursBiased > 104) utcOffsetQuarterHoursBiased = 104;
  int offsetQuarterHours = static_cast<int>(utcOffsetQuarterHoursBiased) - 48;
  int totalMinutes = static_cast<int>(h) * 60 + static_cast<int>(m) + offsetQuarterHours * 15;

  // Wrap around 24 hours
  totalMinutes = ((totalMinutes % 1440) + 1440) % 1440;

  const int hour24 = totalMinutes / 60;
  const int min = totalMinutes % 60;
  if (use12Hour) {
    const bool pm = hour24 >= 12;
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(buf, bufSize, "%d:%02d %s", hour12, min, pm ? "PM" : "AM");
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hour24, min);
  }
  return true;
}

bool HalClock::syncFromNTP() {
  if (!_available) return false;

  Rtc::DateTime dt;
  if (!syncSystemTimeFromNTP(dt)) return false;

  if (_sdkRtc.set(dt)) {
    _lastPollMs = 0;
    _cachedHour = dt.hour;
    _cachedMinute = dt.minute;
    _hasCachedTime = true;
    LOG_INF("CLK", "RTC set to %04u-%02u-%02u %02u:%02u:%02u UTC", dt.year, dt.month, dt.day, dt.hour, dt.minute,
            dt.second);
    return true;
  }
  return false;
}

bool HalClock::syncSystemTimeFromNTP(Rtc::DateTime& out) {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("CLK", "WiFi not connected, cannot sync NTP");
    return false;
  }

  LOG_INF("CLK", "Starting NTP sync...");
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov");

  // Wait for SNTP sync to complete (up to 5 seconds)
  constexpr int maxAttempts = 50;
  for (int i = 0; i < maxAttempts; i++) {
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      return dateTimeFromSystemClock(out);
    }
    delay(100);
  }

  LOG_ERR("CLK", "NTP sync timed out");
  return false;
}

bool HalClock::setDateTimeUtc(const Rtc::DateTime& dateTime) {
  const time_t epoch = unixTimeFromUtc(dateTime);
  if (epoch < MIN_VALID_UNIX_TIME) return false;

  // Round-trip through gmtime both to reject impossible fields (for example
  // 31 February) and to derive weekday rather than trusting callers to fill
  // that RTC-specific field.
  struct tm normalizedTm = {};
  if (!gmtime_r(&epoch, &normalizedTm) || normalizedTm.tm_year + 1900 != dateTime.year ||
      normalizedTm.tm_mon + 1 != dateTime.month || normalizedTm.tm_mday != dateTime.day ||
      normalizedTm.tm_hour != dateTime.hour || normalizedTm.tm_min != dateTime.minute ||
      normalizedTm.tm_sec != dateTime.second) {
    return false;
  }
  Rtc::DateTime normalized = dateTime;
  normalized.weekday = static_cast<uint8_t>(normalizedTm.tm_wday);

  const timeval now = {epoch, 0};
  if (settimeofday(&now, nullptr) != 0) return false;

  bool rtcOk = true;
  if (_available) rtcOk = _sdkRtc.set(normalized);
  _lastPollMs = 0;
  _cachedHour = normalized.hour;
  _cachedMinute = normalized.minute;
  _hasCachedTime = true;
  return rtcOk;
}
