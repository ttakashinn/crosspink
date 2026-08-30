#include <gtest/gtest.h>

#include "activities/reader/VnsReferencePosition.h"

TEST(VnsReferencePositionTest, PositionDoesNotDependOnPagination) {
  const auto a = vns_reference::make(0x12345678, 2, 4096, 1536);
  const auto b = vns_reference::make(0x12345678, 2, 4096, 1536);
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.ordinal, 6U);
}

TEST(VnsReferencePositionTest, TokenRoundTripsAndRejectsWrongVersion) {
  const auto source = vns_reference::make(0x89abcdef, 7, 0, 4321);
  vns_reference::Position decoded;
  EXPECT_TRUE(vns_reference::decode(vns_reference::encode(source), decoded));
  EXPECT_EQ(decoded.contentSignature, source.contentSignature);
  EXPECT_EQ(decoded.spineIndex, source.spineIndex);
  EXPECT_EQ(decoded.visibleTextOffset, source.visibleTextOffset);
  EXPECT_EQ(decoded.ordinal, source.ordinal);
  EXPECT_FALSE(vns_reference::decode("vnspos:2:89abcdef:7:4321:5", decoded));
}

TEST(VnsReferencePositionTest, QrPayloadKeepsTokenAndUtf8BoundaryAtCapacity) {
  const auto position = vns_reference::make(0x12345678, 2, 4096, 1536);
  const std::string token = vns_reference::encode(position);
  const size_t capacity = token.size() + 2U + 5U;
  const auto payload = vns_reference::buildQrPayload("ừừừ", position, capacity);
  ASSERT_LE(payload.size(), capacity);
  ASSERT_GE(payload.size(), token.size() + 2U);
  EXPECT_EQ(payload.substr(payload.size() - token.size()), token);
  EXPECT_EQ(payload.substr(payload.size() - token.size() - 2U, 2), "\n\n");
  const size_t separator = payload.size() - token.size() - 2U;
  EXPECT_EQ(payload.substr(0, separator), "ừ");
}
