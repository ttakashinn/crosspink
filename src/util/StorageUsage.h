#pragma once

#include <cstddef>
#include <cstdint>

namespace storage_usage {

struct Snapshot {
  uint64_t usedBytes = 0;
  uint64_t totalBytes = 0;

  bool available() const { return totalBytes != 0; }
};

Snapshot read();
uint8_t percent(const Snapshot& usage);
bool format(const Snapshot& usage, char* output, size_t outputSize);

}  // namespace storage_usage
