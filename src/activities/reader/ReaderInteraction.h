#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace reader_interaction {

#ifndef CROSSPOINT_POST_VISIBLE_IDLE_MS
#define CROSSPOINT_POST_VISIBLE_IDLE_MS 1000
#endif
constexpr uint32_t POST_VISIBLE_IDLE_MS = CROSSPOINT_POST_VISIBLE_IDLE_MS;

// A page number is not a stable reading position: changing a font or a line
// setting moves every following page boundary.  Keep an anchor inside the
// visible page instead.  The midpoint is intentional: on a reflow it keeps
// the text being read in view, whereas anchoring the page start invariably
// makes a larger font appear to jump backwards.
inline std::optional<uint32_t> readingAnchorAtPageCenter(const std::optional<uint32_t> pageStart,
                                                         const std::optional<uint32_t> nextPageStart) {
  if (!pageStart.has_value()) return std::nullopt;
  if (!nextPageStart.has_value() || *nextPageStart <= *pageStart) return pageStart;
  return *pageStart + (*nextPageStart - *pageStart) / 2U;
}

// Optional reader work may run only after a real page has reached the panel
// and neither a later input edge nor another visible page restarted the quiet
// window. Unsigned subtraction deliberately keeps millis() wraparound safe.
class PostVisibleIdleGuard {
 public:
  void reset() {
    pageVisible_.store(false, std::memory_order_relaxed);
    idleSinceMs_.store(0, std::memory_order_relaxed);
  }

  void pageVisible(const uint32_t atMs) {
    idleSinceMs_.store(atMs, std::memory_order_relaxed);
    pageVisible_.store(true, std::memory_order_release);
  }

  void noteInput(const uint32_t atMs) {
    if (pageVisible_.load(std::memory_order_acquire)) idleSinceMs_.store(atMs, std::memory_order_relaxed);
  }

  bool canRunDeferredWork(const uint32_t nowMs) const {
    return pageVisible_.load(std::memory_order_acquire) &&
           nowMs - idleSinceMs_.load(std::memory_order_relaxed) >= POST_VISIBLE_IDLE_MS;
  }

 private:
  std::atomic<bool> pageVisible_{false};
  std::atomic<uint32_t> idleSinceMs_{0};
};

struct TurnSnapshot {
  uint32_t sequence = 0;
  int8_t direction = 0;
  int8_t queueDepth = 0;
  uint32_t inputAtMs = 0;
  uint32_t queuedAtMs = 0;
  uint32_t renderBeginAtMs = 0;
  uint32_t visibleAtMs = 0;
  int32_t beforePrimary = -1;
  int32_t beforeSecondary = -1;
  int32_t afterPrimary = -1;
  int32_t afterSecondary = -1;
};

// Fixed-size, allocation-free FIFO correlation state. A single latest-record
// slot loses earlier edges when several page turns are queued before the first
// render; this ring preserves those bursts without allocating on the hot path.
class TurnTelemetry {
 public:
  uint32_t input(const uint32_t atMs, const int direction, const int32_t beforePrimary = -1,
                 const int32_t beforeSecondary = -1) {
    Guard guard(lock_);
    const uint32_t next = ++sequence_;
    if (count_ == records_.size()) {
      head_ = (head_ + 1) % records_.size();
      --count_;
    }
    const size_t tail = (head_ + count_) % records_.size();
    records_[tail] = {
        next, static_cast<int8_t>(direction < 0 ? -1 : 1), 0, atMs, 0, 0, 0, beforePrimary, beforeSecondary, -1, -1};
    ++count_;
    return next;
  }

  void queued(const uint32_t atMs, const int queueDepth) {
    Guard guard(lock_);
    if (count_ == 0) return;
    auto& record = newest();
    if (record.queuedAtMs == 0) record.queuedAtMs = atMs;
    record.queueDepth = static_cast<int8_t>(std::clamp(queueDepth, -127, 127));
  }

  void queueDepth(const int queueDepth) {
    Guard guard(lock_);
    if (count_ == 0) return;
    newest().queueDepth = static_cast<int8_t>(std::clamp(queueDepth, -127, 127));
  }

  void cancelNewest() {
    Guard guard(lock_);
    if (count_ == 0) return;
    const size_t tail = (head_ + count_ - 1) % records_.size();
    records_[tail] = {};
    --count_;
  }

  void clear() {
    Guard guard(lock_);
    records_ = {};
    head_ = 0;
    count_ = 0;
  }

  void renderBegin(const uint32_t atMs) {
    Guard guard(lock_);
    if (count_ > 0 && records_[head_].renderBeginAtMs == 0) records_[head_].renderBeginAtMs = atMs;
  }

  TurnSnapshot visible(const uint32_t atMs, const int32_t afterPrimary = -1, const int32_t afterSecondary = -1) {
    Guard guard(lock_);
    if (count_ == 0) return {};
    auto& record = records_[head_];
    record.visibleAtMs = atMs;
    record.afterPrimary = afterPrimary;
    record.afterSecondary = afterSecondary;
    const TurnSnapshot result = record;
    head_ = (head_ + 1) % records_.size();
    --count_;
    return result;
  }

  TurnSnapshot snapshot() const {
    Guard guard(lock_);
    return count_ == 0 ? TurnSnapshot{} : records_[head_];
  }

 private:
  class Guard {
   public:
    explicit Guard(std::atomic_flag& lock) : lock_(lock) {
      while (lock_.test_and_set(std::memory_order_acquire)) {
      }
    }
    ~Guard() { lock_.clear(std::memory_order_release); }

   private:
    std::atomic_flag& lock_;
  };

  TurnSnapshot& newest() { return records_[(head_ + count_ - 1) % records_.size()]; }

  mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  std::array<TurnSnapshot, 8> records_{};
  size_t head_ = 0;
  size_t count_ = 0;
  uint32_t sequence_ = 0;
};

}  // namespace reader_interaction
