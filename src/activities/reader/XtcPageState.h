#pragma once

#include <atomic>
#include <cstdint>

// XTC input runs on the main task while page rendering runs on the dedicated
// render task. Keep the requested cursor atomic and publish the visible cursor
// only after a render completes, so status/progress/telemetry can use a stable
// page snapshot without blocking input for an e-ink refresh.
class XtcPageState final {
  std::atomic<uint32_t> requestedPage_{0};
  std::atomic<uint32_t> visiblePage_{0};

 public:
  uint32_t requestedPage() const { return requestedPage_.load(std::memory_order_relaxed); }
  uint32_t visiblePage() const { return visiblePage_.load(std::memory_order_relaxed); }

  void initialize(const uint32_t page) {
    requestedPage_.store(page, std::memory_order_relaxed);
    visiblePage_.store(page, std::memory_order_relaxed);
  }

  void request(const uint32_t page) { requestedPage_.store(page, std::memory_order_relaxed); }
  void markVisible(const uint32_t page) { visiblePage_.store(page, std::memory_order_relaxed); }

  bool turn(const bool forward, const uint32_t pageCount) {
    const uint32_t page = requestedPage();
    if (forward) {
      // pageCount is the intentional end-of-book sentinel.
      if (page >= pageCount) return false;
      request(page + 1U);
      return true;
    }
    if (page == 0) return false;
    request(page - 1U);
    return true;
  }

  bool skip(const int amount, const uint32_t pageCount) {
    const int64_t page = requestedPage();
    int64_t next = page + static_cast<int64_t>(amount);
    if (next < 0) next = 0;
    if (next > static_cast<int64_t>(pageCount)) next = pageCount;
    if (next == page) return false;
    request(static_cast<uint32_t>(next));
    return true;
  }

  bool atEnd(const uint32_t pageCount) const { return requestedPage() >= pageCount; }

  void returnFromEnd(const uint32_t pageCount) { request(pageCount > 0 ? pageCount - 1U : 0U); }
};
