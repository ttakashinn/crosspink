#include "ClippingExporter.h"

#include <HalStorage.h>
#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace ClippingExporter {
namespace {

constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

struct Digest {
  uint64_t hash = FNV_OFFSET;
  size_t bytes = 0;

  void update(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; ++i) {
      hash ^= data[i];
      hash *= FNV_PRIME;
    }
    bytes += length;
  }
};

bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

bool writeBytes(HalFile& file, const void* data, const size_t length, Digest& digest) {
  if (length == 0) return true;
  if (file.write(data, length) != length) return false;
  digest.update(static_cast<const uint8_t*>(data), length);
  return true;
}

bool writeString(HalFile& file, const std::string& value, Digest& digest) {
  return writeBytes(file, value.data(), value.size(), digest);
}

bool writeLiteral(HalFile& file, const char* value, Digest& digest) {
  return writeBytes(file, value, std::char_traits<char>::length(value), digest);
}

bool readDigest(const std::string& path, Digest& digest) {
  HalFile file;
  if (!Storage.openFileForRead("CLX", path, file)) return false;
  std::array<uint8_t, 256> chunk{};
  size_t remaining = file.fileSize();
  while (remaining > 0) {
    const size_t count = std::min(remaining, chunk.size());
    if (file.read(chunk.data(), count) != static_cast<int>(count)) {
      file.close();
      return false;
    }
    digest.update(chunk.data(), count);
    remaining -= count;
  }
  return file.close();
}

bool matchesDigest(const std::string& path, const Digest& expected) {
  Digest actual;
  return readDigest(path, actual) && actual.bytes == expected.bytes && actual.hash == expected.hash;
}

bool validRecords(const std::vector<ClippingCodec::Record>& records) {
  if (records.empty() || records.size() > ClippingCodec::MAX_CLIPPINGS_PER_BOOK) return false;
  std::array<uint32_t, ClippingCodec::MAX_CLIPPINGS_PER_BOOK> ids{};
  for (size_t i = 0; i < records.size(); ++i) {
    const auto& record = records[i];
    if (!ClippingCodec::validRecord(record) || std::find(ids.begin(), ids.begin() + static_cast<std::ptrdiff_t>(i),
                                                         record.id) != ids.begin() + static_cast<std::ptrdiff_t>(i)) {
      return false;
    }
    ids[i] = record.id;
  }
  return true;
}

bool writeBook(HalFile& file, const SavedItemsCatalog::Entry& book, const std::vector<ClippingCodec::Record>& records,
               Digest& digest) {
  const std::string& title = book.title.empty() ? book.sourcePath : book.title;
  if (!writeString(file, title, digest) || !writeLiteral(file, "\n", digest)) return false;
  if (!book.author.empty() && (!writeLiteral(file, "Author: ", digest) || !writeString(file, book.author, digest) ||
                               !writeLiteral(file, "\n", digest))) {
    return false;
  }

  for (const auto& record : records) {
    const auto first = ClippingCodec::segmentAt(record, 0);
    const auto last = ClippingCodec::segmentAt(record, ClippingCodec::segmentCount(record) - 1);
    const std::string pages = first.pageHint == last.pageHint
                                  ? "Page " + std::to_string(static_cast<unsigned>(first.pageHint) + 1U)
                                  : "Pages " + std::to_string(static_cast<unsigned>(first.pageHint) + 1U) + "-" +
                                        std::to_string(static_cast<unsigned>(last.pageHint) + 1U);
    const std::string location = "- " + pages + " | Chapter " +
                                 std::to_string(static_cast<unsigned>(record.spineIndex) + 1U) + " | Offset " +
                                 std::to_string(first.pageVisibleOffset) + "\n\n";
    if (!writeString(file, location, digest) || !writeString(file, record.text, digest) ||
        !writeLiteral(file, "\n==========\n", digest)) {
      return false;
    }
  }
  return true;
}

}  // namespace

Result exportTo(const std::vector<SavedItemsCatalog::Entry>& books, const std::string& outputPath, LoadBook loadBook,
                void* context) {
  Result result;
  if (outputPath.empty() || !loadBook) return result;
  if (std::none_of(books.begin(), books.end(),
                   [](const SavedItemsCatalog::Entry& book) { return book.clippingCount > 0; })) {
    result.status = Status::NO_CLIPPINGS;
    return result;
  }

  const std::string tempPath = outputPath + ".tmp";
  const std::string backupPath = outputPath + ".bak";
  if (!removeIfPresent(tempPath)) return result;

  HalFile output;
  if (!Storage.openFileForWrite("CLX", tempPath, output)) return result;
  Digest expected;
  bool writeOk = writeLiteral(output, "My Clippings - VNS\n==================\n\n", expected);
  std::vector<ClippingCodec::Record> records;
  for (const auto& book : books) {
    if (!writeOk || book.clippingCount == 0) continue;
    records.clear();
    if (!loadBook(book, records, context) || !validRecords(records)) {
      ++result.skippedBooks;
      continue;
    }
    writeOk = writeBook(output, book, records, expected);
    result.exportedClippings = static_cast<uint16_t>(result.exportedClippings + records.size());
  }
  output.flush();
  if (!output.close()) writeOk = false;

  if (!writeOk || !matchesDigest(tempPath, expected)) {
    removeIfPresent(tempPath);
    result.status = Status::IO_ERROR;
    result.exportedClippings = 0;
    return result;
  }
  if (result.exportedClippings == 0) {
    removeIfPresent(tempPath);
    result.status = Status::NO_CLIPPINGS;
    return result;
  }

  bool rotatedFinal = false;
  if (Storage.exists(outputPath.c_str())) {
    Digest ignored;
    if (readDigest(outputPath, ignored)) {
      if (!removeIfPresent(backupPath) || !Storage.rename(outputPath.c_str(), backupPath.c_str())) {
        removeIfPresent(tempPath);
        result.status = Status::IO_ERROR;
        result.exportedClippings = 0;
        return result;
      }
      rotatedFinal = true;
    } else if (!Storage.remove(outputPath.c_str())) {
      removeIfPresent(tempPath);
      result.status = Status::IO_ERROR;
      result.exportedClippings = 0;
      return result;
    }
  }

  if (!Storage.rename(tempPath.c_str(), outputPath.c_str()) || !matchesDigest(outputPath, expected)) {
    removeIfPresent(outputPath);
    if (rotatedFinal) Storage.rename(backupPath.c_str(), outputPath.c_str());
    removeIfPresent(tempPath);
    result.status = Status::IO_ERROR;
    result.exportedClippings = 0;
    return result;
  }

  result.status = result.skippedBooks == 0 ? Status::EXPORTED : Status::PARTIAL;
  return result;
}

}  // namespace ClippingExporter
