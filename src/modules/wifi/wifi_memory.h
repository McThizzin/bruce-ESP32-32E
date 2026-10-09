#pragma once
#include <Arduino.h>
#include <cstddef>
#include <esp_heap_caps.h>

// These failures are contiguous-allocation failures, so the largest single
// internal-DRAM block is the signal (not total free bytes).
#if defined(BOARD_HAS_PSRAM)
static constexpr size_t WIFI_LOW_HEAP_BYTES = 8 * 1024;
#else
static constexpr size_t WIFI_LOW_HEAP_BYTES = 24 * 1024;
#endif

static inline bool wifiLowMemory() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < WIFI_LOW_HEAP_BYTES;
}

// Erase the lowest-key entries until the container is within cap.
template <typename Container> static inline void capContainer(Container &c, size_t cap) {
    while (c.size() > cap) { c.erase(c.begin()); }
}
