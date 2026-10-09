# ESP32-32E on-device verification

Date: 2026-10-06
Branch: `esp32-32e-port`
Image: `Bruce-ESP32-32E.bin` (merged, 3,643,168 B; app `firmware.bin` 3,577,632 B in a 0x3C0000 = 3,932,160 B partition → 9.0% free)
Hardware: ESP32-32E (Elegoo CYD-style), 4 MB flash, ILI9341 240×320, resistive XPT2046 touch
Base: current upstream `BruceDevices/firmware` (`a59213f3`) + ported `boards/ESP32-32E/`

## Results

| # | Check | Result |
|---|-------|--------|
| 1 | Boot with **no SD card** → SD-failure logs, LittleFS fallback, main menu, no hang | **PASS** |
| 2 | First boot: screen colors correct (not inverted), backlight on, XPT2046 touch responds without calibration hang | **PASS** |
| 3 | SD card inserted, reboot: SD mounts, Files menu works, web-UI upload writes to SD | **PASS** |
| 4 | Boot + reset buttons | **PASS** |
| 5 | Audio / IR / GPS / battery | **NOT TESTED** — not present on this unit |

## Notes
- No partition change was needed (`custom_4Mb_full.csv` app = 0x3C0000; ~9% free), so the planned `custom_4Mb_maxapp.csv` contingency was not created.
- Board files adapted to Arduino-ESP32 core 3.x: LEDC keyed by pin, `bruceConfigPins.rotation`; battery macro corrected to `-DANALOG_BAT_PIN=34` (old `-DBAT_PIN` is dead upstream).
- IR defaults: `-DTXLED=4` (upstream would fall back to `GROVE_SDA=27`, colliding with CC1101 SS) and `-DRXLED=35` (input-only, receiver-capable); the `*_TX_PINS` option lists now exclude input-only GPIO35. IR remains **unverified** against hardware.
- Color inversion is selected by build env: `ESP32-32E`/`-INV` (ILI9341 panel) and `ESP32-32E-7789` (ST7789 panel, `-DTFT_INVERSION_ON`).
- Panel confirmed on hardware (2026-10-08): this unit is **ST7789** and needs `-DTFT_INVERSION_ON` plus `-DTFT_RGB_ORDER=TFT_BGR`; the `ESP32-32E-7789` build gives a correct, upright, correctly-coloured image.

## no-PSRAM Wi-Fi stability — guards + memory caps

Date: 2026-10-08
Branch: `fix/esp32-32e-nopsram`
Stability commits: `7d7ba244` (helpers), `a199cf8d` (karma), `fb2a976d` (sniffer), `c415f28f` (deauther/pwngotchi)
Change: `src/modules/wifi/wifi_memory.h` low-heap tripwire (`wifiLowMemory()`) + `capContainer()`;
NULL-`c_str()`/`std::bad_alloc` guards in `probe_sniffer`/`addMACToCache`, `sniffer`/`analyzeFrame`,
`clientSnifferCallback`, `pwnSnifferCallback`; hard caps on the sniffer/karma containers and
no-PSRAM size reductions (`MAX_PROBE_BUFFER` 200→60, `MAC_CACHE_SIZE` 100→48,
`MAX_BEACON_CACHE` 64→24, plus the caps in the spec table). PSRAM boards keep the original
size constants.

### Build matrix (compiled, 2026-10-08)

| Env | Result | App size (`firmware.bin`) |
|---|---|---|
| `ESP32-32E-7789` | **SUCCESS** | 3,581,472 B |
| `ESP32-32E` | **SUCCESS** | 3,581,392 B |
| `ESP32-32E-INV` | **SUCCESS** | 3,581,504 B |

All app sizes < `0x3C0000` = 3,932,160 B slot (≈8.9% free). Flashable image produced:
`Bruce-ESP32-32E-7789.bin` (merged, 3,647,008 B, SHA256 `6569e201db9dee14be8fafeded978570e0a9846f0584c979dd0032cf4433e18a`).

PSRAM branch: `m5stack-sticks3` — and every other PSRAM env in-tree (`elecrow-advance-35-s3`,
`esp32-s3-devkitc-1-psram`, `lilygo-t-display-S3-pro`, `esp32-c5-tft`, `nm-cyd-c5`) — currently
fails to compile for an unrelated reason: `-DFP=1` collides with FastLED 3.10.6's
`template <typename FP>` in `fl/stl/json.h`. `ESP32-32E.ini:124` already comments out `-DFP=1`
for exactly this collision. To prove the `BOARD_HAS_PSRAM` branch of the changed files, `sniffer.cpp`
and `karma_attack.cpp` were compiled against the `ESP32-32E` flags with `-DBOARD_HAS_PSRAM`
added (`-fsyntax-only`); both exited 0.

### On-device verification — PENDING (user)

Not yet run: requires an ESP32-32E (ESP32-WROOM-32E, no PSRAM) unit. Flash
`Bruce-ESP32-32E-7789.bin` and exercise:
1. **Wi-Fi → Sniffer** for ~2 minutes, all channels — expect no reboot, normal capture.
2. **Karma** (probe/attack) for ~2 minutes — expect no reboot.
3. **Deauther** client scan — expect no reboot.

Success = no `xRingbufferSend` assert and no `abort()` backtrace; under low internal heap the
features may drop packets but the board keeps running. Record the result (and serial evidence) here.
