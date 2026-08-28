#include <gtest/gtest.h>

#include "ReaderProgressSaveDebouncer.h"

TEST(ReaderProgressSaveDebouncer, BatchesFivePositionChanges) {
  ReaderProgressSaveDebouncer debouncer;
  debouncer.seedPersisted(10, 100, 1000);
  for (uint32_t page = 11; page < 15; ++page) EXPECT_FALSE(debouncer.observe(page, 100, 1000 + page));
  EXPECT_TRUE(debouncer.observe(15, 100, 1100));
}

TEST(ReaderProgressSaveDebouncer, MetadataDoesNotCountAsPageTurnButStillExpires) {
  ReaderProgressSaveDebouncer debouncer;
  debouncer.seedPersisted(10, 100, 500);
  EXPECT_FALSE(debouncer.observe(10, 101, 600));
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_FALSE(debouncer.due(500 + ReaderProgressSaveDebouncer::MAX_SAVE_INTERVAL_MS - 1));
  EXPECT_TRUE(debouncer.due(500 + ReaderProgressSaveDebouncer::MAX_SAVE_INTERVAL_MS));
}

TEST(ReaderProgressSaveDebouncer, StaleAcknowledgementCannotClearNewerPosition) {
  ReaderProgressSaveDebouncer debouncer;
  debouncer.seedPersisted(1, 10, 0);
  EXPECT_FALSE(debouncer.observe(2, 10, 1));
  EXPECT_FALSE(debouncer.markPersisted(1, 10, 2));
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_TRUE(debouncer.markPersisted(2, 10, 3));
  EXPECT_FALSE(debouncer.hasPending());
}

TEST(ReaderProgressSaveDebouncer, UnsignedElapsedTimeSurvivesClockWrap) {
  ReaderProgressSaveDebouncer debouncer;
  constexpr uint32_t start = UINT32_MAX - 1000;
  debouncer.seedPersisted(1, 10, start);
  EXPECT_FALSE(debouncer.observe(2, 10, start + 1));
  EXPECT_TRUE(debouncer.due(start + ReaderProgressSaveDebouncer::MAX_SAVE_INTERVAL_MS));
}

TEST(ReaderProgressSaveDebouncer, FailedAttemptBacksOffWithoutLosingPendingState) {
  ReaderProgressSaveDebouncer debouncer;
  debouncer.seedPersisted(10, 100, 0);
  for (uint32_t page = 11; page <= 15; ++page) {
    debouncer.observe(page, 100, page);
  }
  ASSERT_TRUE(debouncer.due(15));
  debouncer.markAttemptFailed(15);
  EXPECT_TRUE(debouncer.hasPending());
  EXPECT_FALSE(debouncer.due(16));
  EXPECT_TRUE(debouncer.due(15 + ReaderProgressSaveDebouncer::MAX_SAVE_INTERVAL_MS));
}
