#include <gtest/gtest.h>

#include "TextRasterPolicy.h"

using text_raster::BwPurpose;

TEST(TextRasterPolicyTest, GrayscaleBaseKeepsEveryCoveredEdgeSample) {
  EXPECT_FALSE(text_raster::paintsBw(0, BwPurpose::GrayscaleBase));
  EXPECT_TRUE(text_raster::paintsBw(1, BwPurpose::GrayscaleBase));
  EXPECT_TRUE(text_raster::paintsBw(2, BwPurpose::GrayscaleBase));
  EXPECT_TRUE(text_raster::paintsBw(3, BwPurpose::GrayscaleBase));
}

TEST(TextRasterPolicyTest, CrispMonochromeDropsOnlyWeakestFringe) {
  EXPECT_FALSE(text_raster::paintsBw(0, BwPurpose::CrispMonochrome));
  EXPECT_FALSE(text_raster::paintsBw(1, BwPurpose::CrispMonochrome));
  EXPECT_TRUE(text_raster::paintsBw(2, BwPurpose::CrispMonochrome));
  EXPECT_TRUE(text_raster::paintsBw(3, BwPurpose::CrispMonochrome));
}

TEST(TextRasterPolicyTest, GrayscaleSelectorsPreserveFourLevelMapping) {
  EXPECT_EQ(text_raster::grayPlanes(0), text_raster::None);
  EXPECT_EQ(text_raster::grayPlanes(1), text_raster::Msb);
  EXPECT_EQ(text_raster::grayPlanes(2), text_raster::Lsb | text_raster::Msb);
  EXPECT_EQ(text_raster::grayPlanes(3), text_raster::None);
}
