#include <gtest/gtest.h>

#include "ReaderInteraction.h"

using reader_interaction::PostVisibleIdleGuard;
using reader_interaction::TurnTelemetry;

TEST(ReadingAnchor, UsesTheMiddleOfTheVisibleTextRange) {
  EXPECT_EQ(reader_interaction::readingAnchorAtPageCenter(100U, 300U), 200U);
  EXPECT_EQ(reader_interaction::readingAnchorAtPageCenter(101U, 200U), 150U);
}

TEST(ReadingAnchor, FallsBackToThePageStartWhenTheNextBoundaryIsUnavailable) {
  EXPECT_EQ(reader_interaction::readingAnchorAtPageCenter(500U, std::nullopt), 500U);
  EXPECT_EQ(reader_interaction::readingAnchorAtPageCenter(500U, 500U), 500U);
  EXPECT_FALSE(reader_interaction::readingAnchorAtPageCenter(std::nullopt, 700U).has_value());
}

TEST(PostVisibleIdleGuard, RequiresAVisiblePageAndAFullQuietWindow) {
  PostVisibleIdleGuard guard;
  EXPECT_FALSE(guard.canRunDeferredWork(5000));
  guard.pageVisible(1000);
  EXPECT_FALSE(guard.canRunDeferredWork(1999));
  EXPECT_TRUE(guard.canRunDeferredWork(2000));
}

TEST(PostVisibleIdleGuard, InputAndEveryVisiblePageRestartTheWindow) {
  PostVisibleIdleGuard guard;
  guard.pageVisible(1000);
  guard.noteInput(1800);
  EXPECT_FALSE(guard.canRunDeferredWork(2799));
  EXPECT_TRUE(guard.canRunDeferredWork(2800));
  guard.pageVisible(3000);
  EXPECT_FALSE(guard.canRunDeferredWork(3999));
  EXPECT_TRUE(guard.canRunDeferredWork(4000));
}

TEST(PostVisibleIdleGuard, HandlesMillisWraparound) {
  PostVisibleIdleGuard guard;
  guard.pageVisible(UINT32_MAX - 499);
  EXPECT_FALSE(guard.canRunDeferredWork(499));
  EXPECT_TRUE(guard.canRunDeferredWork(500));
}

TEST(TurnTelemetry, CorrelatesAllPhasesWithoutAllocation) {
  TurnTelemetry telemetry;
  EXPECT_EQ(telemetry.input(100, 1, 3, 8), 1U);
  telemetry.queued(105, 2);
  telemetry.queueDepth(3);
  telemetry.renderBegin(130);
  const auto first = telemetry.visible(190, 3, 9);
  EXPECT_EQ(first.sequence, 1U);
  EXPECT_EQ(first.direction, 1);
  EXPECT_EQ(first.queueDepth, 3);
  EXPECT_EQ(first.inputAtMs, 100U);
  EXPECT_EQ(first.queuedAtMs, 105U);
  EXPECT_EQ(first.renderBeginAtMs, 130U);
  EXPECT_EQ(first.visibleAtMs, 190U);
  EXPECT_EQ(first.beforePrimary, 3);
  EXPECT_EQ(first.beforeSecondary, 8);
  EXPECT_EQ(first.afterPrimary, 3);
  EXPECT_EQ(first.afterSecondary, 9);

  EXPECT_EQ(telemetry.input(250, -1), 2U);
  const auto second = telemetry.snapshot();
  EXPECT_EQ(second.sequence, 2U);
  EXPECT_EQ(second.direction, -1);
  EXPECT_EQ(second.visibleAtMs, 0U);
}

TEST(TurnTelemetry, PreservesBurstInputsInFifoOrderAndFirstQueuedTimestamp) {
  TurnTelemetry telemetry;
  telemetry.input(100, 1, 0, 1);
  telemetry.queued(101, 1);
  telemetry.queued(109, 3);
  telemetry.input(110, 1, 0, 2);
  telemetry.queued(111, 2);

  telemetry.renderBegin(130);
  telemetry.renderBegin(140);  // retry/redraw before the first page is visible
  const auto first = telemetry.visible(160, 0, 2);
  telemetry.renderBegin(170);
  const auto second = telemetry.visible(200, 0, 3);

  EXPECT_EQ(first.sequence, 1U);
  EXPECT_EQ(first.renderBeginAtMs, 130U);
  EXPECT_EQ(first.queuedAtMs, 101U);
  EXPECT_EQ(first.queueDepth, 3);
  EXPECT_EQ(second.sequence, 2U);
  EXPECT_EQ(second.inputAtMs, 110U);
  EXPECT_EQ(second.visibleAtMs, 200U);
}

TEST(TurnTelemetry, CancelsOnlyTheNewestNoOpTurn) {
  TurnTelemetry telemetry;
  telemetry.input(100, 1, 0, 1);
  telemetry.queued(101, 1);
  telemetry.input(110, -1, 0, 1);
  telemetry.queued(111, 2);

  telemetry.cancelNewest();
  const auto remaining = telemetry.snapshot();
  EXPECT_EQ(remaining.sequence, 1U);
  EXPECT_EQ(remaining.direction, 1);

  telemetry.renderBegin(130);
  EXPECT_EQ(telemetry.visible(160, 0, 2).sequence, 1U);
  telemetry.cancelNewest();
  EXPECT_EQ(telemetry.snapshot().sequence, 0U);
}

TEST(TurnTelemetry, ClearDropsEveryPendingTurnButKeepsSequenceMonotonic) {
  TurnTelemetry telemetry;
  telemetry.input(100, 1);
  telemetry.input(110, 1);
  telemetry.clear();
  EXPECT_EQ(telemetry.snapshot().sequence, 0U);
  EXPECT_EQ(telemetry.input(120, -1), 3U);
}
