# ESP32-32E no-PSRAM stability — design

**Status:** approved in principle (approach: guards + reduce memory)
**Date:** 2026-10-08
**Branch:** `fix/esp32-32e-nopsram`

## Problem

On the ESP32-32E (ESP32-WROOM-32E, 4 MB flash, **no PSRAM**), heavy Wi-Fi features
reboot the board. Two reproduced crashes:

1. `assert failed: xRingbufferSend ringbuf.c:1050 (pvItem != ((void *)0) || xItemSize == 0)`
   `Backtrace: … xRingbufferSend ← probe_sniffer ← ppProcessRxPktHdr ← ppTask`
2. `abort() was called at PC 0x402e9deb on core 0`
   `Backtrace: … std::terminate ← __cxa_allocate_exception ← operator new ← sniffer ← ppProcessRxPktHdr ← ppTask`

(The IDF frames in the backtraces decode exactly against the precompiled SDK libs;
the app frames were decoded against the closest available ELF because the crashing
build's `.elf` — SHA256 `d1569e954…` — has been overwritten. The source functions
are upstream and unchanged by the port, so the mechanism below holds.)

## Root cause

Both are **internal-heap exhaustion with unchecked allocation failure**. PSRAM is
already off in this build and the existing PSRAM call sites are guarded, so the
failing allocations are ordinary `String`/C++ allocations on the ~320 KB internal heap.

1. `src/modules/wifi/karma_attack.cpp:877 addMACToCache()`:
   `xRingbufferSend(macRingBuffer, mac.c_str(), mac.length() + 1, …)`.
   `length()+1` is always ≥ 1, so the kernel assert fires **iff `mac.c_str()` is NULL**.
   Framework source (`WString.cpp:158,185,235`) shows that on a failed allocation
   `String::copy()` calls `invalidate()` → `buffer = NULL`, and `c_str()` returns
   `buffer` (`WString.h:258`). So an OOM-invalidated `cacheKey` (built at
   `karma_attack.cpp:2304`) passes NULL with a nonzero size.

2. `src/modules/wifi/sniffer.cpp:947 sniffer()` → `:615 analyzeFrame()`:
   `registerBeacon()` does `registeredBeacons.insert(...)` (`:585`) and
   `beaconLastSeen[apKey] = …` (`:654`). `std::set`/`std::map` allocate nodes with
   `operator new`; on OOM it throws `std::bad_alloc`, which is uncaught →
   `std::terminate` → `abort()`.

The same unguarded-container-insert pattern exists in:
- `src/modules/wifi/deauther.cpp:1075` `detectedClients.push_back(client)` (client callback)
- `src/modules/pwnagotchi/pwngrid.cpp:255` `registeredBeacons.insert(Beacon)` (Pwnagotchi callback)

## Goals

- No reboot from allocation failure in the Wi-Fi capture paths; features **degrade
  (drop work)** instead of crashing.
- Lower steady-state heap use of sniffer/karma on no-PSRAM boards.
- Leave PSRAM boards' behaviour and sizes unchanged.

## Non-goals

- Rewriting the sniffer/karma features.
- Enabling/flash core dumps (separate diagnostics task).
- Changing display, audio, BLE, or SD code.

## Approach

1. **Allocation-failure guards** at the fault sites (NULL `c_str`, `try/catch
   std::bad_alloc` around container inserts in callbacks).
2. **Low-memory tripwire**: a shared predicate that callers use to skip
   non-essential capture when internal heap is critically low.
3. **Conditional size caps** for no-PSRAM builds via `#ifndef BOARD_HAS_PSRAM`,
   plus hard caps on the unbounded sniffer/karma containers (evict oldest).

## Decisions (exact values)

| Constant / cap | PSRAM boards | no-PSRAM (`!BOARD_HAS_PSRAM`) |
|---|---|---|
| karma `MAX_PROBE_BUFFER` | 200 | 60 |
| karma `MAC_CACHE_SIZE` | 100 | 48 |
| sniffer `MAX_BEACON_CACHE` | 64 | 24 |
| `eapol4WayBuffer` max entries | 64 | 16 |
| `perApHandshakeTracker` max entries | 128 | 32 |
| `SavedHS` / `handshakeReadyBssids` / `handshakeBeaconLogged` max | 128 | 32 |
| `beaconSsidCache` / `beaconLastSeen` hard cap | 128 | 32 |
| low-heap threshold (`WIFI_LOW_HEAP_BYTES`) | 8 KiB | 24 KiB |

Threshold uses `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` (the size of
the largest single internal allocation still possible), not total free, because the
failures are large contiguous allocations.

## Risks / trade-offs

- Trimmed packet buffers mean less complete captures on dense/no-PSRAM devices — an
  accepted trade for not rebooting.
- Caps may evict handshake/beacon state under very dense RF; the menu already treats
  these as best-effort caches.
- `#ifndef BOARD_HAS_PSRAM` relies on boards defining `BOARD_HAS_PSRAM` (they do for
  PSRAM parts; ESP32-32E correctly does not).

## Verification

- Build (compile) all: `ESP32-32E-7789` (default), `ESP32-32E`, `ESP32-32E-INV`, and a
  PSRAM env (`m5stack-sticks3`) to prove the size branches compile both ways.
- On hardware (user): run **Karma → probe/attack** and **Wi-Fi → Sniffer**; confirm no
  reboot and that low memory causes dropped packets, not a panic.
- Confirm PSRAM envs are unchanged (only the `#ifndef` branches differ).
