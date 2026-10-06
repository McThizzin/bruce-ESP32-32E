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
- Color inversion is now selected by build env: `ESP32-32E` (OFF) vs `ESP32-32E-INV` (`-DTFT_INVERSION_ON`).