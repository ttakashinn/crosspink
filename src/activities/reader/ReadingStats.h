#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

inline constexpr size_t READING_TIME_BUCKET_COUNT = 4;
inline constexpr size_t READING_DAY_OF_WEEK_COUNT = 7;
inline constexpr size_t READING_HISTORY_DAYS = 730;
inline constexpr size_t READING_HISTORY_BYTES = (READING_HISTORY_DAYS + 7) / 8;

struct ReadingStatsLocalDateTime {
  uint32_t dateKey = 0;
  uint32_t secondOfDay = 0;

  bool isValid() const { return dateKey != 0 && secondOfDay < 24U * 60U * 60U; }
};

struct BookReadingStats {
  static constexpr uint16_t UNKNOWN_PROGRESS_PERMILLE = UINT16_MAX;

  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint16_t sessionCount = 0;
  bool isCompleted = false;
  // Kept outside generated EPUB caches so Home can paint without parsing the book.
  uint16_t progressPermille = UNKNOWN_PROGRESS_PERMILLE;
  // Calendar fields use YYYYMMDD in the configured local timezone; zero means unknown.
  uint32_t firstReadDateKey = 0;
  uint32_t lastReadDateKey = 0;
  uint16_t activeReadingDays = 0;
  uint16_t currentStreakDays = 0;
  // Pace only includes qualifying forward page turns, excluding rereads/backward navigation.
  uint16_t avgSecondsPerForwardPage = 0;
  uint16_t paceSampleCount = 0;
  uint32_t estimatedTimeLeftSeconds = 0;
  uint32_t completedDateKey = 0;
  bool firstReadDateManual = false;
  bool completedDateManual = false;
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};

  bool operator==(const BookReadingStats&) const = default;
};

struct GlobalReadingStats {
  uint32_t totalReadingSeconds = 0;
  uint32_t totalPagesTurned = 0;
  uint32_t sessionCount = 0;
  uint32_t completedBooks = 0;
  uint32_t firstReadDateKey = 0;
  uint32_t lastReadDateKey = 0;
  uint32_t activeReadingDays = 0;
  uint32_t currentStreakDays = 0;
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};
  // Bit 0 is the anchor day, bit 1 the previous day, and so on.
  uint32_t readingHistoryAnchorDay = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> readingHistoryBits{};
  uint16_t longestReadingStreak = 0;

  bool operator==(const GlobalReadingStats&) const = default;
};

namespace ReadingStatsMath {

template <typename T>
T saturatedAdd(const T value, const T increment) {
  return increment > std::numeric_limits<T>::max() - value ? std::numeric_limits<T>::max() : value + increment;
}

inline uint32_t averageSessionSeconds(const BookReadingStats& stats) {
  return stats.sessionCount == 0 ? 0 : stats.totalReadingSeconds / stats.sessionCount;
}

inline uint32_t averageSessionSeconds(const GlobalReadingStats& stats) {
  return stats.sessionCount == 0 ? 0 : stats.totalReadingSeconds / stats.sessionCount;
}

inline float pagesPerMinute(const BookReadingStats& stats) {
  return stats.totalReadingSeconds < 60
             ? 0.0f
             : static_cast<float>(stats.totalPagesTurned) * 60.0f / static_cast<float>(stats.totalReadingSeconds);
}

inline float pagesPerMinute(const GlobalReadingStats& stats) {
  return stats.totalReadingSeconds < 60
             ? 0.0f
             : static_cast<float>(stats.totalPagesTurned) * 60.0f / static_cast<float>(stats.totalReadingSeconds);
}

inline uint32_t paceEstimatedSecondsLeft(const BookReadingStats& stats, const uint32_t remainingPages) {
  if (stats.isCompleted || stats.avgSecondsPerForwardPage == 0 || remainingPages == 0) return 0;
  const uint64_t remaining = static_cast<uint64_t>(stats.avgSecondsPerForwardPage) * remainingPages;
  return static_cast<uint32_t>(std::min<uint64_t>(remaining, UINT32_MAX));
}

inline uint32_t estimatedSecondsLeft(const BookReadingStats& stats, const int progressPercent) {
  (void)progressPercent;
  return stats.isCompleted ? 0 : stats.estimatedTimeLeftSeconds;
}

inline bool hasConfidentTimeLeft(const BookReadingStats& stats, const int progressPercent) {
  // Five qualified forward-page dwell samples and five active minutes keep a
  // short browse/jump sequence from producing a noisy estimate in the reader.
  return !stats.isCompleted && progressPercent > 0 && progressPercent < 100 && stats.avgSecondsPerForwardPage > 0 &&
         stats.paceSampleCount >= 5 && stats.totalReadingSeconds >= 300 &&
         estimatedSecondsLeft(stats, progressPercent) > 0;
}

inline bool isLeapYear(const uint32_t year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

inline uint8_t daysInMonth(const uint32_t year, const uint8_t month) {
  constexpr uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeapYear(year) ? 29 : days[month - 1];
}

inline uint32_t makeDateKey(const uint32_t year, const uint8_t month, const uint8_t day) {
  if (year < 2000 || year > 2199 || day < 1 || day > daysInMonth(year, month)) return 0;
  return year * 10000U + static_cast<uint32_t>(month) * 100U + day;
}

inline bool splitDateKey(const uint32_t key, uint32_t& year, uint8_t& month, uint8_t& day) {
  if (key == 0) return false;
  year = key / 10000U;
  month = static_cast<uint8_t>((key / 100U) % 100U);
  day = static_cast<uint8_t>(key % 100U);
  return makeDateKey(year, month, day) == key;
}

inline uint32_t nextDateKey(const uint32_t key) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!splitDateKey(key, year, month, day)) return 0;
  if (++day > daysInMonth(year, month)) {
    day = 1;
    if (++month > 12) {
      month = 1;
      ++year;
    }
  }
  return makeDateKey(year, month, day);
}

inline uint32_t previousDateKey(const uint32_t key) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!splitDateKey(key, year, month, day)) return 0;
  if (day > 1) return makeDateKey(year, month, static_cast<uint8_t>(day - 1));
  if (month > 1) {
    --month;
  } else {
    if (year <= 2000) return 0;
    --year;
    month = 12;
  }
  return makeDateKey(year, month, daysInMonth(year, month));
}

inline uint32_t shiftDateKey(uint32_t key, int32_t days) {
  // Bound UI estimates so invalid pace data cannot create a long repaint loop.
  days = std::clamp<int32_t>(days, -3660, 3660);
  while (days > 0 && key != 0) {
    key = nextDateKey(key);
    --days;
  }
  while (days < 0 && key != 0) {
    key = previousDateKey(key);
    ++days;
  }
  return key;
}

inline uint32_t localDateKey(const uint32_t year, const uint8_t month, const uint8_t day, const uint8_t hour,
                             const uint8_t minute, const int offsetMinutes) {
  uint32_t key = makeDateKey(year, month, day);
  if (key == 0 || hour > 23 || minute > 59 || offsetMinutes < -12 * 60 || offsetMinutes > 14 * 60) return 0;
  const int localMinute = static_cast<int>(hour) * 60 + minute + offsetMinutes;
  if (localMinute < 0) return previousDateKey(key);
  if (localMinute >= 24 * 60) return nextDateKey(key);
  return key;
}

inline ReadingStatsLocalDateTime localDateTime(const uint32_t year, const uint8_t month, const uint8_t day,
                                               const uint8_t hour, const uint8_t minute, const uint8_t second,
                                               const int offsetMinutes) {
  ReadingStatsLocalDateTime result;
  if (second > 59 || hour > 23 || minute > 59) return result;
  result.dateKey = makeDateKey(year, month, day);
  if (result.dateKey == 0 || offsetMinutes < -12 * 60 || offsetMinutes > 14 * 60) return {};
  int localSeconds = static_cast<int>(hour) * 3600 + static_cast<int>(minute) * 60 + second + offsetMinutes * 60;
  while (localSeconds < 0) {
    result.dateKey = previousDateKey(result.dateKey);
    localSeconds += 24 * 3600;
  }
  while (localSeconds >= 24 * 3600) {
    result.dateKey = nextDateKey(result.dateKey);
    localSeconds -= 24 * 3600;
  }
  if (result.dateKey == 0) return {};
  result.secondOfDay = static_cast<uint32_t>(localSeconds);
  return result;
}

inline uint32_t dateKeyToDayIndex(const uint32_t dateKey) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!splitDateKey(dateKey, year, month, day)) return UINT32_MAX;
  uint32_t index = 0;
  for (uint32_t y = 2000; y < year; ++y) index += isLeapYear(y) ? 366U : 365U;
  for (uint8_t m = 1; m < month; ++m) index += daysInMonth(year, m);
  return index + day - 1U;
}

inline uint32_t dateKeyFromDayIndex(uint32_t index) {
  uint32_t year = 2000;
  while (year <= 2199) {
    const uint32_t count = isLeapYear(year) ? 366U : 365U;
    if (index < count) break;
    index -= count;
    ++year;
  }
  if (year > 2199) return 0;
  uint8_t month = 1;
  while (month <= 12) {
    const uint8_t count = daysInMonth(year, month);
    if (index < count) break;
    index -= count;
    ++month;
  }
  return month <= 12 ? makeDateKey(year, month, static_cast<uint8_t>(index + 1U)) : 0;
}

inline uint32_t calendarDaysElapsed(const uint32_t startDateKey, const uint32_t endDateKey) {
  const uint32_t start = dateKeyToDayIndex(startDateKey);
  const uint32_t end = dateKeyToDayIndex(endDateKey);
  return start == UINT32_MAX || end == UINT32_MAX || end < start ? 0 : end - start;
}

inline uint8_t dayOfWeekIndex(const uint32_t dateKey) {
  const uint32_t dayIndex = dateKeyToDayIndex(dateKey);
  return dayIndex == UINT32_MAX ? 0 : static_cast<uint8_t>((5U + dayIndex) % 7U);  // Monday = 0.
}

inline uint8_t timeBucketIndex(const uint32_t secondOfDay) {
  const uint8_t hour = static_cast<uint8_t>((secondOfDay / 3600U) % 24U);
  if (hour >= 5 && hour < 12) return 0;
  if (hour >= 12 && hour < 17) return 1;
  if (hour >= 17 && hour < 21) return 2;
  return 3;
}

inline uint32_t secondsUntilNextTimeBucket(const uint32_t secondOfDay) {
  constexpr uint32_t DAY_SECONDS = 24U * 3600U;
  uint32_t boundary = DAY_SECONDS;
  if (secondOfDay < 5U * 3600U)
    boundary = 5U * 3600U;
  else if (secondOfDay < 12U * 3600U)
    boundary = 12U * 3600U;
  else if (secondOfDay < 17U * 3600U)
    boundary = 17U * 3600U;
  else if (secondOfDay < 21U * 3600U)
    boundary = 21U * 3600U;
  return std::max<uint32_t>(1, boundary - std::min(secondOfDay, DAY_SECONDS - 1U));
}

inline void addSeconds(ReadingStatsLocalDateTime& value, uint32_t seconds) {
  if (!value.isValid()) return;
  uint64_t total = static_cast<uint64_t>(value.secondOfDay) + seconds;
  while (total >= 24U * 3600U) {
    value.dateKey = nextDateKey(value.dateKey);
    total -= 24U * 3600U;
    if (value.dateKey == 0) return;
  }
  value.secondOfDay = static_cast<uint32_t>(total);
}

inline bool historyBit(const std::array<uint8_t, READING_HISTORY_BYTES>& bits, const size_t index) {
  return index < READING_HISTORY_DAYS && (bits[index / 8] & static_cast<uint8_t>(1U << (index % 8))) != 0;
}

inline void setHistoryBit(std::array<uint8_t, READING_HISTORY_BYTES>& bits, const size_t index) {
  if (index < READING_HISTORY_DAYS) bits[index / 8] |= static_cast<uint8_t>(1U << (index % 8));
}

inline bool historyEmpty(const std::array<uint8_t, READING_HISTORY_BYTES>& bits) {
  return std::all_of(bits.begin(), bits.end(), [](const uint8_t value) { return value == 0; });
}

inline void shiftHistoryOlder(std::array<uint8_t, READING_HISTORY_BYTES>& bits, const uint32_t days) {
  if (days == 0) return;
  if (days >= READING_HISTORY_DAYS) {
    bits.fill(0);
    return;
  }
  std::array<uint8_t, READING_HISTORY_BYTES> shifted{};
  for (size_t i = 0; i + days < READING_HISTORY_DAYS; ++i) {
    if (historyBit(bits, i)) setHistoryBit(shifted, i + days);
  }
  bits = shifted;
}

inline void markHistoryDay(uint32_t& anchorDay, std::array<uint8_t, READING_HISTORY_BYTES>& bits,
                           const uint32_t dayIndex) {
  if (dayIndex == UINT32_MAX) return;
  if (historyEmpty(bits)) {
    anchorDay = dayIndex;
    setHistoryBit(bits, 0);
    return;
  }
  if (dayIndex > anchorDay) {
    shiftHistoryOlder(bits, dayIndex - anchorDay);
    anchorDay = dayIndex;
  }
  const uint32_t delta = anchorDay - dayIndex;
  if (delta < READING_HISTORY_DAYS) setHistoryBit(bits, delta);
}

inline void mergeHistory(uint32_t& targetAnchor, std::array<uint8_t, READING_HISTORY_BYTES>& target,
                         const uint32_t sourceAnchor, const std::array<uint8_t, READING_HISTORY_BYTES>& source) {
  if (historyEmpty(source)) return;
  if (historyEmpty(target)) {
    targetAnchor = sourceAnchor;
    target = source;
    return;
  }
  if (sourceAnchor > targetAnchor) {
    shiftHistoryOlder(target, sourceAnchor - targetAnchor);
    targetAnchor = sourceAnchor;
  }
  for (size_t i = 0; i < READING_HISTORY_DAYS; ++i) {
    if (!historyBit(source, i) || sourceAnchor < i) continue;
    const uint32_t sourceDay = sourceAnchor - static_cast<uint32_t>(i);
    if (sourceDay > targetAnchor) continue;
    const uint32_t delta = targetAnchor - sourceDay;
    if (delta < READING_HISTORY_DAYS) setHistoryBit(target, delta);
  }
}

inline uint16_t longestStreak(const std::array<uint8_t, READING_HISTORY_BYTES>& bits) {
  uint16_t best = 0;
  uint16_t current = 0;
  for (int i = static_cast<int>(READING_HISTORY_DAYS) - 1; i >= 0; --i) {
    if (historyBit(bits, static_cast<size_t>(i))) {
      current = saturatedAdd<uint16_t>(current, 1);
      best = std::max(best, current);
    } else {
      current = 0;
    }
  }
  return best;
}

inline uint16_t currentStreak(const uint32_t anchorDay, const std::array<uint8_t, READING_HISTORY_BYTES>& bits,
                              const uint32_t todayDateKey) {
  if (historyEmpty(bits) || !historyBit(bits, 0)) return 0;
  const uint32_t today = dateKeyToDayIndex(todayDateKey);
  if (today != UINT32_MAX && anchorDay + 1U < today) return 0;
  uint16_t streak = 0;
  while (streak < READING_HISTORY_DAYS && historyBit(bits, streak)) ++streak;
  return streak;
}

template <typename Stats>
void recordReadingDay(Stats& stats, const uint32_t dateKey) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  if (!splitDateKey(dateKey, year, month, day)) return;
  if (stats.firstReadDateKey == 0) stats.firstReadDateKey = dateKey;
  if (stats.lastReadDateKey == 0) {
    stats.lastReadDateKey = dateKey;
    stats.activeReadingDays = 1;
    stats.currentStreakDays = 1;
    return;
  }
  if (dateKey == stats.lastReadDateKey || dateKey < stats.lastReadDateKey) return;
  stats.activeReadingDays = saturatedAdd(stats.activeReadingDays, static_cast<decltype(stats.activeReadingDays)>(1));
  stats.currentStreakDays =
      nextDateKey(stats.lastReadDateKey) == dateKey
          ? saturatedAdd(stats.currentStreakDays, static_cast<decltype(stats.currentStreakDays)>(1))
          : static_cast<decltype(stats.currentStreakDays)>(1);
  stats.lastReadDateKey = dateKey;
}

inline uint32_t displayedCurrentStreak(const BookReadingStats& stats, const uint32_t todayDateKey) {
  if (stats.lastReadDateKey == 0 || todayDateKey == 0) return 0;
  return stats.lastReadDateKey == todayDateKey || nextDateKey(stats.lastReadDateKey) == todayDateKey
             ? stats.currentStreakDays
             : 0;
}

inline uint32_t displayedCurrentStreak(const GlobalReadingStats& stats, const uint32_t todayDateKey) {
  if (!historyEmpty(stats.readingHistoryBits)) {
    return currentStreak(stats.readingHistoryAnchorDay, stats.readingHistoryBits, todayDateKey);
  }
  if (stats.lastReadDateKey == 0 || todayDateKey == 0) return 0;
  return stats.lastReadDateKey == todayDateKey || nextDateKey(stats.lastReadDateKey) == todayDateKey
             ? stats.currentStreakDays
             : 0;
}

inline uint32_t averageDailySeconds(const BookReadingStats& stats) {
  return stats.activeReadingDays == 0 ? 0 : stats.totalReadingSeconds / stats.activeReadingDays;
}

inline uint32_t averageCalendarDailySeconds(const BookReadingStats& stats, const uint32_t endDateKey) {
  if (stats.firstReadDateKey == 0 || endDateKey == 0) return 0;
  return stats.totalReadingSeconds / std::max<uint32_t>(1, calendarDaysElapsed(stats.firstReadDateKey, endDateKey));
}

inline uint32_t estimatedFinishDateKey(const BookReadingStats& stats, const int progressPercent,
                                       const uint32_t todayDateKey) {
  const uint32_t remaining = estimatedSecondsLeft(stats, progressPercent);
  const uint32_t daily = averageCalendarDailySeconds(stats, todayDateKey);
  if (remaining == 0 || daily == 0 || todayDateKey == 0) return 0;
  const uint32_t days = (remaining + daily - 1) / daily;
  if (days > 3660) return 0;
  return shiftDateKey(todayDateKey, static_cast<int32_t>(days));
}

inline void recordPaceSamples(BookReadingStats& stats, const uint32_t sampleSeconds, const uint16_t sampleCount) {
  if (sampleSeconds == 0 || sampleCount == 0) return;
  constexpr uint16_t MAX_SAMPLES = 1000;
  const uint32_t oldWeight = std::min<uint16_t>(stats.paceSampleCount, MAX_SAMPLES);
  const uint64_t total = static_cast<uint64_t>(stats.avgSecondsPerForwardPage) * oldWeight + sampleSeconds;
  const uint32_t denominator = oldWeight + sampleCount;
  stats.avgSecondsPerForwardPage = static_cast<uint16_t>(
      std::min<uint64_t>((total + denominator / 2U) / denominator, std::numeric_limits<uint16_t>::max()));
  stats.paceSampleCount = static_cast<uint16_t>(std::min<uint32_t>(MAX_SAMPLES, oldWeight + sampleCount));
}

}  // namespace ReadingStatsMath

// Accumulates one reader visit in RAM. Intervals shorter than 2 s are navigation;
// intervals longer than the configured idle threshold are excluded entirely.
class ReadingSessionTracker {
 public:
  static constexpr uint32_t MIN_PAGE_DWELL_MS = 2'000;
  static constexpr uint32_t DEFAULT_MAX_ACTIVE_DWELL_MS = 5 * 60 * 1'000;
  static constexpr uint32_t MAX_ACTIVE_DWELL_MS = DEFAULT_MAX_ACTIVE_DWELL_MS;
  static constexpr uint32_t MIN_READING_TIME_SECONDS = 10;
  static constexpr uint32_t MIN_COUNTED_SESSION_SECONDS = 60;
  static constexpr uint32_t MIN_AUTOMATIC_START_DATE_SECONDS = 120;

  void recordInterval(const uint32_t elapsedMs, const bool forwardPageTurn,
                      const ReadingStatsLocalDateTime& localStart = {},
                      const uint32_t maxActiveDwellMs = DEFAULT_MAX_ACTIVE_DWELL_MS) {
    if (elapsedMs < MIN_PAGE_DWELL_MS || elapsedMs > maxActiveDwellMs) return;
    const uint32_t seconds = std::max<uint32_t>(1, (elapsedMs + 500) / 1000);
    activeSeconds = ReadingStatsMath::saturatedAdd(activeSeconds, seconds);
    if (forwardPageTurn) {
      pagesTurned = ReadingStatsMath::saturatedAdd<uint32_t>(pagesTurned, 1);
      // CrossInk deliberately ignores the first pace sample after opening a
      // book, returning from an overlay, or moving backwards. It is usually a
      // navigation/settling interval and otherwise biases time-left high.
      if (paceWarmupPending) {
        paceWarmupPending = false;
      } else {
        paceSampleSeconds = ReadingStatsMath::saturatedAdd(paceSampleSeconds, seconds);
        paceSamples = ReadingStatsMath::saturatedAdd<uint16_t>(paceSamples, 1);
      }
    } else {
      paceWarmupPending = true;
    }
    recordCalendarSpan(localStart, seconds);
  }

  bool commit(BookReadingStats& book, GlobalReadingStats& global, const bool completedNow,
              const uint32_t localDateKey = 0) const {
    const uint32_t initialBookFirstReadDate = book.firstReadDateKey;
    const bool becameCompleted = completedNow && !book.isCompleted;
    const bool hasReadingTime = activeSeconds >= MIN_READING_TIME_SECONDS;
    const bool hasCountedSession = activeSeconds >= MIN_COUNTED_SESSION_SECONDS;
    const bool hasPageData = pagesTurned > 0 || paceSamples > 0;

    // A forward turn is durable after its own 2-second dwell check even when
    // the whole visit is shorter than 10 seconds. This matches CrossInk and
    // avoids losing legitimate page/pace samples during short sessions.
    if (pagesTurned > 0) {
      book.totalPagesTurned = ReadingStatsMath::saturatedAdd(book.totalPagesTurned, pagesTurned);
      global.totalPagesTurned = ReadingStatsMath::saturatedAdd(global.totalPagesTurned, pagesTurned);
    }
    if (paceSamples > 0) ReadingStatsMath::recordPaceSamples(book, paceSampleSeconds, paceSamples);

    if (hasReadingTime) {
      book.totalReadingSeconds = ReadingStatsMath::saturatedAdd(book.totalReadingSeconds, activeSeconds);
      global.totalReadingSeconds = ReadingStatsMath::saturatedAdd(global.totalReadingSeconds, activeSeconds);
      if (hasCountedSession) {
        book.sessionCount = ReadingStatsMath::saturatedAdd<uint16_t>(book.sessionCount, 1);
        global.sessionCount = ReadingStatsMath::saturatedAdd<uint32_t>(global.sessionCount, 1);
      }
      for (size_t i = 0; i < book.timeOfDaySeconds.size(); ++i) {
        book.timeOfDaySeconds[i] = ReadingStatsMath::saturatedAdd(book.timeOfDaySeconds[i], timeOfDaySeconds[i]);
        global.timeOfDaySeconds[i] = ReadingStatsMath::saturatedAdd(global.timeOfDaySeconds[i], timeOfDaySeconds[i]);
      }
      for (size_t i = 0; i < book.dayOfWeekSeconds.size(); ++i) {
        book.dayOfWeekSeconds[i] = ReadingStatsMath::saturatedAdd(book.dayOfWeekSeconds[i], dayOfWeekSeconds[i]);
        global.dayOfWeekSeconds[i] = ReadingStatsMath::saturatedAdd(global.dayOfWeekSeconds[i], dayOfWeekSeconds[i]);
      }
      bool recordedCalendarDay = false;
      if (!ReadingStatsMath::historyEmpty(readingHistoryBits)) {
        for (int i = static_cast<int>(READING_HISTORY_DAYS) - 1; i >= 0; --i) {
          if (!ReadingStatsMath::historyBit(readingHistoryBits, static_cast<size_t>(i)) ||
              readingHistoryAnchorDay < static_cast<uint32_t>(i))
            continue;
          const uint32_t key = ReadingStatsMath::dateKeyFromDayIndex(readingHistoryAnchorDay - i);
          ReadingStatsMath::recordReadingDay(book, key);
          ReadingStatsMath::recordReadingDay(global, key);
          recordedCalendarDay = true;
        }
        ReadingStatsMath::mergeHistory(global.readingHistoryAnchorDay, global.readingHistoryBits,
                                       readingHistoryAnchorDay, readingHistoryBits);
        global.longestReadingStreak =
            std::max(global.longestReadingStreak, ReadingStatsMath::longestStreak(global.readingHistoryBits));
      }
      if (!recordedCalendarDay) {
        ReadingStatsMath::recordReadingDay(book, localDateKey);
        ReadingStatsMath::recordReadingDay(global, localDateKey);
        ReadingStatsMath::markHistoryDay(global.readingHistoryAnchorDay, global.readingHistoryBits,
                                         ReadingStatsMath::dateKeyToDayIndex(localDateKey));
        global.longestReadingStreak =
            std::max(global.longestReadingStreak, ReadingStatsMath::longestStreak(global.readingHistoryBits));
      }
      // Keep active-day history from 10 seconds, but only expose an automatic
      // per-book start date after a substantial 2-minute visit. This is the
      // threshold used by CrossInk's dashboard and prevents accidental opens
      // from defining the book's start date.
      if (activeSeconds < MIN_AUTOMATIC_START_DATE_SECONDS && !book.firstReadDateManual &&
          book.firstReadDateKey != initialBookFirstReadDate) {
        book.firstReadDateKey = initialBookFirstReadDate;
      }
    }
    if (becameCompleted) {
      book.isCompleted = true;
      if (!book.completedDateManual && book.completedDateKey == 0) book.completedDateKey = localDateKey;
      global.completedBooks = ReadingStatsMath::saturatedAdd<uint32_t>(global.completedBooks, 1);
    }
    return hasReadingTime || hasCountedSession || hasPageData || becameCompleted;
  }

  uint32_t sessionSeconds() const { return activeSeconds; }
  uint32_t sessionPagesTurned() const { return pagesTurned; }
  uint32_t sessionPaceSeconds() const { return paceSampleSeconds; }
  uint16_t sessionPaceSamples() const { return paceSamples; }
  const std::array<uint32_t, READING_TIME_BUCKET_COUNT>& sessionTimeOfDaySeconds() const { return timeOfDaySeconds; }
  const std::array<uint32_t, READING_DAY_OF_WEEK_COUNT>& sessionDayOfWeekSeconds() const { return dayOfWeekSeconds; }
  uint32_t sessionHistoryAnchorDay() const { return readingHistoryAnchorDay; }
  const std::array<uint8_t, READING_HISTORY_BYTES>& sessionHistoryBits() const { return readingHistoryBits; }

 private:
  void recordCalendarSpan(ReadingStatsLocalDateTime cursor, uint32_t seconds) {
    if (!cursor.isValid() || seconds == 0) return;
    uint32_t remaining = seconds;
    while (remaining > 0 && cursor.isValid()) {
      ReadingStatsMath::markHistoryDay(readingHistoryAnchorDay, readingHistoryBits,
                                       ReadingStatsMath::dateKeyToDayIndex(cursor.dateKey));
      const uint32_t segment = std::min(remaining, ReadingStatsMath::secondsUntilNextTimeBucket(cursor.secondOfDay));
      const uint8_t timeBucket = ReadingStatsMath::timeBucketIndex(cursor.secondOfDay);
      const uint8_t weekday = ReadingStatsMath::dayOfWeekIndex(cursor.dateKey);
      timeOfDaySeconds[timeBucket] = ReadingStatsMath::saturatedAdd(timeOfDaySeconds[timeBucket], segment);
      dayOfWeekSeconds[weekday] = ReadingStatsMath::saturatedAdd(dayOfWeekSeconds[weekday], segment);
      remaining -= segment;
      ReadingStatsMath::addSeconds(cursor, segment);
    }
  }

  uint32_t activeSeconds = 0;
  uint32_t pagesTurned = 0;
  uint32_t paceSampleSeconds = 0;
  uint16_t paceSamples = 0;
  bool paceWarmupPending = true;
  std::array<uint32_t, READING_TIME_BUCKET_COUNT> timeOfDaySeconds{};
  std::array<uint32_t, READING_DAY_OF_WEEK_COUNT> dayOfWeekSeconds{};
  uint32_t readingHistoryAnchorDay = 0;
  std::array<uint8_t, READING_HISTORY_BYTES> readingHistoryBits{};
};
