#include "ClippingStore.h"

#include <HalStorage.h>
#include <Utf8.h>

#include <algorithm>
#include <array>

namespace ClippingStore {
namespace {

constexpr char FILE_NAME[] = "clippings-vns.bin";
enum class ReadStatus { OK, MISSING, NEWER_VERSION, INVALID, IO_ERROR };

uint16_t readU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t readU32(const uint8_t* bytes) {
  return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void updateCrc(uint32_t& crc, const uint8_t* bytes, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
}

std::string pathFor(const std::string& cachePath, const char* suffix) {
  return cachePath + (cachePath.empty() || cachePath.back() == '/' ? "" : "/") + FILE_NAME + suffix;
}
bool removeIfPresent(const std::string& path) { return !Storage.exists(path.c_str()) || Storage.remove(path.c_str()); }

ReadStatus read(const std::string& path, std::vector<ClippingCodec::Record>& records) {
  HalFile file;
  if (!Storage.openFileForRead("CLIP", path, file)) {
    return Storage.exists(path.c_str()) ? ReadStatus::IO_ERROR : ReadStatus::MISSING;
  }
  const size_t length = file.fileSize();
  if (length == 0 || length > ClippingCodec::MAX_FILE_BYTES) {
    if (length >= 5) {
      std::array<uint8_t, 5> prefix{};
      if (file.read(prefix.data(), prefix.size()) != static_cast<int>(prefix.size())) return ReadStatus::IO_ERROR;
      if (prefix[0] == 'V' && prefix[1] == 'N' && prefix[2] == 'S' && prefix[3] == 'C' &&
          prefix[4] > ClippingCodec::VERSION) {
        return ReadStatus::NEWER_VERSION;
      }
    }
    return ReadStatus::INVALID;
  }
  std::vector<uint8_t> bytes(length);
  if (file.read(bytes.data(), length) != static_cast<int>(length) || !file.close()) return ReadStatus::IO_ERROR;
  const auto status = ClippingCodec::decode(bytes.data(), bytes.size(), records);
  if (status == ClippingCodec::Status::OK) return ReadStatus::OK;
  if (status == ClippingCodec::Status::NEWER_VERSION) return ReadStatus::NEWER_VERSION;
  return ReadStatus::INVALID;
}

ReadStatus inspect(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("CLIP", path, file)) {
    return Storage.exists(path.c_str()) ? ReadStatus::IO_ERROR : ReadStatus::MISSING;
  }
  const size_t length = file.fileSize();
  std::array<uint8_t, ClippingCodec::HEADER_SIZE> header{};
  if (length < header.size() || file.read(header.data(), header.size()) != static_cast<int>(header.size())) {
    file.close();
    return ReadStatus::INVALID;
  }
  if (header[0] != 'V' || header[1] != 'N' || header[2] != 'S' || header[3] != 'C') {
    file.close();
    return ReadStatus::INVALID;
  }
  if (header[4] > ClippingCodec::VERSION) {
    file.close();
    return ReadStatus::NEWER_VERSION;
  }
  if ((header[4] != 1 && header[4] != ClippingCodec::VERSION) || header[5] != 0) {
    file.close();
    return ReadStatus::INVALID;
  }

  const bool legacy = header[4] == 1;
  const size_t recordHeaderSize = legacy ? ClippingCodec::LEGACY_RECORD_HEADER_SIZE : ClippingCodec::RECORD_HEADER_SIZE;
  const uint16_t count = readU16(header.data() + 6);
  const uint32_t payloadSize = readU32(header.data() + 8);
  const size_t maxPayload = ClippingCodec::MAX_CLIPPINGS_PER_BOOK * (recordHeaderSize + ClippingCodec::MAX_TEXT_BYTES);
  if (count > ClippingCodec::MAX_CLIPPINGS_PER_BOOK || payloadSize > maxPayload ||
      length != ClippingCodec::HEADER_SIZE + payloadSize) {
    file.close();
    return ReadStatus::INVALID;
  }

  uint32_t crc = UINT32_MAX;
  size_t consumed = 0;
  std::array<uint8_t, ClippingCodec::RECORD_HEADER_SIZE> recordHeader{};
  std::array<uint8_t, ClippingCodec::MAX_TEXT_BYTES> text{};
  std::array<uint32_t, ClippingCodec::MAX_CLIPPINGS_PER_BOOK> ids{};
  for (uint16_t i = 0; i < count; ++i) {
    if (payloadSize - consumed < recordHeaderSize ||
        file.read(recordHeader.data(), recordHeaderSize) != static_cast<int>(recordHeaderSize)) {
      file.close();
      return ReadStatus::INVALID;
    }
    updateCrc(crc, recordHeader.data(), recordHeaderSize);
    consumed += recordHeaderSize;

    const size_t startOffset = legacy ? 8 : 12;
    const size_t endOffset = legacy ? 10 : 14;
    const size_t textLengthOffset = legacy ? 12 : 16;
    const size_t reservedOffset = legacy ? 14 : 18;
    const uint16_t start = readU16(recordHeader.data() + startOffset);
    const uint16_t end = readU16(recordHeader.data() + endOffset);
    const uint16_t textLength = readU16(recordHeader.data() + textLengthOffset);
    if (start > end || textLength == 0 || textLength > text.size() || recordHeader[reservedOffset] != 0 ||
        recordHeader[reservedOffset + 1] != 0 || textLength > payloadSize - consumed) {
      file.close();
      return ReadStatus::INVALID;
    }
    if (!legacy) {
      const uint32_t id = readU32(recordHeader.data());
      if (id == 0 || std::find(ids.begin(), ids.begin() + i, id) != ids.begin() + i) {
        file.close();
        return ReadStatus::INVALID;
      }
      ids[i] = id;
    }
    if (file.read(text.data(), textLength) != static_cast<int>(textLength)) {
      file.close();
      return ReadStatus::IO_ERROR;
    }
    updateCrc(crc, text.data(), textLength);
    consumed += textLength;
    if (!utf8IsValid({reinterpret_cast<const char*>(text.data()), textLength})) {
      file.close();
      return ReadStatus::INVALID;
    }
  }
  const bool valid = consumed == payloadSize && ~crc == readU32(header.data() + 12);
  const bool closed = file.close();
  if (!closed) return ReadStatus::IO_ERROR;
  return valid ? ReadStatus::OK : ReadStatus::INVALID;
}

bool fileMatches(const std::string& path, const std::vector<uint8_t>& expected) {
  HalFile file;
  if (!Storage.openFileForRead("CLIP", path, file) || file.fileSize() != expected.size()) return false;
  std::array<uint8_t, 128> chunk{};
  size_t offset = 0;
  while (offset < expected.size()) {
    const size_t count = std::min(chunk.size(), expected.size() - offset);
    if (file.read(chunk.data(), count) != static_cast<int>(count) ||
        !std::equal(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(count), expected.begin() + offset)) {
      return false;
    }
    offset += count;
  }
  return file.close();
}

bool writeVerified(const std::string& path, const std::vector<uint8_t>& bytes) {
  HalFile file;
  if (!Storage.openFileForWrite("CLIP", path, file)) return false;
  bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
  file.flush();
  if (!file.close()) ok = false;
  // Compare in small fixed chunks. Decoding here would temporarily retain the
  // cached records, encoded payload, readback payload and a second record set
  // at once -- an avoidable worst-case heap spike on ESP32-C3.
  return ok && fileMatches(path, bytes);
}

}  // namespace

LoadStatus load(const std::string& cachePath, std::vector<ClippingCodec::Record>& records) {
  const std::array<std::pair<std::string, LoadStatus>, 3> paths = {
      std::pair{pathFor(cachePath, ""), LoadStatus::LOADED},
      std::pair{pathFor(cachePath, ".bak"), LoadStatus::LOADED_BACKUP},
      std::pair{pathFor(cachePath, ".tmp"), LoadStatus::LOADED_TEMP},
  };
  std::array<ReadStatus, 3> statuses{};
  bool invalid = false;
  for (size_t i = 0; i < paths.size(); ++i) {
    statuses[i] = inspect(paths[i].first);
    switch (statuses[i]) {
      case ReadStatus::OK:
        break;
      case ReadStatus::NEWER_VERSION:
        return LoadStatus::NEWER_VERSION;
      case ReadStatus::IO_ERROR:
        return LoadStatus::IO_ERROR;
      case ReadStatus::INVALID:
        invalid = true;
        break;
      case ReadStatus::MISSING:
        break;
    }
  }
  for (size_t i = 0; i < paths.size(); ++i) {
    if (statuses[i] != ReadStatus::OK) continue;
    switch (read(paths[i].first, records)) {
      case ReadStatus::OK:
        return paths[i].second;
      case ReadStatus::NEWER_VERSION:
        return LoadStatus::NEWER_VERSION;
      case ReadStatus::IO_ERROR:
        return LoadStatus::IO_ERROR;
      case ReadStatus::INVALID:
      case ReadStatus::MISSING:
        records.clear();
        return LoadStatus::INVALID;
    }
  }
  records.clear();
  return invalid ? LoadStatus::INVALID : LoadStatus::MISSING;
}

SaveStatus save(const std::string& cachePath, const std::vector<ClippingCodec::Record>& records) {
  std::vector<uint8_t> bytes;
  if (ClippingCodec::encode(records, bytes) != ClippingCodec::Status::OK) return SaveStatus::INVALID;
  const std::string finalPath = pathFor(cachePath, "");
  const std::string backupPath = pathFor(cachePath, ".bak");
  const std::string tempPath = pathFor(cachePath, ".tmp");

  bool finalValid = false;
  for (const std::string* path : {&finalPath, &backupPath, &tempPath}) {
    const ReadStatus status = inspect(*path);
    if (status == ReadStatus::NEWER_VERSION) return SaveStatus::NEWER_VERSION;
    if (status == ReadStatus::IO_ERROR) return SaveStatus::IO_ERROR;
    if (path == &finalPath) finalValid = status == ReadStatus::OK;
  }
  if (!removeIfPresent(tempPath) || !writeVerified(tempPath, bytes)) return SaveStatus::IO_ERROR;
  if (finalValid) {
    if (!removeIfPresent(backupPath) || !Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      removeIfPresent(tempPath);
      return SaveStatus::IO_ERROR;
    }
  } else if (!removeIfPresent(finalPath)) {
    removeIfPresent(tempPath);
    return SaveStatus::IO_ERROR;
  }
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    if (finalValid && !Storage.exists(finalPath.c_str())) Storage.rename(backupPath.c_str(), finalPath.c_str());
    return SaveStatus::IO_ERROR;
  }
  return fileMatches(finalPath, bytes) ? SaveStatus::SAVED : SaveStatus::IO_ERROR;
}

}  // namespace ClippingStore
