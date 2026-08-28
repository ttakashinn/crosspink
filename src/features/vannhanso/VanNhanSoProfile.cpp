#include "VanNhanSoProfile.h"

#include <CrossPointSettings.h>

#include <cstdio>
#include <cstring>
#include <iterator>

namespace vannhanso_profile {
namespace {
const char* safeParam(const char* const* values, const size_t count, const uint8_t index) {
  return values[index < count ? index : 0];
}

uint32_t fnv1a(const char* value) {
  uint32_t hash = 2166136261U;
  while (*value != '\0') {
    hash ^= static_cast<uint8_t>(*value++);
    hash *= 16777619U;
  }
  return hash;
}

bool buildPath(const int screenWidth, const int screenHeight, const char* extension, char* output,
               const size_t outputSize) {
  const int written = snprintf(output, outputSize, "%s/%dx%d-%08lx.%s", CACHE_DIRECTORY, screenWidth, screenHeight,
                               static_cast<unsigned long>(identityHash(screenWidth, screenHeight)), extension);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}
}  // namespace

bool buildQuery(char* output, const size_t outputSize) {
  static constexpr const char* LAYOUTS[] = {"minimal", "full"};
  static constexpr const char* FONT_SIZES[] = {"standard", "large"};
  static constexpr const char* VOCABULARY_LEVELS[] = {"b1", "b2", "c1", "c2", "mixed"};
  static constexpr const char* WEATHER_LOCATIONS[] = {"hanoi",  "hochiminh", "danang", "haiphong",
                                                      "cantho", "hue",       "dongnai"};

  const bool minimal = SETTINGS.vanNhanSoLayout == CrossPointSettings::VANNHANSO_LAYOUT_MINIMAL;
  const int written =
      minimal ? snprintf(output, outputSize, "layout=minimal&font=%s&finance=0&grayscale=1",
                         safeParam(FONT_SIZES, std::size(FONT_SIZES), SETTINGS.vanNhanSoFontSize))
              : snprintf(output, outputSize, "font=%s&vocab=%s&weather=%s&finance=%u&grayscale=1",
                         safeParam(FONT_SIZES, std::size(FONT_SIZES), SETTINGS.vanNhanSoFontSize),
                         safeParam(VOCABULARY_LEVELS, std::size(VOCABULARY_LEVELS), SETTINGS.vanNhanSoVocabularyLevel),
                         safeParam(WEATHER_LOCATIONS, std::size(WEATHER_LOCATIONS), SETTINGS.vanNhanSoWeatherLocation),
                         SETTINGS.vanNhanSoFinance ? 1U : 0U);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

bool buildIdentity(const int screenWidth, const int screenHeight, char* output, const size_t outputSize) {
  char query[QUERY_MAX_LENGTH];
  if (!buildQuery(query, sizeof(query))) return false;
  const int written = snprintf(output, outputSize, "%dx%d|%s", screenWidth, screenHeight, query);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

uint32_t identityHash(const int screenWidth, const int screenHeight) {
  char identity[ID_MAX_LENGTH];
  return buildIdentity(screenWidth, screenHeight, identity, sizeof(identity)) ? fnv1a(identity) : 0;
}

bool buildImagePath(const int screenWidth, const int screenHeight, char* output, const size_t outputSize) {
  return buildPath(screenWidth, screenHeight, "bmp", output, outputSize);
}

bool buildBackupPath(const int screenWidth, const int screenHeight, char* output, const size_t outputSize) {
  return buildPath(screenWidth, screenHeight, "bak", output, outputSize);
}

bool buildDatePath(const int screenWidth, const int screenHeight, char* output, const size_t outputSize) {
  return buildPath(screenWidth, screenHeight, "date", output, outputSize);
}

bool buildManifestUrl(const char* baseUrl, char* output, const size_t outputSize) {
  char query[QUERY_MAX_LENGTH];
  if (!baseUrl || !buildQuery(query, sizeof(query))) return false;
  const int written = snprintf(output, outputSize, "%s?%s", baseUrl, query);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

}  // namespace vannhanso_profile
