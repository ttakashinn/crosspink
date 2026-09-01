#include <gtest/gtest.h>

#include "ButtonHintSafeArea.h"

using button_hints::Orientation;

TEST(ButtonHintSafeArea, PortraitReservesThePhysicalFrontButtonEdge) {
  const auto portrait = button_hints::safeAreaInsets(Orientation::Portrait, 40);
  EXPECT_EQ(portrait.top, 0);
  EXPECT_EQ(portrait.right, 0);
  EXPECT_EQ(portrait.bottom, 40);
  EXPECT_EQ(portrait.left, 0);

  const auto inverted = button_hints::safeAreaInsets(Orientation::PortraitInverted, 40);
  EXPECT_EQ(inverted.top, 40);
  EXPECT_EQ(inverted.right, 0);
  EXPECT_EQ(inverted.bottom, 0);
  EXPECT_EQ(inverted.left, 0);
}

TEST(ButtonHintSafeArea, BothLandscapeDirectionsReserveSymmetricSideGutters) {
  for (const auto orientation : {Orientation::LandscapeClockwise, Orientation::LandscapeCounterClockwise}) {
    const auto insets = button_hints::safeAreaInsets(orientation, 40);
    EXPECT_EQ(insets.top, 0);
    EXPECT_EQ(insets.right, 40);
    EXPECT_EQ(insets.bottom, 0);
    EXPECT_EQ(insets.left, 40);
  }
}

TEST(ButtonHintSafeArea, TouchOrDisabledHintsDoNotReserveSpace) {
  for (const auto orientation : {Orientation::Portrait, Orientation::LandscapeClockwise, Orientation::PortraitInverted,
                                 Orientation::LandscapeCounterClockwise}) {
    const auto insets = button_hints::safeAreaInsets(orientation, 0);
    EXPECT_EQ(insets.top, 0);
    EXPECT_EQ(insets.right, 0);
    EXPECT_EQ(insets.bottom, 0);
    EXPECT_EQ(insets.left, 0);
  }
}
