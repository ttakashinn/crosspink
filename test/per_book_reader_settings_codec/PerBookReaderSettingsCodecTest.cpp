#include <gtest/gtest.h>

#include "PerBookReaderSettingsCodec.h"

CrossPointSettings SETTINGS;

TEST(PerBookReaderSettingsCodec, RoundTripsVietnameseSdFontName) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.overrideMask = PerBookReaderSettings::OVERRIDE_FONT_SIZE | PerBookReaderSettings::OVERRIDE_LINE_SPACING |
                          PerBookReaderSettings::OVERRIDE_MARGIN;
  settings.fontPointSize = 18;
  settings.lineSpacing = CrossPointSettings::WIDE;
  settings.screenMargin = 15;
  settings.wordSpacing = 2;
  settings.repairParagraphIndent = 1;
  settings.autoPageTurnSeconds = 45;
  settings.preferredRenderMode = 1;
  settings.lastWorkingFallback = 2;
  settings.fallbackRenderSignature = 0x12345678U;
  const char name[] = "Văn Nhân Serif";
  std::copy(std::begin(name), std::end(name), settings.sdFontFamilyName.begin());
  settings.hasDictionaryOverride = true;
  const char dictionary[] = "Việt-Anh";
  std::copy(std::begin(dictionary), std::end(dictionary), settings.dictionaryName.begin());

  PerBookReaderSettingsCodec::Encoded encoded;
  ASSERT_TRUE(PerBookReaderSettingsCodec::encode(settings, encoded));
  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(encoded.data(), encoded.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::OK);
  EXPECT_EQ(decoded, settings);
}

TEST(PerBookReaderSettingsCodec, DetectsPayloadCorruptionAndTrailingBytes) {
  PerBookReaderSettings settings;
  PerBookReaderSettingsCodec::Encoded encoded;
  ASSERT_TRUE(PerBookReaderSettingsCodec::encode(settings, encoded));
  encoded.back() ^= 0x40;
  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(encoded.data(), encoded.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::BAD_CRC);
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(encoded.data(), encoded.size() - 1, decoded),
            PerBookReaderSettingsCodec::DecodeStatus::TRUNCATED);
}

TEST(PerBookReaderSettingsCodec, RejectsInconsistentOverrideFlagAndMask) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.overrideMask = PerBookReaderSettings::OVERRIDE_FONT_SIZE;
  PerBookReaderSettingsCodec::Encoded encoded;
  ASSERT_TRUE(PerBookReaderSettingsCodec::encode(settings, encoded));
  uint8_t* payload = encoded.data() + PerBookReaderSettingsCodec::PAYLOAD_OFFSET;
  payload[0] = 0;
  PerBookReaderSettingsCodec::writeU32(
      encoded.data() + PerBookReaderSettingsCodec::CRC_OFFSET,
      PerBookReaderSettingsCodec::crc32(payload, PerBookReaderSettingsCodec::PAYLOAD_SIZE));

  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(encoded.data(), encoded.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::INVALID_VALUE);
}

TEST(PerBookReaderSettingsCodec, RefusesUnknownFutureFormat) {
  PerBookReaderSettings settings;
  PerBookReaderSettingsCodec::Encoded encoded;
  ASSERT_TRUE(PerBookReaderSettingsCodec::encode(settings, encoded));
  encoded[PerBookReaderSettingsCodec::VERSION_OFFSET]++;
  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(encoded.data(), encoded.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::NEWER_VERSION);
}

TEST(PerBookReaderSettingsCodec, RejectsInvalidValuesBeforeWriting) {
  PerBookReaderSettings settings;
  PerBookReaderSettingsCodec::Encoded encoded;
  settings.screenMargin = 7;
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.screenMargin = 10;
  settings.fontPointSize = 0;
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.fontPointSize = 16;
  settings.autoPageTurnSeconds = 4;
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.autoPageTurnSeconds = 30;
  settings.preferredRenderMode = 3;
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.preferredRenderMode = 0;
  settings.dictionaryName[0] = '.';
  settings.dictionaryName[1] = '\0';
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.dictionaryName = {};
  settings.dictionaryName[0] = '/';
  settings.dictionaryName[1] = '\0';
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
  settings.dictionaryName = {};
  settings.hasOverrides = true;
  EXPECT_FALSE(PerBookReaderSettingsCodec::encode(settings, encoded));
}

TEST(PerBookReaderSettingsCodec, MigratesVersionOneWithSafeExtensionDefaults) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.fontPointSize = 19;

  std::array<uint8_t, PerBookReaderSettingsCodec::PAYLOAD_OFFSET + PerBookReaderSettingsCodec::V1_PAYLOAD_SIZE> v1{};
  std::copy(PerBookReaderSettingsCodec::MAGIC.begin(), PerBookReaderSettingsCodec::MAGIC.end(), v1.begin());
  v1[PerBookReaderSettingsCodec::VERSION_OFFSET] = 1;
  PerBookReaderSettingsCodec::writeU16(v1.data() + PerBookReaderSettingsCodec::LENGTH_OFFSET,
                                       PerBookReaderSettingsCodec::V1_PAYLOAD_SIZE);
  uint8_t* payload = v1.data() + PerBookReaderSettingsCodec::PAYLOAD_OFFSET;
  payload[0] = 1;
  payload[1] = settings.fontFamily;
  payload[2] = settings.fontPointSize;
  payload[3] = settings.lineSpacing;
  payload[4] = settings.paragraphAlignment;
  payload[5] = settings.orientation;
  payload[6] = settings.screenMargin;
  payload[7] = settings.embeddedStyle;
  payload[8] = settings.focusReadingEnabled;
  payload[9] = settings.hyphenationEnabled;
  payload[10] = settings.extraParagraphSpacing;
  payload[11] = settings.textAntiAliasing;
  payload[12] = settings.imageRendering;
  std::memcpy(payload + 13, settings.sdFontFamilyName.data(), settings.sdFontFamilyName.size());
  PerBookReaderSettingsCodec::writeU32(
      v1.data() + PerBookReaderSettingsCodec::CRC_OFFSET,
      PerBookReaderSettingsCodec::crc32(payload, PerBookReaderSettingsCodec::V1_PAYLOAD_SIZE));

  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(v1.data(), v1.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::OK);
  EXPECT_TRUE(decoded.hasOverrides);
  EXPECT_EQ(decoded.overrideMask, PerBookReaderSettings::ALL_READER_OVERRIDE_FIELDS);
  EXPECT_EQ(decoded.fontPointSize, 19);
  EXPECT_EQ(decoded.wordSpacing, 0);
  EXPECT_EQ(decoded.repairParagraphIndent, 0);
  EXPECT_EQ(decoded.autoPageTurnSeconds, 0);
  EXPECT_EQ(decoded.preferredRenderMode, 0);
  EXPECT_EQ(decoded.lastWorkingFallback, UINT8_MAX);
  EXPECT_EQ(decoded.fallbackRenderSignature, 0U);
  EXPECT_FALSE(decoded.hasDictionaryOverride);
  EXPECT_STREQ(decoded.dictionaryName.data(), "");
}

TEST(PerBookReaderSettingsCodec, MigratesVersionTwoWithoutInventingDictionaryOverride) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.fontPointSize = 20;
  settings.wordSpacing = 1;

  std::array<uint8_t, PerBookReaderSettingsCodec::PAYLOAD_OFFSET + PerBookReaderSettingsCodec::V2_PAYLOAD_SIZE> v2{};
  std::copy(PerBookReaderSettingsCodec::MAGIC.begin(), PerBookReaderSettingsCodec::MAGIC.end(), v2.begin());
  v2[PerBookReaderSettingsCodec::VERSION_OFFSET] = 2;
  PerBookReaderSettingsCodec::writeU16(v2.data() + PerBookReaderSettingsCodec::LENGTH_OFFSET,
                                       PerBookReaderSettingsCodec::V2_PAYLOAD_SIZE);
  uint8_t* payload = v2.data() + PerBookReaderSettingsCodec::PAYLOAD_OFFSET;
  payload[0] = 1;
  payload[1] = settings.fontFamily;
  payload[2] = settings.fontPointSize;
  payload[3] = settings.lineSpacing;
  payload[4] = settings.paragraphAlignment;
  payload[5] = settings.orientation;
  payload[6] = settings.screenMargin;
  payload[7] = settings.embeddedStyle;
  payload[8] = settings.focusReadingEnabled;
  payload[9] = settings.hyphenationEnabled;
  payload[10] = settings.extraParagraphSpacing;
  payload[11] = settings.textAntiAliasing;
  payload[12] = settings.imageRendering;
  std::memcpy(payload + 13, settings.sdFontFamilyName.data(), settings.sdFontFamilyName.size());
  payload[45] = settings.wordSpacing;
  payload[46] = settings.repairParagraphIndent;
  PerBookReaderSettingsCodec::writeU16(payload + 47, settings.autoPageTurnSeconds);
  payload[49] = settings.preferredRenderMode;
  payload[50] = settings.lastWorkingFallback;
  PerBookReaderSettingsCodec::writeU32(payload + 51, settings.fallbackRenderSignature);
  PerBookReaderSettingsCodec::writeU32(
      v2.data() + PerBookReaderSettingsCodec::CRC_OFFSET,
      PerBookReaderSettingsCodec::crc32(payload, PerBookReaderSettingsCodec::V2_PAYLOAD_SIZE));

  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(v2.data(), v2.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::OK);
  EXPECT_EQ(decoded.fontPointSize, 20);
  EXPECT_EQ(decoded.wordSpacing, 1);
  EXPECT_EQ(decoded.overrideMask, PerBookReaderSettings::ALL_READER_OVERRIDE_FIELDS);
  EXPECT_FALSE(decoded.hasDictionaryOverride);
  EXPECT_STREQ(decoded.dictionaryName.data(), "");
}

TEST(PerBookReaderSettingsCodec, MigratesVersionThreeAsAllTypographyFieldsOverridden) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.overrideMask = PerBookReaderSettings::ALL_READER_OVERRIDE_FIELDS;
  settings.fontPointSize = 21;
  settings.hasDictionaryOverride = true;
  const char dictionary[] = "Anh-Việt";
  std::copy(std::begin(dictionary), std::end(dictionary), settings.dictionaryName.begin());

  PerBookReaderSettingsCodec::Encoded current{};
  ASSERT_TRUE(PerBookReaderSettingsCodec::encode(settings, current));
  std::array<uint8_t, PerBookReaderSettingsCodec::PAYLOAD_OFFSET + PerBookReaderSettingsCodec::V3_PAYLOAD_SIZE> v3{};
  std::copy_n(current.begin(), v3.size(), v3.begin());
  v3[PerBookReaderSettingsCodec::VERSION_OFFSET] = 3;
  PerBookReaderSettingsCodec::writeU16(v3.data() + PerBookReaderSettingsCodec::LENGTH_OFFSET,
                                       PerBookReaderSettingsCodec::V3_PAYLOAD_SIZE);
  const uint8_t* payload = v3.data() + PerBookReaderSettingsCodec::PAYLOAD_OFFSET;
  PerBookReaderSettingsCodec::writeU32(
      v3.data() + PerBookReaderSettingsCodec::CRC_OFFSET,
      PerBookReaderSettingsCodec::crc32(payload, PerBookReaderSettingsCodec::V3_PAYLOAD_SIZE));

  PerBookReaderSettings decoded;
  EXPECT_EQ(PerBookReaderSettingsCodec::decode(v3.data(), v3.size(), decoded),
            PerBookReaderSettingsCodec::DecodeStatus::OK);
  EXPECT_TRUE(decoded.hasOverrides);
  EXPECT_EQ(decoded.overrideMask, PerBookReaderSettings::ALL_READER_OVERRIDE_FIELDS);
  EXPECT_EQ(decoded.fontPointSize, 21);
  EXPECT_TRUE(decoded.hasDictionaryOverride);
  EXPECT_STREQ(decoded.dictionaryName.data(), dictionary);
}

TEST(PerBookReaderSettings, MergesOnlyFieldsMarkedAsBookOverrides) {
  PerBookReaderSettings global;
  global.fontPointSize = 14;
  global.lineSpacing = CrossPointSettings::NORMAL;
  global.imageRendering = CrossPointSettings::IMAGES_DISPLAY;

  PerBookReaderSettings book = global;
  book.hasOverrides = true;
  book.overrideMask = PerBookReaderSettings::OVERRIDE_FONT_SIZE;
  book.fontPointSize = 20;
  book.lineSpacing = CrossPointSettings::WIDE;                // stale, but not overridden
  book.imageRendering = CrossPointSettings::IMAGES_SUPPRESS;  // stale, but not overridden
  book.wordSpacing = 2;

  const PerBookReaderSettings merged = mergeReaderSettings(global, book);
  EXPECT_EQ(merged.fontPointSize, 20);
  EXPECT_EQ(merged.lineSpacing, global.lineSpacing);
  EXPECT_EQ(merged.imageRendering, global.imageRendering);
  EXPECT_EQ(merged.wordSpacing, 2);
}

TEST(PerBookReaderSettings, ComputesOverridesPerFieldAndClearsValuesEqualToGlobal) {
  PerBookReaderSettings global;
  PerBookReaderSettings current = global;
  current.orientation = CrossPointSettings::LANDSCAPE_CW;
  current.extraParagraphSpacing = global.extraParagraphSpacing;

  EXPECT_EQ(readerSettingsOverrideMask(current, global), PerBookReaderSettings::OVERRIDE_ORIENTATION);
  current.orientation = global.orientation;
  EXPECT_EQ(readerSettingsOverrideMask(current, global), 0);
}
