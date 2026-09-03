#include <gtest/gtest.h>

#include "components/InputReleaseGate.h"

TEST(InputReleaseGate, RejectsEventsUntilSurfaceIsReadyAndAReleaseFrameWasConsumed) {
  InputReleaseGate gate;

  EXPECT_FALSE(gate.acceptsInput(false, true));
  EXPECT_FALSE(gate.acceptsInput(true, true));
  EXPECT_FALSE(gate.acceptsInput(true, false));
  EXPECT_TRUE(gate.acceptsInput(true, false));
}

TEST(InputReleaseGate, ResetRequiresAnotherCleanFrame) {
  InputReleaseGate gate;

  EXPECT_FALSE(gate.acceptsInput(true, false));
  EXPECT_TRUE(gate.acceptsInput(true, false));

  gate.reset();

  EXPECT_FALSE(gate.acceptsInput(true, false));
  EXPECT_TRUE(gate.acceptsInput(true, false));
}
