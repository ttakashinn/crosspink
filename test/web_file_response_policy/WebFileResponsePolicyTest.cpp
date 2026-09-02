#include <gtest/gtest.h>

#include "src/network/WebFileResponsePolicy.h"

TEST(WebFileResponsePolicyTest, ReturnsImageContentTypesCaseInsensitively) {
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/cover.JPG"), "image/jpeg");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/cover.jpeg"), "image/jpeg");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/page.PNG"), "image/png");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/page.bmp"), "image/bmp");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/page.GIF"), "image/gif");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/images/page.WeBp"), "image/webp");
}

TEST(WebFileResponsePolicyTest, KeepsKnownDownloadAndFallbackContentTypes) {
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/books/book.EPUB"), "application/epub+zip");
  EXPECT_STREQ(WebFileResponsePolicy::contentTypeForPath("/books/book.xtc"), "application/octet-stream");
}

TEST(WebFileResponsePolicyTest, AllowsInlineOnlyForExplicitImagePreview) {
  EXPECT_TRUE(WebFileResponsePolicy::shouldServeInline("/images/page.png", true));
  EXPECT_FALSE(WebFileResponsePolicy::shouldServeInline("/images/page.png", false));
  EXPECT_FALSE(WebFileResponsePolicy::shouldServeInline("/books/page.html", true));
  EXPECT_FALSE(WebFileResponsePolicy::shouldServeInline("/books/book.epub", true));
}
