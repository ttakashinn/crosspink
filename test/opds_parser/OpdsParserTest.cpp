#include <gtest/gtest.h>

#include <cstring>

#include "OpdsParser.h"

namespace {

TEST(OpdsParser, DefersParserAndEntryStorageUntilBodyArrives) {
  OpdsParser parser;

  EXPECT_FALSE(parser.initialized());

  constexpr char firstByte[] = "<";
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(firstByte), 1), 1u);
  EXPECT_TRUE(parser.initialized());
}

TEST(OpdsParser, ParsesAFeedDeliveredInChunksAfterLazyInitialization) {
  constexpr char feed[] =
      "<?xml version=\"1.0\"?>"
      "<feed xmlns=\"http://www.w3.org/2005/Atom\">"
      "<link rel=\"search\" href=\"/search?q={searchTerms}\"/>"
      "<entry><title>Book One</title><author><name>A. Author</name></author>"
      "<id>book-1</id><link rel=\"http://opds-spec.org/acquisition\" "
      "type=\"application/epub+zip\" href=\"/book-1.epub\"/></entry>"
      "</feed>";
  constexpr size_t split = 47;

  OpdsParser parser;
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(feed), split), split);
  ASSERT_EQ(parser.write(reinterpret_cast<const uint8_t*>(feed + split), std::strlen(feed) - split),
            std::strlen(feed) - split);
  parser.flush();

  ASSERT_FALSE(parser.error());
  ASSERT_EQ(parser.getEntries().size(), 1u);
  EXPECT_EQ(parser.getEntries()[0].title, "Book One");
  EXPECT_EQ(parser.getEntries()[0].author, "A. Author");
  EXPECT_EQ(parser.getEntries()[0].href, "/book-1.epub");
  EXPECT_EQ(parser.getSearchTemplate(), "/search?q={searchTerms}");
}

TEST(OpdsParser, EmptyBodyStillFailsAtFlush) {
  OpdsParser parser;

  parser.flush();

  EXPECT_TRUE(parser.error());
  EXPECT_FALSE(parser.initialized());
}

}  // namespace
