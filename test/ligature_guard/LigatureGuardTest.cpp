#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "EpdFont.h"
#include "EpdFontData.h"
#include "Utf8.h"

namespace {

// getLigature() binary-searches this table, so entries stay sorted by pair.
const EpdLigaturePair kPairs[] = {
    {(0x0066u << 16) | 0x0066u, 0xFB00u},  // f + f -> ff
    {(0x0627u << 16) | 0x0653u, 0x0622u},  // alef + maddah -> alef with maddah
    {(0xFEDFu << 16) | 0xFE8Eu, 0xFEFBu},  // shaped lam + alef -> lam-alef
};

EpdFont makeFont(EpdFontData& data) {
  std::memset(&data, 0, sizeof(data));
  data.ligaturePairs = kPairs;
  data.ligaturePairCount = sizeof(kPairs) / sizeof(kPairs[0]);
  return EpdFont(&data);
}

}  // namespace

TEST(LigatureGuard, KeepsLatinLigatures) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0x0066, 0x0066), 0xFB00u);
}

TEST(LigatureGuard, KeepsBaseArabicComposition) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0x0627, 0x0653), 0x0622u);
}

TEST(LigatureGuard, RejectsArabicPresentationFormPair) {
  EpdFontData data;
  const EpdFont font = makeFont(data);
  EXPECT_EQ(font.getLigature(0xFEDF, 0xFE8E), 0u);
}

TEST(LigatureGuard, DoesNotConsumeAlreadyShapedArabic) {
  EpdFontData data;
  const EpdFont font = makeFont(data);

  std::string tail;
  utf8AppendCodepoint(0xFE8E, tail);
  const char* cursor = tail.c_str();

  EXPECT_EQ(font.applyLigatures(0xFEDF, cursor), 0xFEDFu);
  EXPECT_EQ(cursor, tail.c_str());
}
