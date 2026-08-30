#include <gtest/gtest.h>

#include "DictionaryQuery.h"

TEST(DictionaryQuery, NormalizesVietnameseCaseCompositionAndBoundaries) {
  EXPECT_EQ(DictionaryQuery::clean("  “TIẾNG!”  "), "tiếng");
  EXPECT_EQ(DictionaryQuery::clean("TIE\xCC\x82\xCC\x81NG"), "tiếng");
  EXPECT_EQ(DictionaryQuery::clean("L'ESPRIT"), "l'esprit");
  EXPECT_EQ(DictionaryQuery::clean("..."), "");
}

TEST(DictionaryQuery, KeepsUnrelatedScriptsAndInternalPunctuation) {
  EXPECT_EQ(DictionaryQuery::clean("「中文」"), "中文");
  EXPECT_EQ(DictionaryQuery::clean("co-op"), "co-op");
}
