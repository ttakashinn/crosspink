#pragma once

#include <cstdint>

// Allocation-free write coalescing. A reader persists after five real position
// changes or two minutes, whichever comes first, and explicitly flushes on
// exit/sync/destructive cache operations. Metadata changes do not masquerade
// as page turns, and a stale save acknowledgement cannot clear newer state.
class ReaderProgressSaveDebouncer {
 public:
  static constexpr uint8_t PAGE_CHANGE_INTERVAL = 5;
  static constexpr uint32_t MAX_SAVE_INTERVAL_MS = 2UL * 60UL * 1000UL;

  void seedPersisted(const uint32_t positionKey, const uint32_t metadataKey, const uint32_t nowMs) {
    initialized_ = true;
    lastPositionKey_ = positionKey;
    lastMetadataKey_ = metadataKey;
    lastPersistedAtMs_ = nowMs;
    pendingPageChanges_ = 0;
    pending_ = false;
  }

  bool observe(const uint32_t positionKey, const uint32_t metadataKey, const uint32_t nowMs) {
    if (!initialized_) {
      initialized_ = true;
      lastPositionKey_ = positionKey;
      lastMetadataKey_ = metadataKey;
      lastPersistedAtMs_ = nowMs;
      pendingPageChanges_ = 1;
      pending_ = true;
      return false;
    }

    if (positionKey != lastPositionKey_) {
      lastPositionKey_ = positionKey;
      pending_ = true;
      if (pendingPageChanges_ < UINT8_MAX) ++pendingPageChanges_;
    }
    if (metadataKey != lastMetadataKey_) {
      lastMetadataKey_ = metadataKey;
      pending_ = true;
    }
    return due(nowMs);
  }

  [[nodiscard]] bool due(const uint32_t nowMs) const {
    return pending_ && (pendingPageChanges_ >= PAGE_CHANGE_INTERVAL ||
                        static_cast<uint32_t>(nowMs - lastPersistedAtMs_) >= MAX_SAVE_INTERVAL_MS);
  }
  [[nodiscard]] bool hasPending() const { return pending_; }
  [[nodiscard]] uint32_t lastObservedPosition() const { return lastPositionKey_; }
  [[nodiscard]] uint32_t lastObservedMetadata() const { return lastMetadataKey_; }

  bool markPersisted(const uint32_t positionKey, const uint32_t metadataKey, const uint32_t nowMs) {
    if (!initialized_ || positionKey != lastPositionKey_ || metadataKey != lastMetadataKey_) return false;
    pending_ = false;
    pendingPageChanges_ = 0;
    lastPersistedAtMs_ = nowMs;
    return true;
  }

  void markAttemptFailed(const uint32_t nowMs) {
    if (!initialized_ || !pending_) return;
    // Retain the newest observed state, but avoid a render/write retry storm.
    // Five further page changes or another full time interval will retry.
    pendingPageChanges_ = 0;
    lastPersistedAtMs_ = nowMs;
  }

 private:
  uint32_t lastPositionKey_ = 0;
  uint32_t lastMetadataKey_ = 0;
  uint32_t lastPersistedAtMs_ = 0;
  uint8_t pendingPageChanges_ = 0;
  bool initialized_ = false;
  bool pending_ = false;
};
