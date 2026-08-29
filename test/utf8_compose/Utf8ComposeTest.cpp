#include <gtest/gtest.h>

#include <string>

#include "Utf8.h"

namespace {

// Helpers to build NFD / expected byte sequences explicitly so the test does not
// depend on the encoding of this source file.
const std::string kCombGrave = "\xCC\x80";     // U+0300 COMBINING GRAVE ACCENT
const std::string kCombAcute = "\xCC\x81";     // U+0301 COMBINING ACUTE ACCENT
const std::string kCombCirc = "\xCC\x82";      // U+0302 COMBINING CIRCUMFLEX ACCENT
const std::string kCombHook = "\xCC\x89";      // U+0309 COMBINING HOOK ABOVE
const std::string kCombHorn = "\xCC\x9B";      // U+031B COMBINING HORN
const std::string kCombDotBelow = "\xCC\xA3";  // U+0323 COMBINING DOT BELOW

}  // namespace

// ASCII and already-precomposed (NFC) text must pass through untouched (fast path).
TEST(Utf8ComposeNfc, PassesThroughAsciiAndNfc) {
  EXPECT_EQ(utf8ComposeNfc(""), "");
  EXPECT_EQ(utf8ComposeNfc("hello world"), "hello world");
  EXPECT_EQ(utf8ComposeNfc("caf\xC3\xA9"), "caf\xC3\xA9");  // é already U+00E9
}

// Single combining mark composes onto its base letter.
TEST(Utf8ComposeNfc, ComposesSingleMark) {
  EXPECT_EQ(utf8ComposeNfc("e" + kCombAcute), "\xC3\xA9");  // e + ́  -> é  (U+00E9)
  EXPECT_EQ(utf8ComposeNfc("a" + kCombGrave), "\xC3\xA0");  // a + ̀  -> à  (U+00E0)
}

// Vietnamese letters carry two stacked marks; composition must accumulate them
// onto the intermediate precomposed character (this is the crux of the feature).
TEST(Utf8ComposeNfc, ComposesStackedVietnameseMarks) {
  // a + circumflex + acute -> ấ (U+1EA5)
  EXPECT_EQ(utf8ComposeNfc("a" + kCombCirc + kCombAcute), "\xE1\xBA\xA5");
  // a + dot-below + circumflex (canonical order) -> ậ (U+1EAD)
  EXPECT_EQ(utf8ComposeNfc("a" + kCombDotBelow + kCombCirc), "\xE1\xBA\xAD");
}

TEST(Utf8ComposeNfc, RepairsCommonOutOfOrderVietnameseMarks) {
  // Some EPUB generators put the tone before the vowel-shape mark. Unicode NFC
  // preserves that malformed same-class order, but the reader can safely repair
  // the bounded Vietnamese combinations to keep diacritics attached.
  EXPECT_EQ(utf8ComposeNfc("a" + kCombAcute + kCombCirc), "\xE1\xBA\xA5");     // ấ
  EXPECT_EQ(utf8ComposeNfc("o" + kCombHook + kCombHorn), "\xE1\xBB\x9F");      // ở
  EXPECT_EQ(utf8ComposeNfc("u" + kCombDotBelow + kCombHorn), "\xE1\xBB\xB1");  // ự
}

// A combining mark with no composition for its base is left unchanged, and the
// base is preserved.
TEST(Utf8ComposeNfc, LeavesUncomposableMarksIntact) {
  const std::string in = "q" + kCombAcute;  // no precomposed "q with acute"
  EXPECT_EQ(utf8ComposeNfc(in), in);
}

// A leading combining mark (no preceding base) is emitted unchanged.
TEST(Utf8ComposeNfc, HandlesLeadingMark) { EXPECT_EQ(utf8ComposeNfc(kCombAcute), kCombAcute); }

// Marks embedded in a longer word compose while surrounding text is preserved.
TEST(Utf8ComposeNfc, ComposesWithinWord) {
  // "Ti" + e+circ+acute + "ng" -> "Tiếng"
  EXPECT_EQ(utf8ComposeNfc("Ti" + std::string("e") + kCombCirc + kCombAcute + "ng"), "Ti\xE1\xBA\xBFng");
}

TEST(Utf8Validation, AcceptsVietnameseAndValidFourByteCodepoints) {
  EXPECT_TRUE(utf8IsValid(""));
  EXPECT_TRUE(utf8IsValid("Văn Nhân Số"));
  EXPECT_TRUE(utf8IsValid("\xF0\x9F\x93\x9A"));  // U+1F4DA BOOKS
}

TEST(Utf8Validation, RejectsMalformedAndNonScalarSequences) {
  EXPECT_FALSE(utf8IsValid("\xC0\xAF"));          // overlong '/'
  EXPECT_FALSE(utf8IsValid("\xE2\x82"));          // truncated U+20AC
  EXPECT_FALSE(utf8IsValid("\xED\xA0\x80"));      // surrogate U+D800
  EXPECT_FALSE(utf8IsValid("\xF4\x90\x80\x80"));  // beyond U+10FFFF
  EXPECT_FALSE(utf8IsValid("\x80"));              // stray continuation
}
