#include "ImageToFramebufferDecoder.h"

bool ImageToFramebufferDecoder::validateAndStoreDimensions(const int64_t width, const int64_t height,
                                                           ImageDimensions& out, const char*) {
  if (width <= 0 || height <= 0 || width > MAX_SOURCE_DIMENSION || height > MAX_SOURCE_DIMENSION ||
      width * height > MAX_SOURCE_PIXELS) {
    return false;
  }
  out.width = static_cast<int16_t>(width);
  out.height = static_cast<int16_t>(height);
  return true;
}
