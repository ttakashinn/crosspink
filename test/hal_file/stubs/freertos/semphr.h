#pragma once

#include <cstdint>

using SemaphoreHandle_t = void*;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;

inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() {
  static uint8_t mutex;
  return &mutex;
}

inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
