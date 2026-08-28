#include <gtest/gtest.h>

#include "PerBookReaderSettingsCodec.h"

CrossPointSettings SETTINGS;

TEST(PerBookReaderSettingsCodec, RoundTripsVietnameseSdFontName) {
  PerBookReaderSettings settings;
  settings.hasOverrides = true;
  settings.fontPointSize = 18;
  settings.lineSpacing = CrossPointSettings::WIDE;
  settings.screenMargin = 15;
  const char name[] = "Văn Nhân Serif";
  std::copy(std::begin(name), std::end(name), settings.sdFontFamilyName.begin());

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
}
