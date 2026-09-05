#include "ContentOpfParser.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Serialization.h>
#include <XmlParserUtils.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

#include "Epub/BookMetadataCache.h"

namespace {
constexpr char MEDIA_TYPE_NCX[] = "application/x-dtbncx+xml";
constexpr char MEDIA_TYPE_CSS[] = "text/css";
constexpr char MEDIA_TYPE_IMAGE_PREFIX[] = "image/";
constexpr char itemCacheFile[] = "/.items.bin";
constexpr size_t MAX_MANIFEST_ID = 4096;
constexpr size_t MAX_MANIFEST_HREF = 4096;
constexpr size_t MAX_METADATA_VALUE = 1024;
constexpr size_t MAX_INDEXED_MANIFEST_ITEMS = 4096;
constexpr size_t MAX_CSS_FILES = 256;

bool startsWithImageMediaType(const std::string& mediaType) {
  constexpr size_t prefixLen = sizeof(MEDIA_TYPE_IMAGE_PREFIX) - 1;
  if (mediaType.size() < prefixLen) {
    return false;
  }

  for (size_t i = 0; i < prefixLen; ++i) {
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(mediaType[i])));
    if (c != MEDIA_TYPE_IMAGE_PREFIX[i]) {
      return false;
    }
  }

  return true;
}

bool boundedAttribute(const char* value, const size_t maxLength, std::string& output) {
  if (!value) return false;
  size_t length = 0;
  while (length <= maxLength && value[length] != '\0') ++length;
  if (length > maxLength) return false;
  output.assign(value, length);
  return true;
}

void appendBounded(std::string& output, const char* value, const int length, const size_t maxLength) {
  if (!value || length <= 0 || output.size() >= maxLength) return;
  size_t count = std::min<size_t>(static_cast<size_t>(length), maxLength - output.size());
  if (count < static_cast<size_t>(length)) {
    while (count > 0 && (static_cast<uint8_t>(value[count]) & 0xC0U) == 0x80U) --count;
  }
  output.append(value, count);
}

bool hasSpaceSeparatedToken(const std::string& value, const char* token) {
  const size_t tokenLength = strlen(token);
  size_t start = 0;
  while (start < value.size()) {
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;
    size_t end = start;
    while (end < value.size() && !std::isspace(static_cast<unsigned char>(value[end]))) ++end;
    if (end - start == tokenLength && value.compare(start, tokenLength, token) == 0) return true;
    start = end;
  }
  return false;
}

bool readItemIdMatches(HalFile& file, const std::string& targetId, bool& matches) {
  uint32_t storedLength = 0;
  if (!serialization::tryReadPod(file, storedLength) || storedLength > MAX_MANIFEST_ID) return false;

  const uint64_t idEnd = static_cast<uint64_t>(file.position()) + storedLength;
  if (idEnd > file.fileSize64()) return false;

  matches = storedLength == targetId.size();
  if (!matches) return file.seekCur(static_cast<int64_t>(storedLength));

  constexpr size_t COMPARE_BUFFER_SIZE = 64;
  uint8_t compareBuffer[COMPARE_BUFFER_SIZE];
  size_t compared = 0;
  while (compared < storedLength) {
    const size_t chunkSize = std::min(COMPARE_BUFFER_SIZE, static_cast<size_t>(storedLength) - compared);
    if (file.read(compareBuffer, chunkSize) != static_cast<int>(chunkSize)) return false;
    if (std::memcmp(compareBuffer, targetId.data() + compared, chunkSize) != 0) matches = false;
    compared += chunkSize;
  }
  return true;
}

bool skipStoredString(HalFile& file, const size_t maxLength) {
  uint32_t storedLength = 0;
  if (!serialization::tryReadPod(file, storedLength) || storedLength > maxLength) return false;
  const uint64_t valueEnd = static_cast<uint64_t>(file.position()) + storedLength;
  return valueEnd <= file.fileSize64() && file.seekCur(static_cast<int64_t>(storedLength));
}
}  // namespace

bool ContentOpfParser::findIndexedItemHref(const std::string& idref, std::string& href, bool& found) {
  found = false;
  const uint32_t targetHash = fnvHash(idref);
  const uint16_t targetLen = static_cast<uint16_t>(idref.size());
  const auto result = itemIndex.visitCandidates(targetHash, targetLen, [&](const uint32_t fileOffset) {
    if (!tempItemStore.seek(fileOffset)) {
      LOG_ERR("COF", "Failed seeking manifest index row at %u", static_cast<unsigned>(fileOffset));
      return ManifestItemIndex::CandidateResult::Error;
    }
    bool idMatches = false;
    if (!readItemIdMatches(tempItemStore, idref, idMatches)) {
      LOG_ERR("COF", "Failed reading manifest item ID at %u", static_cast<unsigned>(fileOffset));
      return ManifestItemIndex::CandidateResult::Error;
    }
    if (!idMatches) return ManifestItemIndex::CandidateResult::Continue;
    if (!serialization::tryReadString(tempItemStore, href, MAX_MANIFEST_HREF)) {
      LOG_ERR("COF", "Failed reading manifest item href at %u", static_cast<unsigned>(fileOffset));
      return ManifestItemIndex::CandidateResult::Error;
    }
    return ManifestItemIndex::CandidateResult::Found;
  });

  found = result == ManifestItemIndex::CandidateResult::Found;
  return result != ManifestItemIndex::CandidateResult::Error;
}

bool ContentOpfParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_DBG("COF", "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

bool ContentOpfParser::isUsable(const bool requireResolvedSpine) const {
  return !failed && remainingSize == 0 && packageSeen && manifestSeen &&
         (!requireResolvedSpine || (spineSeen && resolvedSpineItems > 0));
}

ContentOpfParser::~ContentOpfParser() {
  destroyXmlParser(parser);
  if (tempItemStore) {
    tempItemStore.close();
  }
  const auto itemCachePath = cachePath + itemCacheFile;
  if (Storage.exists(itemCachePath.c_str())) {
    Storage.remove(itemCachePath.c_str());
  }
}

size_t ContentOpfParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContentOpfParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser || failed || size > remainingSize) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);

    if (!buf) {
      LOG_ERR("COF", "Couldn't allocate memory for buffer");
      destroyXmlParser(parser);
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_DBG("COF", "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      destroyXmlParser(parser);
      return 0;
    }

    if (failed) {
      destroyXmlParser(parser);
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }

  return size;
}

void XMLCALL ContentOpfParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)atts;

  if (self->state == START && xmlLocalNameEquals(name, "package")) {
    self->packageSeen = true;
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "metadata")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "title")) {
    // Only capture the first title element; subsequent ones are subtitles
    if (self->title.empty()) {
      self->state = IN_BOOK_TITLE;
    }
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "creator")) {
    if (!self->author.empty() && self->author.size() < MAX_METADATA_VALUE) {
      self->author.append(", ", std::min<size_t>(2, MAX_METADATA_VALUE - self->author.size()));
    }
    self->state = IN_BOOK_AUTHOR;
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "language")) {
    // EPUB permits multiple dc:language elements. The reader has one language
    // slot, so retain the primary (first) value instead of concatenating
    // values into an invalid code such as "envi".
    if (self->language.empty()) self->state = IN_BOOK_LANGUAGE;
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "manifest")) {
    self->manifestSeen = true;
    self->state = IN_MANIFEST;
    // A metadata-only pass (used to recover CSS on a warm cache) does not need
    // the idref lookup file at all. Avoiding it removes unnecessary SD writes
    // and prevents an auxiliary CSS refresh from disturbing a valid book cache.
    if (self->cache && !Storage.openFileForWrite("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open manifest lookup file for writing");
      self->failed = true;
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "spine")) {
    self->spineSeen = true;
    self->state = IN_SPINE;
    if (self->cache && !Storage.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR("COF", "Couldn't open manifest lookup file for reading");
      self->failed = true;
      return;
    }

    // Each fixed-capacity chunk is sorted independently. This keeps lookups
    // indexed without requiring one large contiguous heap allocation.
    if (!self->itemIndex.empty()) {
      self->itemIndex.sort();
      self->useItemIndex = true;
      LOG_DBG("COF", "Using chunked index for %zu manifest items in %zu chunks (arena=%u bytes complete=%u)",
              self->itemIndex.size(), self->itemIndex.chunkCount(), static_cast<unsigned>(self->itemIndex.memoryUsed()),
              self->itemIndexComplete ? 1U : 0U);
    }
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "guide")) {
    self->state = IN_GUIDE;
    LOG_DBG("COF", "Entering guide state.");
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "meta")) {
    bool isCover = false;
    std::string coverItemId;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0 && strcmp(atts[i + 1], "cover") == 0) {
        isCover = true;
      } else if (strcmp(atts[i], "content") == 0) {
        boundedAttribute(atts[i + 1], MAX_MANIFEST_ID, coverItemId);
      }
    }

    if (isCover) {
      self->coverItemId = coverItemId;
    }
    return;
  }

  if (self->state == IN_MANIFEST && xmlLocalNameEquals(name, "item")) {
    std::string itemId;
    std::string href;
    std::string mediaType;
    std::string properties;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        boundedAttribute(atts[i + 1], MAX_MANIFEST_ID, itemId);
      } else if (strcmp(atts[i], "href") == 0) {
        std::string rawHref;
        if (boundedAttribute(atts[i + 1], MAX_MANIFEST_HREF, rawHref)) {
          href = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + rawHref));
          if (href.size() > MAX_MANIFEST_HREF) href.clear();
        }
      } else if (strcmp(atts[i], "media-type") == 0) {
        boundedAttribute(atts[i + 1], 128, mediaType);
      } else if (strcmp(atts[i], "properties") == 0) {
        boundedAttribute(atts[i + 1], 512, properties);
      }
    }

    // Persist id->href only during the cache-building pass. Metadata-only
    // parsing still collects cover/TOC/CSS information below.
    if (self->cache) {
      if (!self->tempItemStore) {
        LOG_ERR("COF", "Manifest lookup file is not writable");
        self->failed = true;
        return;
      }
      // A malformed, unreferenced manifest item must not make an otherwise
      // readable book fail indexing. Skip it here; a spine itemref that needs
      // it will be reported as unresolved below.
      if (itemId.empty() || href.empty() || itemId.size() > MAX_MANIFEST_ID || href.size() > MAX_MANIFEST_HREF) {
        LOG_ERR("COF", "Skipping malformed or oversized manifest item");
      } else {
        const size_t fileOffset = self->tempItemStore.position();
        if (self->itemIndexComplete && self->itemIndex.size() < MAX_INDEXED_MANIFEST_ITEMS &&
            fileOffset <= std::numeric_limits<uint32_t>::max()) {
          const ManifestItemIndex::Entry entry{fnvHash(itemId), static_cast<uint16_t>(itemId.size()),
                                               static_cast<uint32_t>(fileOffset)};
          if (!self->itemIndex.append(entry)) {
            // Keep the already-built prefix and every complete on-disk row.
            // References outside the prefix use the bounded-buffer scan.
            self->itemIndexComplete = false;
            LOG_ERR("COF", "Manifest index allocation stopped at %zu items; using file fallback",
                    self->itemIndex.size());
          }
        } else if (self->itemIndexComplete) {
          self->itemIndexComplete = false;
          LOG_DBG("COF", "Manifest index cap reached at %zu items; using file fallback", self->itemIndex.size());
        }

        const uint32_t itemIdSize = static_cast<uint32_t>(itemId.size());
        const uint32_t hrefSize = static_cast<uint32_t>(href.size());
        const bool written = self->tempItemStore.write(&itemIdSize, sizeof(itemIdSize)) == sizeof(itemIdSize) &&
                             self->tempItemStore.write(itemId.data(), itemId.size()) == itemId.size() &&
                             self->tempItemStore.write(&hrefSize, sizeof(hrefSize)) == sizeof(hrefSize) &&
                             self->tempItemStore.write(href.data(), href.size()) == href.size();
        if (!written) {
          LOG_ERR("COF", "Short write while building manifest lookup");
          self->failed = true;
          return;
        }
      }
    }

    if (itemId == self->coverItemId) {
      // Some EPUBs set meta name="cover" to an XHTML wrapper item.
      // Only treat it as a cover image when the manifest media-type is image/*.
      if (startsWithImageMediaType(mediaType)) {
        self->coverItemHref = href;
      } else {
        LOG_DBG("COF", "Ignoring meta cover item '%s' with non-image media type: %s", itemId.c_str(),
                mediaType.c_str());
      }
    }

    if (mediaType == MEDIA_TYPE_NCX) {
      if (self->tocNcxPath.empty()) {
        self->tocNcxPath = href;
      } else {
        LOG_DBG("COF", "Warning: Multiple NCX files found in manifest. Ignoring duplicate: %s", href.c_str());
      }
    }

    // Collect CSS files
    if (mediaType == MEDIA_TYPE_CSS) {
      if (!href.empty() && self->cssFiles.size() < MAX_CSS_FILES) {
        self->cssFiles.push_back(href);
      }
    }

    // EPUB 3: Check for nav document (properties contains "nav")
    if (!href.empty() && self->tocNavPath.empty() && hasSpaceSeparatedToken(properties, "nav")) {
      self->tocNavPath = href;
      LOG_DBG("COF", "Found EPUB 3 nav document: %s", href.c_str());
    }

    // EPUB 3: Check for cover image (properties contains "cover-image")
    if (!href.empty() && self->coverItemHref.empty() && hasSpaceSeparatedToken(properties, "cover-image")) {
      self->coverItemHref = href;
    }
    return;
  }

  // NOTE: This relies on spine appearing after item manifest (which is pretty safe as it's part of the EPUB spec)
  // Only run the spine parsing if there's a cache to add it to
  if (self->cache) {
    if (self->state == IN_SPINE && xmlLocalNameEquals(name, "itemref")) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "idref") == 0) {
          std::string idref;
          if (!boundedAttribute(atts[i + 1], MAX_MANIFEST_ID, idref) || idref.empty()) {
            self->unresolvedSpineItems++;
            LOG_ERR("COF", "Ignoring malformed spine idref");
            break;
          }
          std::string href;
          bool found = false;

          if (self->useItemIndex) {
            if (!self->findIndexedItemHref(idref, href, found)) {
              self->failed = true;
              return;
            }
          }

          if (!found && (!self->useItemIndex || !self->itemIndexComplete)) {
            // Empty manifests and entries beyond the bounded RAM index use a
            // complete linear scan. Normal books stay on the binary fast path.
            if (!self->tempItemStore.seek(0)) {
              self->failed = true;
              return;
            }
            while (self->tempItemStore.available()) {
              bool idMatches = false;
              if (!readItemIdMatches(self->tempItemStore, idref, idMatches)) {
                self->failed = true;
                return;
              }
              if (idMatches) {
                if (!serialization::tryReadString(self->tempItemStore, href, MAX_MANIFEST_HREF)) {
                  self->failed = true;
                  return;
                }
                found = true;
                break;
              }
              if (!skipStoredString(self->tempItemStore, MAX_MANIFEST_HREF)) {
                self->failed = true;
                return;
              }
            }
          }

          if (found) {
            if (!self->cache->createSpineEntry(href)) {
              self->failed = true;
              return;
            }
            self->resolvedSpineItems++;
          } else {
            self->unresolvedSpineItems++;
            LOG_ERR("COF", "Spine idref not found in manifest: %s", idref.c_str());
          }
        }
      }
      return;
    }
  }
  // parse the guide
  if (self->state == IN_GUIDE && xmlLocalNameEquals(name, "reference")) {
    std::string type;
    std::string guideHref;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "type") == 0) {
        boundedAttribute(atts[i + 1], 64, type);
      } else if (strcmp(atts[i], "href") == 0) {
        std::string rawHref;
        if (boundedAttribute(atts[i + 1], MAX_MANIFEST_HREF, rawHref)) {
          guideHref = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->baseContentPath + rawHref));
          if (guideHref.size() > MAX_MANIFEST_HREF) guideHref.clear();
        }
      }
    }
    if (!guideHref.empty()) {
      // EPUB 2 guides often mark every content file as "text", so that type
      // does not identify a reliable first-reading location. Only use the
      // explicit "start" semantic; otherwise the reader opens at spine index 0.
      if (type == "start" && !self->hasExplicitStartReference) {
        LOG_DBG("COF", "Found %s reference in guide: %s", type.c_str(), guideHref.c_str());
        self->textReferenceHref = guideHref;
        self->hasExplicitStartReference = type == "start";
      } else if ((type == "cover" || type == "cover-page") && self->guideCoverPageHref.empty()) {
        LOG_DBG("COF", "Found cover reference in guide: %s", guideHref.c_str());
        self->guideCoverPageHref = guideHref;
      }
    }
    return;
  }
}

void XMLCALL ContentOpfParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->state == IN_BOOK_TITLE) {
    appendBounded(self->title, s, len, MAX_METADATA_VALUE);
    return;
  }

  if (self->state == IN_BOOK_AUTHOR) {
    appendBounded(self->author, s, len, MAX_METADATA_VALUE);
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE) {
    appendBounded(self->language, s, len, MAX_METADATA_VALUE);
    return;
  }
}

void XMLCALL ContentOpfParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)name;

  if (self->state == IN_SPINE && xmlLocalNameEquals(name, "spine")) {
    self->state = IN_PACKAGE;
    if (self->tempItemStore && !self->tempItemStore.close()) self->failed = true;
    return;
  }

  if (self->state == IN_GUIDE && xmlLocalNameEquals(name, "guide")) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_MANIFEST && xmlLocalNameEquals(name, "manifest")) {
    self->state = IN_PACKAGE;
    if (self->tempItemStore && !self->tempItemStore.close()) self->failed = true;
    return;
  }

  if (self->state == IN_BOOK_TITLE && xmlLocalNameEquals(name, "title")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR && xmlLocalNameEquals(name, "creator")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE && xmlLocalNameEquals(name, "language")) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && xmlLocalNameEquals(name, "metadata")) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && xmlLocalNameEquals(name, "package")) {
    self->state = START;
    return;
  }
}
