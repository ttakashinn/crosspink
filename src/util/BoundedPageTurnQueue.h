#pragma once

#include <cstdint>

// Allocation-free pending intent for manual page turns. Opposite input
// cancels the most recent intent instead of forcing two e-ink refreshes.
class BoundedPageTurnQueue {
 public:
  static constexpr int8_t MAX_PENDING_TURNS = 4;

  void push(const bool forward) {
    const int8_t direction = forward ? 1 : -1;
    if ((pending_ > 0 && direction < 0) || (pending_ < 0 && direction > 0)) {
      pending_ += direction;
      return;
    }
    if (pending_ < MAX_PENDING_TURNS && pending_ > -MAX_PENDING_TURNS) pending_ += direction;
  }

  int8_t pop() {
    if (pending_ == 0) return 0;
    const int8_t direction = pending_ > 0 ? 1 : -1;
    pending_ -= direction;
    return direction;
  }

  void clear() { pending_ = 0; }
  bool empty() const { return pending_ == 0; }
  int8_t pending() const { return pending_; }

 private:
  int8_t pending_ = 0;
};
