#pragma once

#include <cstdint>

struct CrossPointSettings {
  enum VANNHANSO_LAYOUT : uint8_t {
    VANNHANSO_LAYOUT_MINIMAL = 0,
    VANNHANSO_LAYOUT_FULL = 1,
  };

  enum VANNHANSO_WEATHER_LOCATION : uint8_t {
    VANNHANSO_WEATHER_HANOI = 0,
    VANNHANSO_WEATHER_HOCHIMINH = 1,
    VANNHANSO_WEATHER_DANANG = 2,
    VANNHANSO_WEATHER_HAIPHONG = 3,
    VANNHANSO_WEATHER_CANTHO = 4,
    VANNHANSO_WEATHER_HUE = 5,
    VANNHANSO_WEATHER_DONGNAI = 6,
    VANNHANSO_WEATHER_NAMDINH = 7,
    VANNHANSO_WEATHER_LOCATION_COUNT,
  };

  uint8_t vanNhanSoLayout = VANNHANSO_LAYOUT_FULL;
  uint8_t vanNhanSoFontSize = 0;
  uint8_t vanNhanSoVocabularyLevel = 4;
  uint8_t vanNhanSoWeatherLocation = VANNHANSO_WEATHER_HANOI;
  uint8_t vanNhanSoFinance = 1;
};

extern CrossPointSettings SETTINGS;
