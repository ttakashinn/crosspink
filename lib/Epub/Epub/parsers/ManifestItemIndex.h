#pragma once

#include <Arena.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

// A bounded, fallibly allocated manifest index. The complete item IDs and
// hrefs remain in the temporary on-disk store; this index only keeps compact
// lookup keys and offsets, so allocation failure can safely fall back to a
// linear file scan.
class ManifestItemIndex final {
 public:
  struct Entry {
    uint32_t idHash;
    uint16_t idLen;
    uint32_t fileOffset;
  };

  enum class CandidateResult { Continue, Found, Error };

  static constexpr size_t DEFAULT_SLAB_BYTES = 4096;
  static constexpr size_t DEFAULT_MAX_BYTES = 13 * DEFAULT_SLAB_BYTES;

  explicit ManifestItemIndex(const size_t slabBytes = DEFAULT_SLAB_BYTES, const size_t maxBytes = DEFAULT_MAX_BYTES)
      : slabBytes_(slabBytes), maxBytes_(maxBytes) {}

  bool append(const Entry& entry) {
    static_assert(sizeof(Chunk) <= DEFAULT_SLAB_BYTES);
    if (!arena_.initialized() && !arena_.init(slabBytes_, maxBytes_)) return false;

    if (!tail_ || tail_->count == CHUNK_CAPACITY) {
      auto* const chunk = arenaNew<Chunk>(arena_);
      if (!chunk) return false;
      if (tail_) {
        tail_->next = chunk;
      } else {
        head_ = chunk;
      }
      tail_ = chunk;
      ++chunkCount_;
    }

    tail_->entries[tail_->count++] = entry;
    ++size_;
    return true;
  }

  void sort() {
    for (auto* chunk = head_; chunk; chunk = chunk->next) {
      std::sort(chunk->entries, chunk->entries + chunk->count, entryLess);
    }
  }

  template <typename Visitor>
  CandidateResult visitCandidates(const uint32_t idHash, const uint16_t idLen, Visitor&& visitor) const {
    const Entry target{idHash, idLen, 0};
    for (const auto* chunk = head_; chunk; chunk = chunk->next) {
      const Entry* const begin = chunk->entries;
      const Entry* const end = begin + chunk->count;
      const Entry* it = std::lower_bound(begin, end, target, entryLess);
      while (it != end && it->idHash == idHash && it->idLen == idLen) {
        const CandidateResult result = visitor(it->fileOffset);
        if (result != CandidateResult::Continue) return result;
        ++it;
      }
    }
    return CandidateResult::Continue;
  }

  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] size_t size() const { return size_; }
  [[nodiscard]] size_t chunkCount() const { return chunkCount_; }
  [[nodiscard]] size_t memoryUsed() const { return arena_.used(); }
  [[nodiscard]] size_t memoryCapacity() const { return arena_.capacity(); }

 private:
  static constexpr size_t CHUNK_CAPACITY = 320;

  struct Chunk {
    Chunk* next = nullptr;
    uint16_t count = 0;
    Entry entries[CHUNK_CAPACITY];
  };

  static bool entryLess(const Entry& lhs, const Entry& rhs) {
    return lhs.idHash < rhs.idHash || (lhs.idHash == rhs.idHash && lhs.idLen < rhs.idLen);
  }

  Arena arena_;
  Chunk* head_ = nullptr;
  Chunk* tail_ = nullptr;
  size_t size_ = 0;
  size_t chunkCount_ = 0;
  size_t slabBytes_;
  size_t maxBytes_;
};
