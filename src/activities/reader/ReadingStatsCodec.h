#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ReadingStats.h"

namespace ReadingStatsCodec {

enum class DecodeStatus { OK, NEWER_VERSION, INVALID };

namespace detail {
constexpr uint8_t BOOK_VERSION = 4;
constexpr uint8_t GLOBAL_VERSION = 3;

inline void put16(uint8_t* out, const uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

inline void put32(uint8_t* out, const uint32_t value) {
  for (int i = 0; i < 4; ++i) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint16_t get16(const uint8_t* in) { return static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8); }

inline uint32_t get32(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) | (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

inline uint32_t checksum(const uint8_t* bytes, const size_t size) {
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619U;
  }
  return hash;
}

template <size_t N>
DecodeStatus validate(const std::array<uint8_t, 4>& magic, const uint8_t version, const uint8_t* bytes,
                      const size_t size) {
  if (size >= 5 && std::equal(magic.begin(), magic.end(), bytes) && bytes[4] > version) {
    return DecodeStatus::NEWER_VERSION;
  }
  if (size != N || !std::equal(magic.begin(), magic.end(), bytes) || bytes[4] != version ||
      get32(bytes + N - 4) != checksum(bytes, N - 4)) {
    return DecodeStatus::INVALID;
  }
  return DecodeStatus::OK;
}

inline bool validDateOrZero(const uint32_t key) {
  if (key == 0) return true;
  uint32_t year;
  uint8_t month;
  uint8_t day;
  return ReadingStatsMath::splitDateKey(key, year, month, day);
}

template <typename Stats>
bool validCalendarSummary(const Stats& stats) {
  return validDateOrZero(stats.firstReadDateKey) && validDateOrZero(stats.lastReadDateKey) &&
         ((stats.firstReadDateKey == 0) == (stats.lastReadDateKey == 0)) &&
         ((stats.lastReadDateKey == 0) == (stats.activeReadingDays == 0)) &&
         stats.currentStreakDays <= stats.activeReadingDays &&
         (stats.firstReadDateKey == 0 || stats.lastReadDateKey >= stats.firstReadDateKey);
}
}  // namespace detail

struct BookCodec {
  static constexpr std::array<uint8_t, 4> MAGIC = {'V', 'N', 'B', 'S'};
  using Encoded = std::array<uint8_t, 91>;
  using V3Encoded = std::array<uint8_t, 34>;
  using V2Encoded = std::array<uint8_t, 22>;
  using V1Encoded = std::array<uint8_t, 20>;

  static Encoded encode(const BookReadingStats& stats) {
    Encoded out{};
    std::copy(MAGIC.begin(), MAGIC.end(), out.begin());
    out[4] = detail::BOOK_VERSION;
    detail::put32(out.data() + 5, stats.totalReadingSeconds);
    detail::put32(out.data() + 9, stats.totalPagesTurned);
    detail::put16(out.data() + 13, stats.sessionCount);
    out[15] = stats.isCompleted ? 1 : 0;
    detail::put16(out.data() + 16, stats.progressPermille);
    detail::put32(out.data() + 18, stats.firstReadDateKey);
    detail::put32(out.data() + 22, stats.lastReadDateKey);
    detail::put16(out.data() + 26, stats.activeReadingDays);
    detail::put16(out.data() + 28, stats.currentStreakDays);
    detail::put16(out.data() + 30, stats.avgSecondsPerForwardPage);
    detail::put16(out.data() + 32, stats.paceSampleCount);
    detail::put32(out.data() + 34, stats.estimatedTimeLeftSeconds);
    detail::put32(out.data() + 38, stats.completedDateKey);
    out[42] = (stats.firstReadDateManual ? 1U : 0U) | (stats.completedDateManual ? 2U : 0U);
    for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
      detail::put32(out.data() + 43 + i * 4, stats.timeOfDaySeconds[i]);
    }
    for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
      detail::put32(out.data() + 59 + i * 4, stats.dayOfWeekSeconds[i]);
    }
    detail::put32(out.data() + 87, detail::checksum(out.data(), 87));
    return out;
  }

  static DecodeStatus decode(const uint8_t* bytes, const size_t size, BookReadingStats& stats) {
    if (size >= 5 && std::equal(MAGIC.begin(), MAGIC.end(), bytes) && bytes[4] > detail::BOOK_VERSION) {
      return DecodeStatus::NEWER_VERSION;
    }
    const uint8_t storedVersion = size >= 5 ? bytes[4] : 0;
    const auto status = storedVersion == 1   ? detail::validate<V1Encoded{}.size()>(MAGIC, 1, bytes, size)
                        : storedVersion == 2 ? detail::validate<V2Encoded{}.size()>(MAGIC, 2, bytes, size)
                        : storedVersion == 3
                            ? detail::validate<V3Encoded{}.size()>(MAGIC, 3, bytes, size)
                            : detail::validate<Encoded{}.size()>(MAGIC, detail::BOOK_VERSION, bytes, size);
    if (status != DecodeStatus::OK) return status;
    if (bytes[15] > 1) return DecodeStatus::INVALID;

    stats = {};
    stats.totalReadingSeconds = detail::get32(bytes + 5);
    stats.totalPagesTurned = detail::get32(bytes + 9);
    stats.sessionCount = detail::get16(bytes + 13);
    stats.isCompleted = bytes[15] != 0;
    stats.progressPermille =
        storedVersion == 1 ? BookReadingStats::UNKNOWN_PROGRESS_PERMILLE : detail::get16(bytes + 16);
    if (stats.progressPermille != BookReadingStats::UNKNOWN_PROGRESS_PERMILLE && stats.progressPermille > 1000) {
      return DecodeStatus::INVALID;
    }
    if (storedVersion >= 3) {
      stats.firstReadDateKey = detail::get32(bytes + 18);
      stats.lastReadDateKey = detail::get32(bytes + 22);
      stats.activeReadingDays = detail::get16(bytes + 26);
      stats.currentStreakDays = detail::get16(bytes + 28);
      if (!detail::validCalendarSummary(stats)) return DecodeStatus::INVALID;
    }
    if (storedVersion >= 4) {
      stats.avgSecondsPerForwardPage = detail::get16(bytes + 30);
      stats.paceSampleCount = detail::get16(bytes + 32);
      stats.estimatedTimeLeftSeconds = detail::get32(bytes + 34);
      stats.completedDateKey = detail::get32(bytes + 38);
      const uint8_t flags = bytes[42];
      if (flags > 3) return DecodeStatus::INVALID;
      stats.firstReadDateManual = (flags & 1U) != 0;
      stats.completedDateManual = (flags & 2U) != 0;
      for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
        stats.timeOfDaySeconds[i] = detail::get32(bytes + 43 + i * 4);
      }
      for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
        stats.dayOfWeekSeconds[i] = detail::get32(bytes + 59 + i * 4);
      }
      if ((stats.avgSecondsPerForwardPage == 0) != (stats.paceSampleCount == 0) || stats.paceSampleCount > 1000 ||
          !detail::validDateOrZero(stats.completedDateKey) || (stats.completedDateKey != 0 && !stats.isCompleted) ||
          (stats.firstReadDateManual && stats.firstReadDateKey == 0) ||
          (stats.completedDateManual && stats.completedDateKey == 0) ||
          (stats.completedDateKey != 0 && stats.firstReadDateKey != 0 &&
           stats.completedDateKey < stats.firstReadDateKey)) {
        return DecodeStatus::INVALID;
      }
    }
    return DecodeStatus::OK;
  }
};

struct GlobalCodec {
  static constexpr std::array<uint8_t, 4> MAGIC = {'V', 'N', 'G', 'S'};
  using Encoded = std::array<uint8_t, 183>;
  using V2Encoded = std::array<uint8_t, 41>;
  using V1Encoded = std::array<uint8_t, 25>;

  static Encoded encode(const GlobalReadingStats& stats) {
    Encoded out{};
    std::copy(MAGIC.begin(), MAGIC.end(), out.begin());
    out[4] = detail::GLOBAL_VERSION;
    detail::put32(out.data() + 5, stats.totalReadingSeconds);
    detail::put32(out.data() + 9, stats.totalPagesTurned);
    detail::put32(out.data() + 13, stats.sessionCount);
    detail::put32(out.data() + 17, stats.completedBooks);
    detail::put32(out.data() + 21, stats.firstReadDateKey);
    detail::put32(out.data() + 25, stats.lastReadDateKey);
    detail::put32(out.data() + 29, stats.activeReadingDays);
    detail::put32(out.data() + 33, stats.currentStreakDays);
    for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
      detail::put32(out.data() + 37 + i * 4, stats.timeOfDaySeconds[i]);
    }
    for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
      detail::put32(out.data() + 53 + i * 4, stats.dayOfWeekSeconds[i]);
    }
    detail::put32(out.data() + 81, stats.readingHistoryAnchorDay);
    std::memcpy(out.data() + 85, stats.readingHistoryBits.data(), stats.readingHistoryBits.size());
    detail::put16(out.data() + 177, stats.longestReadingStreak);
    detail::put32(out.data() + 179, detail::checksum(out.data(), 179));
    return out;
  }

  static DecodeStatus decode(const uint8_t* bytes, const size_t size, GlobalReadingStats& stats) {
    if (size >= 5 && std::equal(MAGIC.begin(), MAGIC.end(), bytes) && bytes[4] > detail::GLOBAL_VERSION) {
      return DecodeStatus::NEWER_VERSION;
    }
    const uint8_t storedVersion = size >= 5 ? bytes[4] : 0;
    const auto status = storedVersion == 1 ? detail::validate<V1Encoded{}.size()>(MAGIC, 1, bytes, size)
                        : storedVersion == 2
                            ? detail::validate<V2Encoded{}.size()>(MAGIC, 2, bytes, size)
                            : detail::validate<Encoded{}.size()>(MAGIC, detail::GLOBAL_VERSION, bytes, size);
    if (status != DecodeStatus::OK) return status;

    stats = {};
    stats.totalReadingSeconds = detail::get32(bytes + 5);
    stats.totalPagesTurned = detail::get32(bytes + 9);
    stats.sessionCount = detail::get32(bytes + 13);
    stats.completedBooks = detail::get32(bytes + 17);
    if (storedVersion >= 2) {
      stats.firstReadDateKey = detail::get32(bytes + 21);
      stats.lastReadDateKey = detail::get32(bytes + 25);
      stats.activeReadingDays = detail::get32(bytes + 29);
      stats.currentStreakDays = detail::get32(bytes + 33);
      if (!detail::validCalendarSummary(stats)) return DecodeStatus::INVALID;
    }
    if (storedVersion >= 3) {
      for (size_t i = 0; i < stats.timeOfDaySeconds.size(); ++i) {
        stats.timeOfDaySeconds[i] = detail::get32(bytes + 37 + i * 4);
      }
      for (size_t i = 0; i < stats.dayOfWeekSeconds.size(); ++i) {
        stats.dayOfWeekSeconds[i] = detail::get32(bytes + 53 + i * 4);
      }
      stats.readingHistoryAnchorDay = detail::get32(bytes + 81);
      std::memcpy(stats.readingHistoryBits.data(), bytes + 85, stats.readingHistoryBits.size());
      stats.longestReadingStreak = detail::get16(bytes + 177);
      if (ReadingStatsMath::historyEmpty(stats.readingHistoryBits)) {
        if (stats.readingHistoryAnchorDay != 0) return DecodeStatus::INVALID;
      } else if (ReadingStatsMath::dateKeyFromDayIndex(stats.readingHistoryAnchorDay) == 0) {
        return DecodeStatus::INVALID;
      }
      for (size_t i = static_cast<size_t>(stats.readingHistoryAnchorDay) + 1;
           i < READING_HISTORY_DAYS && i < stats.readingHistoryBits.size() * 8U; ++i) {
        if (ReadingStatsMath::historyBit(stats.readingHistoryBits, i)) return DecodeStatus::INVALID;
      }
      const uint16_t retainedLongest = ReadingStatsMath::longestStreak(stats.readingHistoryBits);
      if (stats.longestReadingStreak > READING_HISTORY_DAYS || stats.longestReadingStreak < retainedLongest) {
        return DecodeStatus::INVALID;
      }
    }
    return DecodeStatus::OK;
  }
};

}  // namespace ReadingStatsCodec
