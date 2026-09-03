#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <unordered_map>

#include "network/WebFontCatalogJson.h"

namespace catalog_json = web_font_catalog_json;

struct SinkState {
  std::string output;
  size_t maxChunk = 0;
  size_t calls = 0;
  size_t failAt = static_cast<size_t>(-1);
};

bool collect(void* context, const char* data, const size_t size) {
  auto& state = *static_cast<SinkState*>(context);
  if (state.calls++ == state.failAt) return false;
  state.maxChunk = std::max(state.maxChunk, size);
  state.output.append(data, size);
  return true;
}

unsigned long lookupSize(void* context, const char* path) {
  const auto& sizes = *static_cast<std::unordered_map<std::string, unsigned long>*>(context);
  const auto it = sizes.find(path);
  return it == sizes.end() ? 0 : it->second;
}

TEST(WebFontCatalogJson, StreamsExactCompatibleSchemaWithEscapingAndSortedSizes) {
  SdCardFontFamilyInfo family;
  family.name = "Bookerly \"NFD\"\n";
  family.files = {{"/.fonts/Bookerly/Bookerly_18.cpfont", 18, 0},
                  {"/.fonts/Bookerly/Bookerly_12.cpfont", 12, 0},
                  {"/.fonts/Bookerly/Bookerly_18-duplicate.cpfont", 18, 1}};
  std::vector<SdCardFontFamilyInfo> families{family};
  std::unordered_map<std::string, unsigned long> sizes{
      {family.files[0].path, 1800}, {family.files[1].path, 1200}, {family.files[2].path, 1810}};
  SinkState sink;

  ASSERT_TRUE(catalog_json::stream(families, 128, collect, &sink, lookupSize, &sizes));
  EXPECT_EQ(sink.output,
            "{\"families\":[{\"name\":\"Bookerly \\\"NFD\\\"\\n\",\"sizes\":[12,18],\"files\":["
            "{\"name\":\"Bookerly_18.cpfont\",\"size\":1800},"
            "{\"name\":\"Bookerly_12.cpfont\",\"size\":1200},"
            "{\"name\":\"Bookerly_18-duplicate.cpfont\",\"size\":1810}]"
            "}],\"maxFamilies\":128}");
  EXPECT_LE(sink.maxChunk, catalog_json::SCRATCH_BYTES);
}

TEST(WebFontCatalogJson, PeakSerializationChunkDoesNotGrowWithCatalog) {
  std::vector<SdCardFontFamilyInfo> families;
  families.reserve(128);
  for (int familyIndex = 0; familyIndex < 128; ++familyIndex) {
    SdCardFontFamilyInfo family;
    family.name = std::string(127, static_cast<char>('A' + familyIndex % 26));
    family.files.reserve(255);
    for (int pointSize = 1; pointSize <= 255; ++pointSize) {
      family.files.push_back(
          {"/.fonts/Stress/Stress_" + std::to_string(pointSize) + ".cpfont", static_cast<uint8_t>(pointSize), 0});
    }
    families.push_back(std::move(family));
  }
  SinkState sink;

  ASSERT_TRUE(catalog_json::stream(families, 128, collect, &sink, nullptr, nullptr));
  EXPECT_LE(sink.maxChunk, catalog_json::SCRATCH_BYTES);
  EXPECT_GT(sink.output.size(), 1000000U);
  EXPECT_EQ(sink.output.rfind("],\"maxFamilies\":128}"), sink.output.size() - 20);
}

TEST(WebFontCatalogJson, StopsWhenSocketSinkFails) {
  SinkState sink;
  sink.failAt = 2;
  EXPECT_FALSE(catalog_json::stream({}, 128, collect, &sink, nullptr, nullptr));
}
