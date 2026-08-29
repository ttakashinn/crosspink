#include <gtest/gtest.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "VanNhanSoProfile.h"

CrossPointSettings SETTINGS;

namespace profile = vannhanso_profile;

class VanNhanSoProfileTest : public testing::Test {
 protected:
  void SetUp() override { SETTINGS = CrossPointSettings{}; }
};

TEST_F(VanNhanSoProfileTest, EmitsNamDinhInTheCanonicalFullProfile) {
  SETTINGS.vanNhanSoFontSize = 1;
  SETTINGS.vanNhanSoVocabularyLevel = 2;
  SETTINGS.vanNhanSoWeatherLocation = CrossPointSettings::VANNHANSO_WEATHER_NAMDINH;
  SETTINGS.vanNhanSoFinance = 0;

  char query[profile::QUERY_MAX_LENGTH];
  ASSERT_TRUE(profile::buildQuery(query, sizeof(query)));
  EXPECT_STREQ(query, "font=large&vocab=c1&weather=namdinh&finance=0&grayscale=1");

  char url[256];
  ASSERT_TRUE(profile::buildManifestUrl("https://vannhanso.com/manifest/today", url, sizeof(url)));
  EXPECT_STREQ(url, "https://vannhanso.com/manifest/today?font=large&vocab=c1&weather=namdinh&finance=0&grayscale=1");
}

TEST_F(VanNhanSoProfileTest, MinimalProfileDiscardsInvisibleWeatherVocabularyAndFinance) {
  SETTINGS.vanNhanSoLayout = CrossPointSettings::VANNHANSO_LAYOUT_MINIMAL;
  SETTINGS.vanNhanSoFontSize = 1;
  SETTINGS.vanNhanSoVocabularyLevel = 3;
  SETTINGS.vanNhanSoWeatherLocation = CrossPointSettings::VANNHANSO_WEATHER_NAMDINH;
  SETTINGS.vanNhanSoFinance = 1;

  char query[profile::QUERY_MAX_LENGTH];
  ASSERT_TRUE(profile::buildQuery(query, sizeof(query)));
  EXPECT_STREQ(query, "layout=minimal&font=large&finance=0&grayscale=1");
}

TEST_F(VanNhanSoProfileTest, InvalidWeatherIndexFallsBackToHanoiWithoutReadingPastTheTable) {
  SETTINGS.vanNhanSoWeatherLocation = 255;

  char query[profile::QUERY_MAX_LENGTH];
  ASSERT_TRUE(profile::buildQuery(query, sizeof(query)));
  // Hanoi is the backend default, so the canonical query omits it.
  EXPECT_EQ(std::strstr(query, "weather="), nullptr);
  EXPECT_STREQ(query, "grayscale=1");
}

TEST_F(VanNhanSoProfileTest, DefaultValuesUseTheBackendCanonicalQueryWithoutRedirectOnlyParameters) {
  char query[profile::QUERY_MAX_LENGTH];
  ASSERT_TRUE(profile::buildQuery(query, sizeof(query)));
  EXPECT_STREQ(query, "grayscale=1");
  EXPECT_EQ(std::strstr(query, "vocab=mixed"), nullptr);
  EXPECT_EQ(std::strstr(query, "weather=hanoi"), nullptr);
  EXPECT_EQ(std::strstr(query, "finance=1"), nullptr);
}

TEST_F(VanNhanSoProfileTest, IdentityChangesWithLocationAndScreenAndRejectsSmallBuffers) {
  const uint32_t hanoi = profile::identityHash(480, 800);
  SETTINGS.vanNhanSoWeatherLocation = CrossPointSettings::VANNHANSO_WEATHER_NAMDINH;
  const uint32_t namDinh = profile::identityHash(480, 800);

  EXPECT_NE(hanoi, namDinh);
  EXPECT_NE(namDinh, profile::identityHash(512, 800));

  char tooSmall[8];
  EXPECT_FALSE(profile::buildQuery(tooSmall, sizeof(tooSmall)));
}
