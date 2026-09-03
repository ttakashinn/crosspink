#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "Bitmap.h"
#include "HomeCoverCachePolicy.h"

namespace {

std::vector<uint8_t> makeOneBitBmp(const int width, const int height, const bool includePixels) {
  const uint32_t rowBytes = (static_cast<uint32_t>(width) + 31U) / 32U * 4U;
  const uint32_t pixelBytes = rowBytes * static_cast<uint32_t>(height);
  BmpHeader header{};
  header.fileHeader.bfType = 0x4D42;
  header.fileHeader.bfSize = sizeof(BmpHeader) + pixelBytes;
  header.fileHeader.bfOffBits = sizeof(BmpHeader);
  header.infoHeader.biSize = sizeof(header.infoHeader);
  header.infoHeader.biWidth = width;
  header.infoHeader.biHeight = -height;
  header.infoHeader.biPlanes = 1;
  header.infoHeader.biBitCount = 1;
  header.infoHeader.biSizeImage = pixelBytes;
  header.infoHeader.biClrUsed = 2;
  header.colors[1].rgbBlue = 255;
  header.colors[1].rgbGreen = 255;
  header.colors[1].rgbRed = 255;

  std::vector<uint8_t> bytes(sizeof(header) + (includePixels ? pixelBytes : 0));
  std::memcpy(bytes.data(), &header, sizeof(header));
  return bytes;
}

}  // namespace

TEST(HomeCoverBitmapTest, AcceptsCompletePixelData) {
  HalFile file(makeOneBitBmp(8, 2, true));
  Bitmap bitmap(file);

  EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::Ok);
}

TEST(HomeCoverBitmapTest, RejectsHeaderOnlyThumbnail) {
  HalFile file(makeOneBitBmp(8, 2, false));
  Bitmap bitmap(file);

  EXPECT_EQ(bitmap.parseHeaders(), BmpReaderError::ShortReadRow);
}

TEST(HomeCoverCachePolicyTest, DoesNotCacheTransientFailureForExpectedCover) {
  EXPECT_FALSE(shouldCacheHomeCover(true, false));
}

TEST(HomeCoverCachePolicyTest, CachesRenderedCoverAndIntentionalPlaceholder) {
  EXPECT_TRUE(shouldCacheHomeCover(true, true));
  EXPECT_TRUE(shouldCacheHomeCover(false, false));
}
