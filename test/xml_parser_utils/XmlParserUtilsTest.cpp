#include <gtest/gtest.h>

#include "XmlParserUtils.h"

TEST(XmlParserUtils, MatchesLocalNamesRegardlessOfPrefix) {
  EXPECT_TRUE(xmlLocalNameEquals("package", "package"));
  EXPECT_TRUE(xmlLocalNameEquals("ns0:package", "package"));
  EXPECT_FALSE(xmlLocalNameEquals("ns0:metadata", "package"));
}

TEST(XmlParserUtils, MatchesExactWhitespaceSeparatedTokens) {
  EXPECT_TRUE(hasSpaceSeparatedToken("page-list hidden", "page-list"));
  EXPECT_TRUE(hasSpaceSeparatedToken("doc-noteref\tdoc-pagebreak", "doc-pagebreak"));
  EXPECT_FALSE(hasSpaceSeparatedToken("not-page-list", "page-list"));
  EXPECT_FALSE(hasSpaceSeparatedToken("pagebreakish", "pagebreak"));
}
