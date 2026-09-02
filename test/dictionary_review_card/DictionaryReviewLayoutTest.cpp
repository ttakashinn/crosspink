#include <gtest/gtest.h>

#include "features/dictionary_review/DictionaryReviewLayout.h"

TEST(DictionaryReviewLayoutTest, LastPopulatedSectionUsesAllRemainingLines) {
  EXPECT_EQ(dictionary_review::availableLinesForSection(200, 770, 24, 30, 14, 0), 18);
}

TEST(DictionaryReviewLayoutTest, ReservesOnlyMinimumSpaceForLaterPopulatedSections) {
  EXPECT_EQ(dictionary_review::availableLinesForSection(200, 770, 24, 30, 14, 2), 13);
}

TEST(DictionaryReviewLayoutTest, ReturnsNoLinesWhenSectionCannotFit) {
  EXPECT_EQ(dictionary_review::availableLinesForSection(740, 770, 24, 30, 14, 1), 0);
  EXPECT_EQ(dictionary_review::availableLinesForSection(200, 770, 24, 0, 14, 0), 0);
}
