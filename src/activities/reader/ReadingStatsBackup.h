#pragma once

#include <cstddef>

namespace ReadingStatsBackup {

inline constexpr const char* BACKUP_DIRECTORY = "/.vns-stats-backup";
inline constexpr int KEEP_COUNT = 7;

// Writes a verified, current-format copy of the all-time device statistics.
// Per-book files remain the source for each title and are intentionally not
// duplicated here, matching CrossInk's backup semantics.
bool create(char* outFileName = nullptr, size_t outFileNameLength = 0);
int prune(int keep = KEEP_COUNT);

}  // namespace ReadingStatsBackup
