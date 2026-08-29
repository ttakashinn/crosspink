#pragma once

#include <Arena.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>

template <typename T>
class ArenaVector {
  static_assert(std::is_trivially_copyable_v<T>, "ArenaVector requires trivially copyable values");

 public:
  explicit ArenaVector(Arena& arena) : arena_(arena) {}

  bool reserve(const size_t requested) {
    if (requested <= capacity_) return true;
    if (requested > std::numeric_limits<size_t>::max() / sizeof(T)) return false;
    auto* next = static_cast<T*>(arena_.alloc(sizeof(T) * requested, alignof(T)));
    if (!next) return false;
    if (data_ && size_) std::memcpy(next, data_, sizeof(T) * size_);
    data_ = next;
    capacity_ = requested;
    return true;
  }

  bool resize(const size_t requested) {
    if (!reserve(requested)) return false;
    if (requested > size_) {
      std::memset(data_ + size_, 0, sizeof(T) * (requested - size_));
    }
    size_ = requested;
    return true;
  }

  bool push_back(const T& value) {
    if (size_ == capacity_) {
      if (capacity_ > std::numeric_limits<size_t>::max() / 2) return false;
      if (!reserve(capacity_ == 0 ? 8 : capacity_ * 2)) return false;
    }
    data_[size_++] = value;
    return true;
  }

  bool insert(const size_t index, const T& value) {
    if (index > size_) return false;
    if (size_ == capacity_) {
      if (capacity_ > std::numeric_limits<size_t>::max() / 2) return false;
      if (!reserve(capacity_ == 0 ? 8 : capacity_ * 2)) return false;
    }
    if (index < size_) {
      std::memmove(data_ + index + 1, data_ + index, sizeof(T) * (size_ - index));
    }
    data_[index] = value;
    ++size_;
    return true;
  }

  void pop_back() {
    if (size_) --size_;
  }
  void clear() { size_ = 0; }
  void resetStorage() {
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
  }

  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] size_t size() const { return size_; }
  [[nodiscard]] size_t capacity() const { return capacity_; }
  T* data() { return data_; }
  const T* data() const { return data_; }
  T& back() { return data_[size_ - 1]; }
  const T& back() const { return data_[size_ - 1]; }
  T* begin() { return data_; }
  T* end() { return data_ ? data_ + size_ : nullptr; }
  const T* begin() const { return data_; }
  const T* end() const { return data_ ? data_ + size_ : nullptr; }
  T& operator[](const size_t index) { return data_[index]; }
  const T& operator[](const size_t index) const { return data_[index]; }

 private:
  Arena& arena_;
  T* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
};
