#include <gtest/gtest.h>

#include "ReaderViewportLayout.h"

TEST(ReaderViewportLayout, KeepsSymmetricReadingMarginWhenStatusBarIsHidden) {
  EXPECT_EQ(5, reader_viewport::bottomInset(5, 0));
  EXPECT_EQ(40, reader_viewport::bottomInset(40, 0));
}

TEST(ReaderViewportLayout, KeepsReadingMarginSeparateFromVisibleStatusBar) {
  EXPECT_EQ(24, reader_viewport::bottomInset(5, 19));
  EXPECT_EQ(59, reader_viewport::bottomInset(40, 19));
}

TEST(ReaderViewportLayout, ReservesAnExtraTextLaneForAutomaticPageTurn) {
  EXPECT_EQ(29, reader_viewport::bottomInset(5, 5, 19));
}
