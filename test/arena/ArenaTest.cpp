#include <Arena.h>
#include <ArenaVector.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

TEST(Arena, RejectsInvalidConfigurationAndArithmeticOverflow) {
  Arena arena;
  EXPECT_FALSE(arena.init(0, 1024));
  ASSERT_TRUE(arena.init(128, 256));
  EXPECT_EQ(arena.alloc(8, 0), nullptr);
  EXPECT_EQ(arena.alloc(8, 3), nullptr);
  EXPECT_EQ(arena.alloc(std::numeric_limits<size_t>::max(), 8), nullptr);
}

TEST(Arena, HonorsAlignmentGrowthBudgetAndHighWater) {
  Arena arena;
  ASSERT_TRUE(arena.init(64, 128));
  auto* byte = static_cast<uint8_t*>(arena.alloc(1, 1));
  auto* aligned = static_cast<uint64_t*>(arena.alloc(sizeof(uint64_t), alignof(uint64_t)));
  ASSERT_NE(byte, nullptr);
  ASSERT_NE(aligned, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned) % alignof(uint64_t), 0U);
  EXPECT_GE(arena.highWater(), sizeof(uint64_t) + 1);
  EXPECT_NE(arena.alloc(56, 1), nullptr);
  EXPECT_EQ(arena.capacity(), 128U);
  EXPECT_EQ(arena.alloc(65, 1), nullptr);
}

TEST(Arena, RestoreFreesGrowthAndRejectsForeignCheckpoint) {
  Arena arena;
  Arena other;
  ASSERT_TRUE(arena.init(32, 96));
  ASSERT_TRUE(other.init(32, 32));
  ASSERT_NE(arena.alloc(24, 1), nullptr);
  const Arena::Checkpoint checkpoint = arena.save();
  ASSERT_NE(arena.alloc(24, 1), nullptr);
  EXPECT_EQ(arena.capacity(), 64U);
  EXPECT_FALSE(arena.restore(other.save()));
  EXPECT_TRUE(arena.restore(checkpoint));
  EXPECT_EQ(arena.capacity(), 32U);
  EXPECT_EQ(arena.used(), 24U);
}

TEST(ArenaVector, GrowsWithinBudgetAndPreservesValues) {
  Arena arena;
  ASSERT_TRUE(arena.init(128, 512));
  ArenaVector<uint32_t> values(arena);
  for (uint32_t i = 0; i < 20; ++i) ASSERT_TRUE(values.push_back(i * 3));
  ASSERT_EQ(values.size(), 20U);
  for (uint32_t i = 0; i < 20; ++i) EXPECT_EQ(values.begin()[i], i * 3);
  values.pop_back();
  EXPECT_EQ(values.back(), 54U);
}

TEST(ArenaVector, ResizeAndInsertPreserveOrder) {
  Arena arena;
  ASSERT_TRUE(arena.init(64, 512));
  ArenaVector<uint16_t> values(arena);

  ASSERT_TRUE(values.resize(3));
  EXPECT_EQ(values[0], 0);
  values[0] = 10;
  values[1] = 30;
  values[2] = 40;
  ASSERT_TRUE(values.insert(1, 20));

  ASSERT_EQ(values.size(), 4u);
  EXPECT_EQ(values[0], 10);
  EXPECT_EQ(values[1], 20);
  EXPECT_EQ(values[2], 30);
  EXPECT_EQ(values[3], 40);
}

TEST(Arena, ArrayCountOverflowIsRejected) {
  Arena arena;
  ASSERT_TRUE(arena.init(128, 128));
  EXPECT_EQ(arenaNewArray<uint64_t>(arena, std::numeric_limits<size_t>::max()), nullptr);
}
