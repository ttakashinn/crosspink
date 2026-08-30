#include <gtest/gtest.h>

#include "BoundedPageTurnQueue.h"

TEST(BoundedPageTurnQueue, PreservesRepeatedTurns) {
  BoundedPageTurnQueue queue;
  queue.push(true);
  queue.push(true);
  queue.push(true);

  EXPECT_EQ(queue.pending(), 3);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_TRUE(queue.empty());
}

TEST(BoundedPageTurnQueue, CapsPendingTurnsWithoutOverflow) {
  BoundedPageTurnQueue queue;
  for (int i = 0; i < 20; ++i) queue.push(false);

  EXPECT_EQ(queue.pending(), -BoundedPageTurnQueue::MAX_PENDING_TURNS);
  for (int i = 0; i < BoundedPageTurnQueue::MAX_PENDING_TURNS; ++i) EXPECT_EQ(queue.pop(), -1);
  EXPECT_TRUE(queue.empty());
}

TEST(BoundedPageTurnQueue, OppositeTurnsCancelPendingIntent) {
  BoundedPageTurnQueue queue;
  queue.push(true);
  queue.push(true);
  queue.push(false);

  EXPECT_EQ(queue.pending(), 1);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_TRUE(queue.empty());
}

TEST(BoundedPageTurnQueue, ClearDropsAllPendingTurns) {
  BoundedPageTurnQueue queue;
  queue.push(true);
  queue.push(true);
  queue.clear();

  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.pop(), 0);
}
