#pragma once

// A placeholder is final only when the book has no cover path. If a cover is
// expected but opening or decoding it failed, keeping the region uncached lets
// Home retry instead of pinning a blank tile for the activity's lifetime.
constexpr bool shouldCacheHomeCover(const bool expectsCover, const bool renderedCover) {
  return !expectsCover || renderedCover;
}
