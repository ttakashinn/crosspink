#include <gtest/gtest.h>

#include "activities/reader/ReadingStats.h"
#include "activities/reader/ReadingStatsCodec.h"
#include "activities/reader/ReadingStatsLayout.h"

namespace {

template <size_t N>
void refreshChecksum(std::array<uint8_t, N>& bytes) {
  ReadingStatsCodec::detail::put32(bytes.data() + N - 4, ReadingStatsCodec::detail::checksum(bytes.data(), N - 4));
}

}  // namespace

TEST(ReadingStatsCodec, RoundTripsEveryBookAndGlobalField) {
  BookReadingStats book;
  book.totalReadingSeconds = 7200;
  book.totalPagesTurned = 245;
  book.sessionCount = 8;
  book.isCompleted = true;
  book.progressPermille = 735;
  book.firstReadDateKey = 20260820;
  book.lastReadDateKey = 20260829;
  book.activeReadingDays = 6;
  book.currentStreakDays = 3;
  book.avgSecondsPerForwardPage = 29;
  book.paceSampleCount = 245;
  book.estimatedTimeLeftSeconds = 2600;
  book.completedDateKey = 20260829;
  book.firstReadDateManual = true;
  book.completedDateManual = true;
  book.timeOfDaySeconds = {1200, 2400, 3000, 600};
  book.dayOfWeekSeconds = {120, 240, 360, 480, 600, 2400, 3000};
  const auto bookBytes = ReadingStatsCodec::BookCodec::encode(book);
  BookReadingStats decodedBook;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bookBytes.data(), bookBytes.size(), decodedBook),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(decodedBook, book);

  GlobalReadingStats global;
  global.totalReadingSeconds = 86400;
  global.totalPagesTurned = 3200;
  global.sessionCount = 130;
  global.completedBooks = 14;
  global.firstReadDateKey = 20260102;
  global.lastReadDateKey = 20260829;
  global.activeReadingDays = 80;
  global.currentStreakDays = 12;
  global.timeOfDaySeconds = {12000, 24000, 32000, 18400};
  global.dayOfWeekSeconds = {10000, 11000, 12000, 13000, 14000, 15000, 11400};
  global.readingHistoryAnchorDay = ReadingStatsMath::dateKeyToDayIndex(20260829);
  ReadingStatsMath::setHistoryBit(global.readingHistoryBits, 0);
  ReadingStatsMath::setHistoryBit(global.readingHistoryBits, 1);
  ReadingStatsMath::setHistoryBit(global.readingHistoryBits, 2);
  ReadingStatsMath::setHistoryBit(global.readingHistoryBits, 5);
  global.longestReadingStreak = 12;
  const auto globalBytes = ReadingStatsCodec::GlobalCodec::encode(global);
  GlobalReadingStats decodedGlobal;
  EXPECT_EQ(ReadingStatsCodec::GlobalCodec::decode(globalBytes.data(), globalBytes.size(), decodedGlobal),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(decodedGlobal, global);
}

TEST(ReadingStatsCodec, MigratesV3BookStatsAndV2GlobalStats) {
  ReadingStatsCodec::BookCodec::V3Encoded bookBytes{};
  std::copy(ReadingStatsCodec::BookCodec::MAGIC.begin(), ReadingStatsCodec::BookCodec::MAGIC.end(), bookBytes.begin());
  bookBytes[4] = 3;
  ReadingStatsCodec::detail::put32(bookBytes.data() + 5, 3600);
  ReadingStatsCodec::detail::put32(bookBytes.data() + 9, 80);
  ReadingStatsCodec::detail::put16(bookBytes.data() + 13, 4);
  bookBytes[15] = 0;
  ReadingStatsCodec::detail::put16(bookBytes.data() + 16, 420);
  ReadingStatsCodec::detail::put32(bookBytes.data() + 18, 20260820);
  ReadingStatsCodec::detail::put32(bookBytes.data() + 22, 20260829);
  ReadingStatsCodec::detail::put16(bookBytes.data() + 26, 5);
  ReadingStatsCodec::detail::put16(bookBytes.data() + 28, 2);
  refreshChecksum(bookBytes);

  BookReadingStats book;
  ASSERT_EQ(ReadingStatsCodec::BookCodec::decode(bookBytes.data(), bookBytes.size(), book),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(book.progressPermille, 420U);
  EXPECT_EQ(book.firstReadDateKey, 20260820U);
  EXPECT_EQ(book.avgSecondsPerForwardPage, 0U);
  EXPECT_EQ(book.timeOfDaySeconds, (std::array<uint32_t, 4>{}));

  ReadingStatsCodec::GlobalCodec::V2Encoded globalBytes{};
  std::copy(ReadingStatsCodec::GlobalCodec::MAGIC.begin(), ReadingStatsCodec::GlobalCodec::MAGIC.end(),
            globalBytes.begin());
  globalBytes[4] = 2;
  ReadingStatsCodec::detail::put32(globalBytes.data() + 5, 7200);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 9, 160);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 13, 8);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 17, 2);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 21, 20260820);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 25, 20260829);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 29, 5);
  ReadingStatsCodec::detail::put32(globalBytes.data() + 33, 2);
  refreshChecksum(globalBytes);

  GlobalReadingStats global;
  ASSERT_EQ(ReadingStatsCodec::GlobalCodec::decode(globalBytes.data(), globalBytes.size(), global),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(global.completedBooks, 2U);
  EXPECT_TRUE(ReadingStatsMath::historyEmpty(global.readingHistoryBits));
  EXPECT_EQ(global.longestReadingStreak, 0U);
}

TEST(ReadingStatsCodec, MigratesV2BookStatsWithoutInventingCalendarData) {
  ReadingStatsCodec::BookCodec::V2Encoded bytes{};
  std::copy(ReadingStatsCodec::BookCodec::MAGIC.begin(), ReadingStatsCodec::BookCodec::MAGIC.end(), bytes.begin());
  bytes[4] = 2;
  ReadingStatsCodec::detail::put32(bytes.data() + 5, 7200);
  ReadingStatsCodec::detail::put32(bytes.data() + 9, 245);
  ReadingStatsCodec::detail::put16(bytes.data() + 13, 8);
  bytes[15] = 0;
  ReadingStatsCodec::detail::put16(bytes.data() + 16, 735);
  ReadingStatsCodec::detail::put32(bytes.data() + 18, ReadingStatsCodec::detail::checksum(bytes.data(), 18));

  BookReadingStats decoded;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bytes.data(), bytes.size(), decoded),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(decoded.progressPermille, 735U);
  EXPECT_EQ(decoded.firstReadDateKey, 0U);
  EXPECT_EQ(decoded.activeReadingDays, 0U);
}

TEST(ReadingStatsCodec, MigratesLegacyBookStatsWithoutInventingProgress) {
  std::array<uint8_t, 20> bytes{};
  std::copy(ReadingStatsCodec::BookCodec::MAGIC.begin(), ReadingStatsCodec::BookCodec::MAGIC.end(), bytes.begin());
  bytes[4] = 1;
  ReadingStatsCodec::detail::put32(bytes.data() + 5, 7200);
  ReadingStatsCodec::detail::put32(bytes.data() + 9, 245);
  ReadingStatsCodec::detail::put16(bytes.data() + 13, 8);
  bytes[15] = 1;
  ReadingStatsCodec::detail::put32(bytes.data() + 16, ReadingStatsCodec::detail::checksum(bytes.data(), 16));

  BookReadingStats decoded;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bytes.data(), bytes.size(), decoded),
            ReadingStatsCodec::DecodeStatus::OK);
  EXPECT_EQ(decoded.totalReadingSeconds, 7200U);
  EXPECT_EQ(decoded.totalPagesTurned, 245U);
  EXPECT_EQ(decoded.sessionCount, 8U);
  EXPECT_TRUE(decoded.isCompleted);
  EXPECT_EQ(decoded.progressPermille, BookReadingStats::UNKNOWN_PROGRESS_PERMILLE);
}

TEST(ReadingStatsCodec, RejectsCorruptionAndPreservesNewerVersion) {
  auto bytes = ReadingStatsCodec::BookCodec::encode({});
  bytes[8] ^= 0x55;
  BookReadingStats stats;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bytes.data(), bytes.size(), stats),
            ReadingStatsCodec::DecodeStatus::INVALID);

  bytes = ReadingStatsCodec::BookCodec::encode({});
  bytes[4]++;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bytes.data(), bytes.size(), stats),
            ReadingStatsCodec::DecodeStatus::NEWER_VERSION);
}

TEST(ReadingStatsCodec, RejectsInvalidPaceDatesFlagsAndHistory) {
  BookReadingStats validBook;
  validBook.totalReadingSeconds = 1200;
  validBook.totalPagesTurned = 10;
  validBook.sessionCount = 1;
  validBook.firstReadDateKey = 20260829;
  validBook.lastReadDateKey = 20260829;
  validBook.activeReadingDays = 1;
  validBook.currentStreakDays = 1;
  validBook.avgSecondsPerForwardPage = 120;
  validBook.paceSampleCount = 10;
  auto bookBytes = ReadingStatsCodec::BookCodec::encode(validBook);

  ReadingStatsCodec::detail::put16(bookBytes.data() + 32, 1001);
  refreshChecksum(bookBytes);
  BookReadingStats decodedBook;
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bookBytes.data(), bookBytes.size(), decodedBook),
            ReadingStatsCodec::DecodeStatus::INVALID);

  bookBytes = ReadingStatsCodec::BookCodec::encode(validBook);
  bookBytes[42] = 0x80;
  refreshChecksum(bookBytes);
  EXPECT_EQ(ReadingStatsCodec::BookCodec::decode(bookBytes.data(), bookBytes.size(), decodedBook),
            ReadingStatsCodec::DecodeStatus::INVALID);

  GlobalReadingStats validGlobal;
  validGlobal.firstReadDateKey = 20000101;
  validGlobal.lastReadDateKey = 20000101;
  validGlobal.activeReadingDays = 1;
  validGlobal.currentStreakDays = 1;
  validGlobal.readingHistoryAnchorDay = 0;
  ReadingStatsMath::setHistoryBit(validGlobal.readingHistoryBits, 0);
  validGlobal.longestReadingStreak = 1;
  auto globalBytes = ReadingStatsCodec::GlobalCodec::encode(validGlobal);
  ReadingStatsMath::setHistoryBit(validGlobal.readingHistoryBits, 1);  // Before supported epoch.
  globalBytes = ReadingStatsCodec::GlobalCodec::encode(validGlobal);
  GlobalReadingStats decodedGlobal;
  EXPECT_EQ(ReadingStatsCodec::GlobalCodec::decode(globalBytes.data(), globalBytes.size(), decodedGlobal),
            ReadingStatsCodec::DecodeStatus::INVALID);

  validGlobal.readingHistoryBits.fill(0);
  validGlobal.readingHistoryAnchorDay = ReadingStatsMath::dateKeyToDayIndex(20260829);
  ReadingStatsMath::setHistoryBit(validGlobal.readingHistoryBits, 0);
  ReadingStatsMath::setHistoryBit(validGlobal.readingHistoryBits, 1);
  validGlobal.longestReadingStreak = 1;
  globalBytes = ReadingStatsCodec::GlobalCodec::encode(validGlobal);
  EXPECT_EQ(ReadingStatsCodec::GlobalCodec::decode(globalBytes.data(), globalBytes.size(), decodedGlobal),
            ReadingStatsCodec::DecodeStatus::INVALID);
}

TEST(ReadingSessionTracker, FiltersNavigationAndIdleTime) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(ReadingSessionTracker::MIN_PAGE_DWELL_MS - 1, true);
  tracker.recordInterval(ReadingSessionTracker::MAX_ACTIVE_DWELL_MS + 1, true);
  EXPECT_EQ(tracker.sessionSeconds(), 0U);
  EXPECT_EQ(tracker.sessionPagesTurned(), 0U);
}

TEST(ReadingSessionTracker, AppliesConfiguredIdleThresholdAndForwardPaceOnly) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(30'001, true, {}, 30'000);
  tracker.recordInterval(12'000, false, {}, 30'000);
  tracker.recordInterval(18'000, true, {}, 30'000);
  tracker.recordInterval(30'000, true, {}, 30'000);
  EXPECT_EQ(tracker.sessionSeconds(), 60U);
  EXPECT_EQ(tracker.sessionPagesTurned(), 2U);
  EXPECT_EQ(tracker.sessionPaceSeconds(), 30U);
  EXPECT_EQ(tracker.sessionPaceSamples(), 1U);

  BookReadingStats book;
  GlobalReadingStats global;
  ASSERT_TRUE(tracker.commit(book, global, false));
  EXPECT_EQ(book.avgSecondsPerForwardPage, 30U);
  EXPECT_EQ(book.paceSampleCount, 1U);
}

TEST(ReadingSessionTracker, SplitsCalendarBucketsAcrossBoundariesAndMidnight) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(20'000, false, {20260830, 11U * 3600U + 59U * 60U + 50U});
  tracker.recordInterval(20'000, false, {20260830, 23U * 3600U + 59U * 60U + 50U});

  EXPECT_EQ(tracker.sessionTimeOfDaySeconds(), (std::array<uint32_t, 4>{10, 10, 0, 20}));
  // 30/08/2026 is Sunday; the second span contributes 10 s to Monday after midnight.
  EXPECT_EQ(tracker.sessionDayOfWeekSeconds(), (std::array<uint32_t, 7>{10, 0, 0, 0, 0, 0, 30}));

  BookReadingStats book;
  GlobalReadingStats global;
  ASSERT_TRUE(tracker.commit(book, global, false));
  // Bucket/history tracking starts at 10 seconds; an automatic book start
  // date requires one substantial 2-minute visit.
  EXPECT_EQ(book.firstReadDateKey, 0U);
  EXPECT_EQ(book.lastReadDateKey, 20260831U);
  EXPECT_EQ(book.activeReadingDays, 2U);
  EXPECT_EQ(global.longestReadingStreak, 2U);
}

TEST(ReadingSessionTracker, CommitsOneBatchedSessionAndCompletion) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(30'000, true);
  tracker.recordInterval(35'000, false);
  BookReadingStats book;
  GlobalReadingStats global;
  ASSERT_TRUE(tracker.commit(book, global, true, 20260829));
  EXPECT_EQ(book.totalReadingSeconds, 65U);
  EXPECT_EQ(book.totalPagesTurned, 1U);
  EXPECT_EQ(book.sessionCount, 1U);
  EXPECT_TRUE(book.isCompleted);
  EXPECT_EQ(global.totalReadingSeconds, 65U);
  EXPECT_EQ(global.completedBooks, 1U);
  EXPECT_EQ(book.firstReadDateKey, 0U);
  EXPECT_EQ(book.currentStreakDays, 1U);
}

TEST(ReadingSessionTracker, ShortCommittedVisitAddsTimeButNotSessionCount) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(15'000, false);
  BookReadingStats book;
  GlobalReadingStats global;
  ASSERT_TRUE(tracker.commit(book, global, false));
  EXPECT_EQ(book.totalReadingSeconds, 15U);
  EXPECT_EQ(book.sessionCount, 0U);
  EXPECT_EQ(global.sessionCount, 0U);
  EXPECT_EQ(ReadingStatsMath::averageSessionSeconds(book), 0U);
}

TEST(ReadingSessionTracker, SetsAutomaticStartDateAfterTwoMinuteVisit) {
  ReadingSessionTracker tracker;
  tracker.recordInterval(120'000, false, {20260829, 8U * 3600U});
  BookReadingStats book;
  GlobalReadingStats global;
  ASSERT_TRUE(tracker.commit(book, global, false, 20260829));
  EXPECT_EQ(book.firstReadDateKey, 20260829U);
  EXPECT_EQ(book.sessionCount, 1U);
}

TEST(ReadingSessionTracker, CompletionIsCountedOnlyOnce) {
  ReadingSessionTracker tracker;
  BookReadingStats book{0, 0, 0, true};
  GlobalReadingStats global{0, 0, 0, 4};
  EXPECT_FALSE(tracker.commit(book, global, true));
  EXPECT_EQ(global.completedBooks, 4U);
}

TEST(ReadingStatsCalendar, HandlesLeapDaysTimezoneAndStreaks) {
  uint32_t year;
  uint8_t month;
  uint8_t day;
  EXPECT_FALSE(ReadingStatsMath::splitDateKey(0, year, month, day));
  EXPECT_EQ(ReadingStatsMath::nextDateKey(20240228), 20240229U);
  EXPECT_EQ(ReadingStatsMath::nextDateKey(20240229), 20240301U);
  EXPECT_EQ(ReadingStatsMath::previousDateKey(20260101), 20251231U);
  EXPECT_EQ(ReadingStatsMath::localDateKey(2026, 8, 29, 18, 30, 7 * 60), 20260830U);
  EXPECT_EQ(ReadingStatsMath::localDateKey(2026, 8, 29, 2, 30, -4 * 60), 20260828U);

  BookReadingStats unknownClock;
  ReadingStatsMath::recordReadingDay(unknownClock, 0);
  EXPECT_EQ(unknownClock.firstReadDateKey, 0U);
  EXPECT_EQ(unknownClock.activeReadingDays, 0U);

  BookReadingStats stats;
  ReadingStatsMath::recordReadingDay(stats, 20260827);
  ReadingStatsMath::recordReadingDay(stats, 20260827);
  ReadingStatsMath::recordReadingDay(stats, 20260828);
  ReadingStatsMath::recordReadingDay(stats, 20260830);
  EXPECT_EQ(stats.firstReadDateKey, 20260827U);
  EXPECT_EQ(stats.lastReadDateKey, 20260830U);
  EXPECT_EQ(stats.activeReadingDays, 3U);
  EXPECT_EQ(stats.currentStreakDays, 1U);
}

TEST(ReadingStatsCalendar, IgnoresUnknownOrBackwardClockAndBoundsFinishEstimate) {
  BookReadingStats stats{3600, 60, 3, false, 500, 20260829, 20260829, 1, 1};
  ReadingStatsMath::recordReadingDay(stats, 0);
  ReadingStatsMath::recordReadingDay(stats, 20260828);
  EXPECT_EQ(stats.lastReadDateKey, 20260829U);
  EXPECT_EQ(stats.activeReadingDays, 1U);
  EXPECT_EQ(ReadingStatsMath::averageDailySeconds(stats), 3600U);
  EXPECT_EQ(ReadingStatsMath::estimatedFinishDateKey(stats, 50, 20260829), 20260830U);
}

TEST(ReadingStatsCalendar, ComputesCurrentAndLongestHistoryWithoutKeepingAStaleStreak) {
  GlobalReadingStats stats;
  const uint32_t anchor = ReadingStatsMath::dateKeyToDayIndex(20260829);
  ReadingStatsMath::markHistoryDay(stats.readingHistoryAnchorDay, stats.readingHistoryBits, anchor - 5);
  ReadingStatsMath::markHistoryDay(stats.readingHistoryAnchorDay, stats.readingHistoryBits, anchor - 4);
  ReadingStatsMath::markHistoryDay(stats.readingHistoryAnchorDay, stats.readingHistoryBits, anchor - 3);
  ReadingStatsMath::markHistoryDay(stats.readingHistoryAnchorDay, stats.readingHistoryBits, anchor - 1);
  ReadingStatsMath::markHistoryDay(stats.readingHistoryAnchorDay, stats.readingHistoryBits, anchor);
  stats.longestReadingStreak = ReadingStatsMath::longestStreak(stats.readingHistoryBits);

  EXPECT_EQ(stats.longestReadingStreak, 3U);
  EXPECT_EQ(ReadingStatsMath::displayedCurrentStreak(stats, 20260829), 2U);
  EXPECT_EQ(ReadingStatsMath::displayedCurrentStreak(stats, 20260830), 2U);
  EXPECT_EQ(ReadingStatsMath::displayedCurrentStreak(stats, 20260831), 0U);
}

TEST(ReadingStatsLayout, FitsX4X4ProAndX3PortraitViewports) {
  struct Viewport {
    int width;
    int height;
    int hints;
  };
  constexpr Viewport viewports[] = {{480, 800, 40}, {480, 800, 0}, {528, 792, 40}};
  for (const auto viewport : viewports) {
    const auto stats = makeReadingStatsScreenLayout(viewport.width, viewport.height, 60, viewport.hints);
    EXPECT_GE(stats.currentBook.x, 0);
    EXPECT_GE(stats.currentBook.width / stats.columns, 140);
    EXPECT_GE(stats.currentBook.height, 300);
    EXPECT_GT(stats.thisDevice.height, 250);
    EXPECT_LE(stats.currentBook.right(), viewport.width);
    EXPECT_LE(stats.thisDevice.right(), viewport.width);
    EXPECT_LE(stats.thisDevice.bottom(), viewport.height - viewport.hints);

    const auto dashboard = makeReadingDashboardLayout(0, 48, viewport.width, 400);
    EXPECT_GE(dashboard.summary.width, 230);
    EXPECT_GE(dashboard.cover.height, 290);
    EXPECT_GE(dashboard.footer.height, 70);
    EXPECT_LE(dashboard.footer.right(), viewport.width);
    EXPECT_LE(dashboard.footer.bottom(), 448);
  }
}
