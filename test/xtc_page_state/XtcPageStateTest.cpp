#include <gtest/gtest.h>

#include "XtcPageState.h"

TEST(XtcPageState, PreservesEndOfBookSentinelAndBounds) {
  XtcPageState state;
  state.initialize(0);

  EXPECT_FALSE(state.turn(false, 3));
  EXPECT_TRUE(state.turn(true, 3));
  EXPECT_EQ(state.requestedPage(), 1U);
  EXPECT_TRUE(state.skip(20, 3));
  EXPECT_EQ(state.requestedPage(), 3U);
  EXPECT_TRUE(state.atEnd(3));
  EXPECT_FALSE(state.turn(true, 3));

  state.returnFromEnd(3);
  EXPECT_EQ(state.requestedPage(), 2U);
  EXPECT_TRUE(state.skip(-20, 3));
  EXPECT_EQ(state.requestedPage(), 0U);
}

TEST(XtcPageState, PublishesVisiblePageOnlyAfterRenderCompletes) {
  XtcPageState state;
  state.initialize(4);

  const uint32_t renderSnapshot = state.requestedPage();
  ASSERT_TRUE(state.turn(true, 10));

  EXPECT_EQ(renderSnapshot, 4U);
  EXPECT_EQ(state.requestedPage(), 5U);
  EXPECT_EQ(state.visiblePage(), 4U);

  state.markVisible(renderSnapshot);
  EXPECT_EQ(state.visiblePage(), 4U);
  state.markVisible(state.requestedPage());
  EXPECT_EQ(state.visiblePage(), 5U);
}

TEST(XtcPageState, HandlesEmptyBookWithoutUnsignedUnderflow) {
  XtcPageState state;
  state.initialize(9);

  state.returnFromEnd(0);
  EXPECT_EQ(state.requestedPage(), 0U);
  EXPECT_TRUE(state.atEnd(0));
  EXPECT_FALSE(state.turn(true, 0));
}
