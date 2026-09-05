#include <gtest/gtest.h>

#include <vector>

#include "RecentBookList.h"

namespace {
RecentBook book(const char* path, const char* title = "title", const char* author = "author",
                const char* cover = "cover") {
  return {path, title, author, cover};
}
}  // namespace

TEST(RecentBookList, UnchangedFrontEntryIsANoOp) {
  std::vector<RecentBook> books{book("/a"), book("/b")};

  EXPECT_FALSE(recent_books::upsertFront(books, "/a", "title", "author", "cover", 10));
  ASSERT_EQ(books.size(), 2U);
  EXPECT_EQ(books[0].path, "/a");
  EXPECT_EQ(books[1].path, "/b");
}

TEST(RecentBookList, ExistingEntryIsUpdatedAndPromotedWithoutChangingSize) {
  std::vector<RecentBook> books{book("/a"), book("/b", "old", "old", "old"), book("/c")};

  EXPECT_TRUE(recent_books::upsertFront(books, "/b", "new", "writer", "new-cover", 10));
  ASSERT_EQ(books.size(), 3U);
  EXPECT_EQ(books[0].path, "/b");
  EXPECT_EQ(books[0].title, "new");
  EXPECT_EQ(books[0].author, "writer");
  EXPECT_EQ(books[0].coverBmpPath, "new-cover");
  EXPECT_EQ(books[1].path, "/a");
  EXPECT_EQ(books[2].path, "/c");
}

TEST(RecentBookList, NewEntryIsInsertedAndOldestEntryIsTrimmed) {
  std::vector<RecentBook> books{book("/a"), book("/b"), book("/c")};

  EXPECT_TRUE(recent_books::upsertFront(books, "/new", "title", "author", "cover", 3));
  ASSERT_EQ(books.size(), 3U);
  EXPECT_EQ(books[0].path, "/new");
  EXPECT_EQ(books[1].path, "/a");
  EXPECT_EQ(books[2].path, "/b");
}

TEST(RecentBookList, MetadataUpdateReportsOnlyPersistedChanges) {
  std::vector<RecentBook> books{book("/a")};

  EXPECT_FALSE(recent_books::updateMetadata(books, "/missing", "title", "author", "cover"));
  EXPECT_FALSE(recent_books::updateMetadata(books, "/a", "title", "author", "cover"));
  EXPECT_TRUE(recent_books::updateMetadata(books, "/a", "new-title", "new-author", "new-cover"));
  EXPECT_EQ(books[0].title, "new-title");
  EXPECT_EQ(books[0].author, "new-author");
  EXPECT_EQ(books[0].coverBmpPath, "new-cover");
}
