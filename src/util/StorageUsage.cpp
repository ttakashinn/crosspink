#include "StorageUsage.h"

#include <algorithm>
#include <cstdio>

#if defined(SIMULATOR)
#include <sys/statvfs.h>

#include <cstdlib>
#else
#include <HalStorage.h>
#endif

namespace storage_usage {

namespace {
constexpr uint64_t MEBIBYTE = 1024ULL * 1024ULL;
constexpr uint64_t GIBIBYTE = 1024ULL * MEBIBYTE;

uint64_t roundedTenths(const uint64_t bytes, const uint64_t unit) { return (bytes * 10ULL + unit / 2ULL) / unit; }
}  // namespace

Snapshot read() {
#if defined(SIMULATOR)
  const char* root = std::getenv("CROSSPOINT_SIM_SD");
  if (!root || !*root) root = std::getenv("CROSSPOINT_EMU_SD");
  if (!root || !*root) root = "./fs_";

  struct statvfs stats{};
  if (statvfs(root, &stats) != 0) return {};
  const uint64_t total = static_cast<uint64_t>(stats.f_blocks) * stats.f_frsize;
  const uint64_t available = static_cast<uint64_t>(stats.f_bavail) * stats.f_frsize;
  return {total >= available ? total - available : 0, total};
#else
  return {Storage.usedBytes(), Storage.totalBytes()};
#endif
}

uint8_t percent(const Snapshot& usage) {
  if (!usage.available()) return 0;
  const uint64_t used = std::min(usage.usedBytes, usage.totalBytes);
  return static_cast<uint8_t>((used * 100ULL) / usage.totalBytes);
}

bool format(const Snapshot& usage, char* output, const size_t outputSize) {
  if (!output || outputSize == 0) return false;
  output[0] = '\0';
  if (!usage.available()) return false;

  const uint64_t used = std::min(usage.usedBytes, usage.totalBytes);
  const uint64_t unit = usage.totalBytes >= GIBIBYTE ? GIBIBYTE : MEBIBYTE;
  const char* unitName = unit == GIBIBYTE ? "GiB" : "MiB";
  const uint64_t usedTenths = roundedTenths(used, unit);
  const uint64_t totalTenths = roundedTenths(usage.totalBytes, unit);
  const int written = snprintf(
      output, outputSize, "%llu.%llu / %llu.%llu %s (%u%%)", static_cast<unsigned long long>(usedTenths / 10ULL),
      static_cast<unsigned long long>(usedTenths % 10ULL), static_cast<unsigned long long>(totalTenths / 10ULL),
      static_cast<unsigned long long>(totalTenths % 10ULL), unitName, percent(usage));
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

}  // namespace storage_usage
