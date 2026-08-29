#pragma once

#include <cstdint>

#include "ReadingStats.h"

namespace ReadingStatsClock {

bool currentLocalDateTime(ReadingStatsLocalDateTime& out);
// Returns YYYYMMDD in the user's configured timezone. Zero means neither the
// external RTC nor the synchronized ESP clock is trustworthy.
uint32_t currentLocalDateKey();
bool hasPersistentWallClock();

}  // namespace ReadingStatsClock
