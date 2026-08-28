#pragma once

#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

// Bounded chained-slab arena for burst-then-discard firmware work. Unlike the
// original CrossInk implementation, every size/alignment operation is checked,
// growth has an explicit budget, and checkpoints are validated before restore.
// Objects with non-trivial destructors must not be stored here.
class Arena {
 public:
  struct Checkpoint {
    const void* slab = nullptr;
    size_t offset = 0;
  };

  Arena() = default;
  ~Arena() { release(); }
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  bool init(const size_t slabBytes, const size_t maxBytes = std::numeric_limits<size_t>::max()) {
    release();
    if (slabBytes == 0 || maxBytes < slabBytes) return false;
    slabSize_ = slabBytes;
    maxBytes_ = maxBytes;
    head_ = current_ = allocateSlab(slabBytes);
    if (!head_) {
      release();
      LOG_ERR("ARENA", "Initial allocation failed (%u bytes)", static_cast<unsigned>(slabBytes));
      return false;
    }
    totalCapacity_ = slabBytes;
    return true;
  }

  void release() {
    Slab* slab = head_;
    while (slab) {
      Slab* next = slab->next;
      std::free(slab);
      slab = next;
    }
    head_ = nullptr;
    current_ = nullptr;
    slabSize_ = 0;
    maxBytes_ = 0;
    totalCapacity_ = 0;
    highWater_ = 0;
  }

  void clear() {
    if (!head_) return;
    Slab* slab = head_->next;
    while (slab) {
      Slab* next = slab->next;
      totalCapacity_ -= slab->capacity;
      std::free(slab);
      slab = next;
    }
    head_->next = nullptr;
    head_->offset = 0;
    current_ = head_;
  }

  [[nodiscard]] void* alloc(const size_t size, const size_t alignment = alignof(std::max_align_t)) {
    if (!current_ || size == 0 || !validAlignment(alignment)) return nullptr;
    if (void* memory = tryAllocate(*current_, size, alignment)) {
      updateHighWater();
      return memory;
    }

    size_t required = 0;
    if (!checkedAdd(size, alignment - 1, required)) return nullptr;
    const size_t capacity = required > slabSize_ ? required : slabSize_;
    if (capacity > maxBytes_ - totalCapacity_) {
      LOG_ERR("ARENA", "Budget exceeded (need=%u used=%u max=%u)", static_cast<unsigned>(capacity),
              static_cast<unsigned>(totalCapacity_), static_cast<unsigned>(maxBytes_));
      return nullptr;
    }

    Slab* next = allocateSlab(capacity);
    if (!next) {
      LOG_ERR("ARENA", "Growth allocation failed (%u bytes)", static_cast<unsigned>(capacity));
      return nullptr;
    }
    current_->next = next;
    current_ = next;
    totalCapacity_ += capacity;
    void* memory = tryAllocate(*current_, size, alignment);
    if (!memory) {
      // This should be unreachable because capacity includes alignment slop.
      LOG_ERR("ARENA", "Internal alignment failure");
      return nullptr;
    }
    updateHighWater();
    return memory;
  }

  [[nodiscard]] Checkpoint save() const { return {current_, current_ ? current_->offset : 0}; }

  bool restore(const Checkpoint& checkpoint) {
    if (!checkpoint.slab) return checkpoint.offset == 0 && !head_;
    Slab* target = nullptr;
    for (Slab* slab = head_; slab; slab = slab->next) {
      if (slab == checkpoint.slab) {
        target = slab;
        break;
      }
    }
    if (!target || checkpoint.offset > target->offset || checkpoint.offset > target->capacity) return false;

    Slab* slab = target->next;
    while (slab) {
      Slab* next = slab->next;
      totalCapacity_ -= slab->capacity;
      std::free(slab);
      slab = next;
    }
    target->next = nullptr;
    target->offset = checkpoint.offset;
    current_ = target;
    return true;
  }

  [[nodiscard]] size_t used() const {
    size_t total = 0;
    for (const Slab* slab = head_; slab; slab = slab->next) total += slab->offset;
    return total;
  }
  [[nodiscard]] size_t capacity() const { return totalCapacity_; }
  [[nodiscard]] size_t highWater() const { return highWater_; }
  [[nodiscard]] bool initialized() const { return head_ != nullptr; }

 private:
  struct Slab {
    Slab* next = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
  };

  static constexpr size_t DATA_ALIGNMENT = alignof(std::max_align_t);

  static bool checkedAdd(const size_t left, const size_t right, size_t& result) {
    if (right > std::numeric_limits<size_t>::max() - left) return false;
    result = left + right;
    return true;
  }

  static bool validAlignment(const size_t alignment) {
    return alignment != 0 && alignment <= DATA_ALIGNMENT && (alignment & (alignment - 1)) == 0;
  }

  static constexpr size_t headerBytes() { return (sizeof(Slab) + DATA_ALIGNMENT - 1) & ~(DATA_ALIGNMENT - 1); }

  static uint8_t* data(Slab& slab) { return reinterpret_cast<uint8_t*>(&slab) + headerBytes(); }

  static Slab* allocateSlab(const size_t capacity) {
    size_t allocationSize = 0;
    if (!checkedAdd(headerBytes(), capacity, allocationSize)) return nullptr;
    void* memory = std::malloc(allocationSize);
    if (!memory) return nullptr;
    auto* slab = ::new (memory) Slab;
    slab->capacity = capacity;
    return slab;
  }

  static void* tryAllocate(Slab& slab, const size_t size, const size_t alignment) {
    const size_t padding = (alignment - (slab.offset & (alignment - 1))) & (alignment - 1);
    size_t alignedOffset = 0;
    size_t endOffset = 0;
    if (!checkedAdd(slab.offset, padding, alignedOffset) || !checkedAdd(alignedOffset, size, endOffset) ||
        endOffset > slab.capacity) {
      return nullptr;
    }
    slab.offset = endOffset;
    return data(slab) + alignedOffset;
  }

  void updateHighWater() {
    const size_t now = used();
    if (now > highWater_) highWater_ = now;
  }

  Slab* head_ = nullptr;
  Slab* current_ = nullptr;
  size_t slabSize_ = 0;
  size_t maxBytes_ = 0;
  size_t totalCapacity_ = 0;
  size_t highWater_ = 0;
};

template <typename T, typename... Args>
T* arenaNew(Arena& arena, Args&&... args) {
  void* memory = arena.alloc(sizeof(T), alignof(T));
  return memory ? ::new (memory) T(std::forward<Args>(args)...) : nullptr;
}

template <typename T>
T* arenaNewArray(Arena& arena, const size_t count) {
  static_assert(std::is_trivially_destructible_v<T>, "Arena arrays must be trivially destructible");
  if (count == 0 || count > std::numeric_limits<size_t>::max() / sizeof(T)) return nullptr;
  void* memory = arena.alloc(sizeof(T) * count, alignof(T));
  return memory ? ::new (memory) T[count]() : nullptr;
}
