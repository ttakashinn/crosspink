#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

struct RecentBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const RecentBook& other) const { return path == other.path; }
};

namespace recent_books {
inline bool hasSameMetadata(const RecentBook& book, const std::string& title, const std::string& author,
                            const std::string& coverBmpPath) {
  return book.title == title && book.author == author && book.coverBmpPath == coverBmpPath;
}

// Insert a new book or promote an existing one. Returns false only when the
// list, order, and persisted metadata are already identical.
inline bool upsertFront(std::vector<RecentBook>& books, const std::string& path, const std::string& title,
                        const std::string& author, const std::string& coverBmpPath, const size_t maxBooks) {
  if (maxBooks == 0) {
    const bool changed = !books.empty();
    books.clear();
    return changed;
  }

  auto it = std::find_if(books.begin(), books.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it != books.end()) {
    const bool changed = it != books.begin() || !hasSameMetadata(*it, title, author, coverBmpPath);
    if (!changed) return false;

    it->title = title;
    it->author = author;
    it->coverBmpPath = coverBmpPath;
    if (it != books.begin()) std::rotate(books.begin(), it, std::next(it));
    return true;
  }

  books.insert(books.begin(), {path, title, author, coverBmpPath});
  if (books.size() > maxBooks) books.resize(maxBooks);
  return true;
}

inline bool updateMetadata(std::vector<RecentBook>& books, const std::string& path, const std::string& title,
                           const std::string& author, const std::string& coverBmpPath) {
  auto it = std::find_if(books.begin(), books.end(), [&](const RecentBook& book) { return book.path == path; });
  if (it == books.end() || hasSameMetadata(*it, title, author, coverBmpPath)) return false;
  it->title = title;
  it->author = author;
  it->coverBmpPath = coverBmpPath;
  return true;
}
}  // namespace recent_books
