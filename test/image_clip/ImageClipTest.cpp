#include <cassert>
#include <climits>

#include "ImageClip.h"

int main() {
  {
    const auto clip = calculateImageClip(10, 20, 100, 80, 200, 200);
    assert(clip.colStart == 0 && clip.colEnd == 100);
    assert(clip.rowStart == 0 && clip.rowEnd == 80);
  }
  {
    const auto clip = calculateImageClip(-12, -7, 100, 80, 200, 200);
    assert(clip.colStart == 12 && clip.colEnd == 100);
    assert(clip.rowStart == 7 && clip.rowEnd == 80);
  }
  {
    const auto clip = calculateImageClip(180, 190, 100, 80, 200, 200);
    assert(clip.colStart == 0 && clip.colEnd == 20);
    assert(clip.rowStart == 0 && clip.rowEnd == 10);
  }
  assert(calculateImageClip(200, 0, 10, 10, 200, 200).empty());
  assert(calculateImageClip(0, 200, 10, 10, 200, 200).empty());
  assert(calculateImageClip(INT_MIN, INT_MIN, 10, 10, 200, 200).empty());
  assert(calculateImageClip(0, 0, -1, 10, 200, 200).empty());
  return 0;
}
