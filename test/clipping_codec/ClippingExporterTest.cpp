#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>

#include "ClippingExporter.h"
#include "HalStorage.h"

namespace fs = std::filesystem;

namespace {

struct LoaderContext {
  std::unordered_map<std::string, std::vector<ClippingCodec::Record>> records;
};

bool loadBook(const SavedItemsCatalog::Entry& entry, std::vector<ClippingCodec::Record>& records, void* opaque) {
  auto& context = *static_cast<LoaderContext*>(opaque);
  const auto found = context.records.find(entry.sourcePath);
  if (found == context.records.end()) return false;
  records = found->second;
  return true;
}

std::string readAll(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class ClippingExporterTest : public testing::Test {
 protected:
  void SetUp() override {
    const auto* info = testing::UnitTest::GetInstance()->current_test_info();
    root = fs::temp_directory_path() / "crosspoint_clipping_exporter_test" / info->name();
    fs::remove_all(root);
    fs::create_directories(root);
    output = root / "My Clippings - VNS.txt";
    Storage.resetFaults();
  }

  void TearDown() override {
    Storage.resetFaults();
    fs::remove_all(root);
  }

  static SavedItemsCatalog::Entry book(std::string path, std::string title, std::string author, uint16_t count = 1) {
    return {std::move(path), std::move(title), std::move(author), 0, count};
  }

  static ClippingCodec::Record clipping(std::string text, uint32_t id, uint16_t spine = 1, uint16_t page = 2,
                                        uint32_t offset = 30) {
    return {spine, page, offset, 4, 6, std::move(text), id};
  }

  fs::path root;
  fs::path output;
};

}  // namespace

TEST_F(ClippingExporterTest, StreamsBooksInCatalogOrderAndPreservesNfcAndNfdText) {
  const std::vector<SavedItemsCatalog::Entry> books = {book("/Books/a.epub", "Sách A", "Tác giả A"),
                                                       book("/Books/b.epub", "Sách B", "Tác giả B")};
  LoaderContext context;
  context.records[books[0].sourcePath] = {clipping("Tiếng Việt", 1)};
  context.records[books[1].sourcePath] = {clipping("Tiếng Việt", 2, 3, 4, 90)};

  const auto result = ClippingExporter::exportTo(books, output.string(), loadBook, &context);

  EXPECT_EQ(result.status, ClippingExporter::Status::EXPORTED);
  EXPECT_EQ(result.exportedClippings, 2);
  EXPECT_EQ(result.skippedBooks, 0);
  EXPECT_EQ(readAll(output),
            "My Clippings - VNS\n==================\n\n"
            "Sách A\nAuthor: Tác giả A\n- Page 3 | Chapter 2 | Offset 30\n\nTiếng Việt\n==========\n"
            "Sách B\nAuthor: Tác giả B\n- Page 5 | Chapter 4 | Offset 90\n\nTiếng Việt\n==========\n");
}

TEST_F(ClippingExporterTest, SkipsMissingAndInvalidBooksButPublishesValidClippings) {
  const std::vector<SavedItemsCatalog::Entry> books = {book("/Books/good.epub", "Good", ""),
                                                       book("/Books/missing.epub", "Missing", ""),
                                                       book("/Books/corrupt.epub", "Corrupt", "")};
  LoaderContext context;
  context.records[books[0].sourcePath] = {clipping("valid", 1)};
  context.records[books[2].sourcePath] = {clipping("first", 7), clipping("duplicate", 7)};

  const auto result = ClippingExporter::exportTo(books, output.string(), loadBook, &context);

  EXPECT_EQ(result.status, ClippingExporter::Status::PARTIAL);
  EXPECT_EQ(result.exportedClippings, 1);
  EXPECT_EQ(result.skippedBooks, 2);
  EXPECT_NE(readAll(output).find("valid"), std::string::npos);
  EXPECT_EQ(readAll(output).find("Missing"), std::string::npos);
  EXPECT_EQ(readAll(output).find("Corrupt"), std::string::npos);
}

TEST_F(ClippingExporterTest, PartialWriteKeepsLastVerifiedExport) {
  const auto entry = book("/Books/book.epub", "Book", "Author");
  LoaderContext context;
  context.records[entry.sourcePath] = {clipping("old", 1)};
  ASSERT_EQ(ClippingExporter::exportTo({entry}, output.string(), loadBook, &context).status,
            ClippingExporter::Status::EXPORTED);
  const std::string oldExport = readAll(output);

  context.records[entry.sourcePath] = {clipping("new", 2)};
  Storage.limitWritesTo(12);
  const auto result = ClippingExporter::exportTo({entry}, output.string(), loadBook, &context);

  EXPECT_EQ(result.status, ClippingExporter::Status::IO_ERROR);
  EXPECT_EQ(readAll(output), oldExport);
  EXPECT_FALSE(fs::exists(output.string() + ".tmp"));
}

TEST_F(ClippingExporterTest, SuccessfulReplacementKeepsPreviousExportAsBackup) {
  const auto entry = book("/Books/book.epub", "Book", "Author");
  LoaderContext context;
  context.records[entry.sourcePath] = {clipping("old", 1)};
  ASSERT_EQ(ClippingExporter::exportTo({entry}, output.string(), loadBook, &context).status,
            ClippingExporter::Status::EXPORTED);
  const std::string oldExport = readAll(output);

  context.records[entry.sourcePath] = {clipping("new", 2)};
  ASSERT_EQ(ClippingExporter::exportTo({entry}, output.string(), loadBook, &context).status,
            ClippingExporter::Status::EXPORTED);

  EXPECT_NE(readAll(output).find("new"), std::string::npos);
  EXPECT_EQ(readAll(output.string() + ".bak"), oldExport);
}

TEST_F(ClippingExporterTest, UnavailableOnlyExportDoesNotReplaceExistingFile) {
  const auto entry = book("/Books/book.epub", "Book", "Author");
  LoaderContext context;
  context.records[entry.sourcePath] = {clipping("old", 1)};
  ASSERT_EQ(ClippingExporter::exportTo({entry}, output.string(), loadBook, &context).status,
            ClippingExporter::Status::EXPORTED);
  const std::string oldExport = readAll(output);
  context.records.clear();

  const auto result = ClippingExporter::exportTo({entry}, output.string(), loadBook, &context);

  EXPECT_EQ(result.status, ClippingExporter::Status::NO_CLIPPINGS);
  EXPECT_EQ(result.skippedBooks, 1);
  EXPECT_EQ(readAll(output), oldExport);
}

TEST_F(ClippingExporterTest, ExportsMultiPageLocationRange) {
  const auto entry = book("/Books/book.epub", "Book", "Author");
  ClippingCodec::Record record{1, 2, 30, 4, 6, "page one page two", 12};
  record.segmentCount = 2;
  record.segments[0] = {2, 30, 4, 6, 0, 8};
  record.segments[1] = {3, 70, 0, 1, 9, 8};
  LoaderContext context;
  context.records[entry.sourcePath] = {record};

  ASSERT_EQ(ClippingExporter::exportTo({entry}, output.string(), loadBook, &context).status,
            ClippingExporter::Status::EXPORTED);
  EXPECT_NE(readAll(output).find("- Pages 3-4 | Chapter 2 | Offset 30"), std::string::npos);
}
