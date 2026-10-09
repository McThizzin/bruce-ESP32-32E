# ESP32-32E no-PSRAM Wi-Fi stability — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop the ESP32-32E (no-PSRAM) board from rebooting when Wi-Fi capture runs out of internal heap, by guarding allocation failures and shrinking capture caches on no-PSRAM builds.

**Architecture:** Two layers. (1) **Safety:** make the promiscuous callbacks tolerate allocation failure (NULL `String::c_str()`, `std::bad_alloc`) instead of panicking, and add a shared low-heap tripwire. (2) **Pressure:** cap the sniffer/karma containers and reduce their sizes only when `BOARD_HAS_PSRAM` is not defined. PSRAM boards keep their existing sizes and behaviour.

**Tech Stack:** C++ / Arduino-ESP32 3.3.9 (xtensa), FreeRTOS ring buffers/queues, `std::map`/`std::set`/`std::vector`, `esp_heap_caps`.

**Spec:** `docs/superpowers/specs/2026-10-08-esp32-32e-nopsram-stability.md`

## Global Constraints

- All size reductions MUST be inside `#ifndef BOARD_HAS_PSRAM` (or an equivalent `#if defined(BOARD_HAS_PSRAM)` "else") so PSRAM boards are byte-for-byte unchanged.
- Never modify `custom_4Mb_full.csv` (shared partition table).
- Keep changes surgical: no refactors, no renaming, no reformatting beyond the edited lines.
- Every callback (`sniffer`, `probe_sniffer`, `clientSnifferCallback`, `pwnSnifferCallback`) MUST NOT let `std::bad_alloc` propagate.
- Build target: `arduino-esp32 3.3.9`, PlatformIO `espressif32@55.3.39`.
- There is no host unit-test suite for this firmware; **verification = compile + on-device check**. Compile all four envs in every build step.

## Review Focus

1. A management frame arriving when internal heap is nearly exhausted must be **dropped**, never trigger `xRingbufferSend`'s NULL assert.
2. A failed `String` allocation in `probe_sniffer` (`cacheKey`) must not reach `addMACToCache` as a NULL `c_str()`.
3. Dense RF (hundreds of APs/beacons) must not grow `eapol4WayBuffer`/`perApHandshakeTracker`/`beacon*Cache`/`SavedHS` unbounded.
4. PSRAM boards (`m5stack-sticks3`, `m5stack-cardputer`, …) must keep original sizes and behaviour.
5. No `std::bad_alloc` may escape a promiscuous RX callback into the Wi-Fi task.

---

### Task 1: Shared low-memory helper

**Files:**
- Create: `src/modules/wifi/wifi_memory.h`

**Interfaces:**
- Produces: `bool wifiLowMemory()`; `void capContainer(Container&, size_t)`; `constexpr size_t WIFI_LOW_HEAP_BYTES`.

- [ ] **Step 1: Create the header**

```cpp
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
```

- [ ] **Step 2: Verify it compiles**

Run: `pio run -e ESP32-32E-7789`
Expected: `SUCCESS` (header is unused so far, but must parse).

- [ ] **Step 3: Commit**

```bash
git add src/modules/wifi/wifi_memory.h
git commit -m "feat(wifi): add low-memory guard + container cap helpers"
```

---

### Task 2: Guard and size the Karma probe path

**Files:**
- Modify: `src/modules/wifi/karma_attack.cpp` (`addMACToCache` :877, `probe_sniffer` :2265, constants :107-108)
- Test/verify: build `ESP32-32E-7789` + `ESP32-32E`

**Interfaces:**
- Consumes: `wifiLowMemory()`, `capContainer()` from Task 1.

- [ ] **Step 1: Include the helper**

At the top of `karma_attack.cpp` (with the other `modules/wifi/...` includes), add:
```cpp
#include "modules/wifi/wifi_memory.h"
```
(Use the same include style as the file's existing `modules/wifi/...` includes.)

- [ ] **Step 2: Make the buffer sizes no-PSRAM aware**

Replace line 107:
```cpp
#define MAX_PROBE_BUFFER 200
```
with:
```cpp
#if defined(BOARD_HAS_PSRAM)
#define MAX_PROBE_BUFFER 200
#else
#define MAX_PROBE_BUFFER 60
#endif
```
Replace line 108:
```cpp
#define MAC_CACHE_SIZE 100
```
with:
```cpp
#if defined(BOARD_HAS_PSRAM)
#define MAC_CACHE_SIZE 100
#else
#define MAC_CACHE_SIZE 48
#endif
```

- [ ] **Step 3: Guard the assert site in `addMACToCache`**

Make the send use a locally captured, validated pointer so a failed `String` can never send NULL:
```cpp
void addMACToCache(const String &mac) {
    if (!macRingBuffer) return;
    const char *cstr = mac.c_str();
    if (cstr == nullptr || mac.length() == 0) return; // failed String allocation, or empty
    if (xRingbufferGetCurFreeSize(macRingBuffer) < mac.length() + 1) {
        size_t itemSize;
        char *oldItem = (char *)xRingbufferReceive(macRingBuffer, &itemSize, 0);
        if (oldItem) vRingbufferReturnItem(macRingBuffer, oldItem);
    }
    xRingbufferSend(macRingBuffer, cstr, mac.length() + 1, pdMS_TO_TICKS(100));
}
```

- [ ] **Step 4: Drop work when memory is low, and contain `bad_alloc`, in `probe_sniffer`**

Immediately after the `if (!storageAvailable) return;` guard (:2268) add:
```cpp
    if (wifiLowMemory()) return; // skip capture rather than risk a failed allocation
```
Then wrap the body from `String clientMAC = extractMAC(pkt);` (:2276) to the end of the
function's packet handling in a `try { … } catch (const std::bad_alloc &) { return; }`,
and cap `handshakeBuffer` after the push at :2291:
```cpp
    capContainer(handshakeBuffer, 20); // existing behaviour is already capped at 20
```
(Keep `erase(begin())` behaviour: replace the existing `if (handshakeBuffer.size() > 20) handshakeBuffer.erase(...)` with `capContainer(handshakeBuffer, 20);` for one code path.)

- [ ] **Step 5: Build both envs**

Run: `pio run -e ESP32-32E-7789 -e ESP32-32E`
Expected: both `SUCCESS`.

- [ ] **Step 6: Commit**

```bash
git add src/modules/wifi/karma_attack.cpp
git commit -m "fix(wifi/karma): guard addMACToCache NULL and cap probe memory on no-PSRAM"
```

---

### Task 3: Guard and cap the Wi-Fi sniffer

**Files:**
- Modify: `src/modules/wifi/sniffer.cpp` (`sniffer` :947, `analyzeFrame` :615, caches :83-135, insert sites :341-342, :503, :533-540, :556-563, :585, :606, :654)
- Test/verify: build `ESP32-32E-7789` + `ESP32-32E` + `m5stack-sticks3`

**Interfaces:**
- Consumes: `wifiLowMemory()`, `capContainer()`.

- [ ] **Step 1: Include the helper**

Add near the other includes in `sniffer.cpp`:
```cpp
#include "modules/wifi/wifi_memory.h"
```

- [ ] **Step 2: Make the cache sizes no-PSRAM aware**

Replace `MAX_BEACON_CACHE` (:124) and add the entry caps:
```cpp
#if defined(BOARD_HAS_PSRAM)
constexpr size_t MAX_BEACON_CACHE = 64;
constexpr size_t EAPOL_MAP_MAX = 64;
constexpr size_t PERAP_HS_MAP_MAX = 128;
constexpr size_t HS_SET_MAX = 128;
constexpr size_t BEACON_MAP_MAX = 128;
#else
constexpr size_t MAX_BEACON_CACHE = 24;
constexpr size_t EAPOL_MAP_MAX = 16;
constexpr size_t PERAP_HS_MAP_MAX = 32;
constexpr size_t HS_SET_MAX = 32;
constexpr size_t BEACON_MAP_MAX = 32;
#endif
```
Delete the old `constexpr size_t MAX_BEACON_CACHE = 64;` line so there is exactly one definition.

- [ ] **Step 3: Make `sniffer()` OOM-safe**

After the `isLittleFS`/storage guard (:950-955) add:
```cpp
    if (wifiLowMemory()) return; // drop frames while heap is critically low
```
Wrap the `analyzeFrame` call (:962) so a container-node OOM drops the frame:
```cpp
    FrameInfo frameInfo;
    try {
        frameInfo = analyzeFrame(pkt);
    } catch (const std::bad_alloc &) {
        return;
    }
```

- [ ] **Step 4: Cap every growing container at its insert site**

Add a `capContainer(...)` call immediately after each insertion:
- after `perApHandshakeTracker[apKey]` (:341) → `capContainer(perApHandshakeTracker, PERAP_HS_MAP_MAX);`
- after `eapol4WayBuffer[apKey]` (:342) → `capContainer(eapol4WayBuffer, EAPOL_MAP_MAX);`
- after `registeredBeacons.insert(beacon)` (:585) → `capContainer(registeredBeacons, BEACON_MAP_MAX);`
- after `beaconRawCache[apKey]` (:594) → `capContainer(beaconRawCache, MAX_BEACON_CACHE);`
- after `beaconSsidCache[info.apKey] = ssid;` (:606) → `capContainer(beaconSsidCache, BEACON_MAP_MAX);`
- after `beaconLastSeen[info.apKey] = (uint32_t)millis();` (:654) → `capContainer(beaconLastSeen, BEACON_MAP_MAX);`
- after `handshakeReadyBssids.insert(key);` (:503) → `capContainer(handshakeReadyBssids, HS_SET_MAX);`
- after each `SavedHS.insert(path);` (:533, :537, :540) → `capContainer(SavedHS, HS_SET_MAX);`
- after each `handshakeBeaconLogged.insert(key);` (:556, :560, :563) → `capContainer(handshakeBeaconLogged, HS_SET_MAX);`

- [ ] **Step 5: Build three envs incl. a PSRAM board**

Run: `pio run -e ESP32-32E-7789 -e ESP32-32E -e m5stack-sticks3`
Expected: all `SUCCESS` (proves both `#if` branches compile).

- [ ] **Step 6: Commit**

```bash
git add src/modules/wifi/sniffer.cpp
git commit -m "fix(wifi/sniffer): tolerate bad_alloc, cap caches, shrink no-PSRAM limits"
```

---

### Task 4: Guard the other promiscuous callbacks

**Files:**
- Modify: `src/modules/wifi/deauther.cpp` (`clientSnifferCallback` :1102, `detectedClients` :56/:1075)
- Modify: `src/modules/pwnagotchi/pwngrid.cpp` (`pwnSnifferCallback` :319, `registeredBeacons.insert` :255)

**Interfaces:**
- Consumes: `wifiLowMemory()`, `capContainer()`.

- [ ] **Step 1: Deauther client callback**

Include `#include "modules/wifi/wifi_memory.h";` add `if (wifiLowMemory()) return;` at the
top of `clientSnifferCallback`, and wrap the `detectedClients.push_back(client);` (:1075)
block in `try { … } catch (const std::bad_alloc &) { return; }`. After the push add:
```cpp
capContainer(detectedClients, 64);
```
(There is no existing cap; 64 is well above the menu's selection needs.)

- [ ] **Step 2: Pwnagotchi callback**

Include `#include "modules/wifi/wifi_memory.h"`; add the low-memory guard to
`pwnSnifferCallback`; wrap `registeredBeacons.insert(Beacon);` (:255) in a
`try/catch (const std::bad_alloc &)`, and cap after insertion:
```cpp
capContainer(registeredBeacons, 64);
```

- [ ] **Step 3: Build**

Run: `pio run -e ESP32-32E-7789 -e ESP32-32E`
Expected: both `SUCCESS`.

- [ ] **Step 4: Commit**

```bash
git add src/modules/wifi/deauther.cpp src/modules/pwnagotchi/pwngrid.cpp
git commit -m "fix(wifi): guard deauther/pwnagotchi sniffer callbacks against OOM"
```

---

### Task 5: Build matrix + on-device verification + docs

**Files:**
- Modify: `docs/esp32-32e-verification.md` (append a section)

- [ ] **Step 1: Full build matrix**

Run: `pio run -e ESP32-32E-7789 -e ESP32-32E -e ESP32-32E-INV -e m5stack-sticks3`
Expected: all four `SUCCESS`; record app size (must stay under 0x3C0000 = 3,932,160 B).

- [ ] **Step 2: Produce the flashable image**

Run: `pio run -e ESP32-32E-7789` then confirm `Bruce-ESP32-32E-7789.bin` exists at repo root.

- [ ] **Step 3: On-device verification (user)**

Flash `Bruce-ESP32-32E-7789.bin` and exercise:
1. **Wi-Fi → Sniffer** for ~2 minutes on all channels; expect no reboot, normal capture.
2. **Karma** (probe/attack) for ~2 minutes; expect no reboot.
3. **Deauther** client scan; expect no reboot.
Expected: no `xRingbufferSend` assert and no `abort()` backtrace; under low memory the
features may drop packets but the board keeps running.

- [ ] **Step 4: Record results**

Append to `docs/esp32-32e-verification.md` a dated subsection noting: commit SHA, env
built, the features exercised, and whether any reboot occurred (plus serial evidence).

- [ ] **Step 5: Commit**

```bash
git add docs/esp32-32e-verification.md
git commit -m "docs(esp32-32e): record no-PSRAM Wi-Fi stability verification"
```

## Self-Review

- **Spec coverage:** guards (Tasks 2-4), low-memory tripwire (Task 1, used in 2-4),
  conditional caps (Tasks 2-3, values from spec table), verification (Task 5). PSRAM
  non-change enforced by Global Constraints + `m5stack-sticks3` build.
- **Type consistency:** `wifiLowMemory()`, `capContainer()`, and the cap constants are
  defined once in Task 1/2/3 and referenced by the same names in later tasks.
- **Review Focus:** each of the five items maps to a guard/cap plus its build or
  on-device check in the owning task.
- **Known unknown:** exact `std::bad_alloc` line for crash 2 was decoded against a
  mismatched ELF; the `try/catch` in `sniffer()` covers the whole `analyzeFrame` path,
  so it is robust to the precise line. A fresh crash on the current build (Task 5) will
  confirm.
