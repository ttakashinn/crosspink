#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ClippingCodec.h"
#include "saved_items/SavedItemsCatalog.h"

namespace ClippingExporter {

constexpr const char* OUTPUT_PATH = "/My Clippings - VNS.txt";

enum class Status : uint8_t { EXPORTED, PARTIAL, NO_CLIPPINGS, IO_ERROR };

struct Result {
  Status status = Status::IO_ERROR;
  uint16_t exportedClippings = 0;
  uint16_t skippedBooks = 0;
};

using LoadBook = bool (*)(const SavedItemsCatalog::Entry& entry, std::vector<ClippingCodec::Record>& records,
                          void* context);

// Streams one book at a time to outputPath.tmp, verifies the complete file,
// then publishes it while retaining the previous export as outputPath.bak.
Result exportTo(const std::vector<SavedItemsCatalog::Entry>& books, const std::string& outputPath, LoadBook loadBook,
                void* context = nullptr);

}  // namespace ClippingExporter
