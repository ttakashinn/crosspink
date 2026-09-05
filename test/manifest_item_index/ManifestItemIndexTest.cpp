#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ManifestItemIndex.h"

TEST(ManifestItemIndex, FindsSameHashAndLengthCandidatesAcrossChunks) {
  ManifestItemIndex index(ManifestItemIndex::DEFAULT_SLAB_BYTES, 2 * ManifestItemIndex::DEFAULT_SLAB_BYTES);
  for (uint32_t i = 0; i < 640; ++i) {
    const uint32_t hash = i == 17 || i == 18 || i == 517 ? 42 : 1000 + i;
    const uint16_t length = i == 18 ? 7 : 6;
    ASSERT_TRUE(index.append({hash, length, i * 10}));
  }

  ASSERT_EQ(index.chunkCount(), 2U);
  index.sort();

  std::vector<uint32_t> offsets;
  const auto result = index.visitCandidates(42, 6, [&](const uint32_t offset) {
    offsets.push_back(offset);
    return ManifestItemIndex::CandidateResult::Continue;
  });

  EXPECT_EQ(result, ManifestItemIndex::CandidateResult::Continue);
  EXPECT_EQ(offsets, (std::vector<uint32_t>{170, 5170}));
}

TEST(ManifestItemIndex, StopsAtFirstConfirmedCandidate) {
  ManifestItemIndex index;
  ASSERT_TRUE(index.append({5, 3, 100}));
  ASSERT_TRUE(index.append({5, 3, 200}));
  index.sort();

  std::vector<uint32_t> offsets;
  const auto result = index.visitCandidates(5, 3, [&](const uint32_t offset) {
    offsets.push_back(offset);
    return offset == 200 ? ManifestItemIndex::CandidateResult::Found : ManifestItemIndex::CandidateResult::Continue;
  });

  EXPECT_EQ(result, ManifestItemIndex::CandidateResult::Found);
  EXPECT_EQ(offsets, (std::vector<uint32_t>{100, 200}));
}

TEST(ManifestItemIndex, AllocationFailurePreservesIndexedPrefix) {
  ManifestItemIndex index(ManifestItemIndex::DEFAULT_SLAB_BYTES, ManifestItemIndex::DEFAULT_SLAB_BYTES);
  for (uint32_t i = 0; i < 320; ++i) ASSERT_TRUE(index.append({i, 4, i}));
  EXPECT_FALSE(index.append({999, 4, 999}));
  EXPECT_EQ(index.size(), 320U);
  EXPECT_EQ(index.chunkCount(), 1U);
  EXPECT_EQ(index.memoryCapacity(), ManifestItemIndex::DEFAULT_SLAB_BYTES);

  index.sort();
  const auto result = index.visitCandidates(100, 4, [](const uint32_t offset) {
    return offset == 100 ? ManifestItemIndex::CandidateResult::Found : ManifestItemIndex::CandidateResult::Error;
  });
  EXPECT_EQ(result, ManifestItemIndex::CandidateResult::Found);
}
