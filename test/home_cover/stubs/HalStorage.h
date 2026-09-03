#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

class HalFile {
 public:
  HalFile() = default;
  explicit HalFile(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)), open_(true) {}

  bool seek(const size_t position) {
    if (!open_ || position > bytes_.size()) return false;
    position_ = position;
    return true;
  }

  bool seekCur(const int64_t offset) {
    if (!open_) return false;
    const int64_t next = static_cast<int64_t>(position_) + offset;
    if (next < 0 || static_cast<size_t>(next) > bytes_.size()) return false;
    position_ = static_cast<size_t>(next);
    return true;
  }

  int read(void* output, const size_t count) {
    if (!open_) return -1;
    const size_t available = bytes_.size() - position_;
    const size_t readCount = std::min(count, available);
    if (readCount > 0) std::memcpy(output, bytes_.data() + position_, readCount);
    position_ += readCount;
    return static_cast<int>(readCount);
  }

  int read() {
    if (!open_ || position_ >= bytes_.size()) return -1;
    return bytes_[position_++];
  }

  uint64_t fileSize64() const { return bytes_.size(); }
  explicit operator bool() const { return open_; }

 private:
  std::vector<uint8_t> bytes_;
  size_t position_ = 0;
  bool open_ = false;
};
