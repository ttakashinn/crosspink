#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "TxtLineBreak.h"

namespace {

auto byteWidth = [](const char*, const size_t length) { return static_cast<int>(length); };

}  // namespace

TEST(TxtLineBreak, KeepsAWholeLineThatFits) {
  constexpr char text[] = "short line";
  const auto result = txt_layout::nextLine(text, std::strlen(text), 20, byteWidth);
  EXPECT_EQ(result.length, std::strlen(text));
  EXPECT_EQ(result.consumed, std::strlen(text));
}

TEST(TxtLineBreak, WrapsAtTheLastSpaceThatFitsAndConsumesIt) {
  constexpr char text[] = "alpha beta gamma";
  const auto result = txt_layout::nextLine(text, std::strlen(text), 10, byteWidth);
  EXPECT_EQ(std::string(text, result.length), "alpha beta");
  EXPECT_EQ(result.consumed, 11U);
}

TEST(TxtLineBreak, NeverSplitsAVietnameseUtf8Codepoint) {
  constexpr char text[] = "áéí";
  const auto result = txt_layout::nextLine(text, std::strlen(text), 3, byteWidth);
  EXPECT_EQ(std::string(text, result.length), "á");
  EXPECT_EQ(result.length, 2U);
  EXPECT_EQ(result.consumed, 2U);
}

TEST(TxtLineBreak, MakesProgressWhenOneCodepointIsWiderThanTheViewport) {
  constexpr char text[] = "↔rest";
  const auto result = txt_layout::nextLine(text, std::strlen(text), 1, byteWidth);
  EXPECT_EQ(std::string(text, result.length), "↔");
  EXPECT_EQ(result.length, 3U);
  EXPECT_EQ(result.consumed, 3U);
}

TEST(TxtLineBreak, TrimsAnIncompleteUtf8TailFromANonFinalReadChunk) {
  const std::string chunk = std::string("abc") + "\xE2\x86";
  EXPECT_EQ(txt_layout::safeReadChunkLength(chunk.data(), chunk.size(), false), 3U);
  EXPECT_EQ(txt_layout::safeReadChunkLength("\xE2", 1, false), 0U);
  EXPECT_EQ(txt_layout::safeReadChunkLength(chunk.data(), chunk.size(), true), chunk.size());
}

TEST(TxtLineBreak, RecognizesLfCrLfAndStandaloneCr) {
  const auto lf = txt_layout::scanLogicalLine("line\nnext", 9, false);
  EXPECT_EQ(lf.contentLength, 4U);
  EXPECT_EQ(lf.delimiterLength, 1U);
  EXPECT_TRUE(lf.complete);

  const auto crlf = txt_layout::scanLogicalLine("line\r\nnext", 10, false);
  EXPECT_EQ(crlf.contentLength, 4U);
  EXPECT_EQ(crlf.delimiterLength, 2U);
  EXPECT_TRUE(crlf.complete);

  const auto cr = txt_layout::scanLogicalLine("line\r", 5, false);
  EXPECT_EQ(cr.contentLength, 4U);
  EXPECT_EQ(cr.delimiterLength, 1U);
  EXPECT_TRUE(cr.complete);
}

TEST(TxtLineBreak, MarksOnlyAnEofTailAsACompleteLogicalLine) {
  const auto partial = txt_layout::scanLogicalLine("tail", 4, false);
  EXPECT_EQ(partial.contentLength, 4U);
  EXPECT_EQ(partial.delimiterLength, 0U);
  EXPECT_FALSE(partial.complete);

  const auto final = txt_layout::scanLogicalLine("tail", 4, true);
  EXPECT_EQ(final.contentLength, 4U);
  EXPECT_EQ(final.delimiterLength, 0U);
  EXPECT_TRUE(final.complete);
}
