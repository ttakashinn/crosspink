#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "ImageDimsProbe.h"

TEST(ImageDimsProbe, IdentifiesExtensionlessPngByContent) {
  const std::array<uint8_t, 24> png = {
      0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
      'I',  'H', 'D', 'R', 0x00, 0x00, 0x01, 0x40, 0x00, 0x00, 0x00, 0xF0,
  };
  ImageDimsProbe probe;
  probe.write(png.data(), png.size());

  ImageDimensions dimensions{};
  ASSERT_TRUE(probe.getDimensions(dimensions));
  EXPECT_EQ(dimensions.width, 320);
  EXPECT_EQ(dimensions.height, 240);
  EXPECT_EQ(probe.getFormat(), ImageDimsProbe::Format::Png);
}

TEST(ImageDimsProbe, IdentifiesExtensionlessJpegByContent) {
  const std::array<uint8_t, 15> jpeg = {
      0xFF, 0xD8,                          // SOI
      0xFF, 0xE0, 0x00, 0x04, 0x00, 0x00,  // APP0
      0xFF, 0xC0, 0x00, 0x07, 0x08, 0x00, 0x78,
  };
  const std::array<uint8_t, 2> width = {0x00, 0xA0};
  ImageDimsProbe probe;
  probe.write(jpeg.data(), jpeg.size());
  probe.write(width.data(), width.size());

  ImageDimensions dimensions{};
  ASSERT_TRUE(probe.getDimensions(dimensions));
  EXPECT_EQ(dimensions.width, 160);
  EXPECT_EQ(dimensions.height, 120);
  EXPECT_EQ(probe.getFormat(), ImageDimsProbe::Format::Jpeg);
}

TEST(ImageDimsProbe, DoesNotExposeAFormatForInvalidPayload) {
  const std::array<uint8_t, 6> gif = {'G', 'I', 'F', '8', '9', 'a'};
  ImageDimsProbe probe;
  probe.write(gif.data(), gif.size());

  ImageDimensions dimensions{};
  EXPECT_FALSE(probe.getDimensions(dimensions));
  EXPECT_EQ(probe.getFormat(), ImageDimsProbe::Format::Unknown);
}
