# Port ESP32-32E Support to Current Bruce Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a new repo forked from the current official `BruceDevices/firmware` and re-implement ESP32-32E (Waveshare 2.8" ILI9341 / Elegoo CYD-style) build support on top of it, replacing the ~11-month-old fork `g1l34t20n/bruceesp32e`.

**Architecture:** Do not merge or rebase the old fork's 30 commits (881 upstream commits would conflict in exactly the files we touched). Instead start a branch from current upstream and port the small board-support footprint: `boards/ESP32-32E/` (auto-registered via `extra_configs = boards/*/*.ini`), tiny `platformio.ini` edits, optional test sketches and small docs. The `esp32e-docs/` vendor tree is deliberately NOT ported (reversed by user decision — see Task 4). Every old local bug-fix is re-evaluated against current upstream code rather than copied.

**Tech Stack:** PlatformIO (pioarduino platform-espressif32 55.03.39), Arduino framework 3.3.x, TFT_eSPI (ILI9341_2_DRIVER via HSPI), XPT2046 resistive touch (`lib/CYD-touch`, already upstream), FastLED ^3.10.x, sd_files on LittleFS/SD.

**Spec:** This document's §0 (Findings & Decisions) is the spec — it records the analysis, the user's decisions (new repo from `BruceDevices/firmware`, **exclude** the `esp32e-docs/` vendor tree — decision reversed mid-execution, execute in a fresh session), and the exact port footprint.

---

## 0. Findings & Decisions (Spec)

Git facts, verified 2026-10-06:

| Fact | Value |
|---|---|
| Official upstream repo | `https://github.com/BruceDevices/firmware.git`, default branch `main` |
| Upstream HEAD at write time | `a59213f3ec6cd302fa32c2042b2700c9c24d991a` (identical to bmorcelli/Bruce's HEAD) |
| Old fork (source of the port) | sibling repo `bruceesp32e` (this repo), fork point `113ac916`, fork HEAD `60414dde` |
| Fork's unique work | 30 commits on top of `113ac916`; upstream is 881 commits ahead |
| Board-env registration | `extra_configs = boards/*.ini`, `boards/*/*.ini` in `platformio.ini` → **one ini file in `boards/ESP32-32E/` registers all its envs** |

Port footprint (only these, everything else came from upstream):

| Path (old fork `HEAD`) | What it is |
|---|---|
| `boards/ESP32-32E/ESP32-32E.ini` | Encapsulated env: `[ESP32_32E_base]`, `[env:ESP32-32E]`, `[env:ESP32-32E-RESISTIVE]`, `[env:LAUNCHER_ESP32-32E]`, `[env:ESP32-32E-INV]` |
| `boards/ESP32-32E/interface.cpp` | XPT2046 touch init + LEDC brightness control (built via `build_src_filter` include) |
| `boards/ESP32-32E/pins_arduino.h` | Pin map for the Waveshare module (pulled in via `-Iboards/ESP32-32E`) |
| `boards/ESP32-32E/README.md`, `debug_patch.txt` | Board docs |
| `platformio.ini` | 3 small edits (see Task 3) |
| `test/ESP32-32E_minimal_test.ino`, `test/CC1101_hardware_test.ino` | Standalone hardware tests; optional to port |
| `custom_4Mb_maxapp.csv` | Contingency partition (untracked in old fork; only if flash overflows — see Task 6) |
| `esp32e-docs/` | Waveshare vendor tree (demos/PDFs/tools), 436 MB committed / 3,006 files on disk. **User decision (reversed): do NOT commit** — remains in `../bruceesp32e/esp32e-docs` for reference |
| Root docs asked about earlier | `sd_files/` is a stock upstream folder — **do not port** (already upstream) |

**Deliberately NOT ported** (verified against current upstream):
- `src/main.cpp` / `src/core/sd_functions.cpp` edits — logging-only instrumentation; the non-fatal "no SD card" path already exists upstream. Skip unless boot hangs without an SD card (Task 7 re-checks).
- Deletion of `src/core/main_debug.cpp`, `src/core/interface_debug.cpp` — both files are already gone from upstream.
- `custom_4Mb.csv`, `custom_4Mb_full.csv`, `custom_4Mb.csv`, `custom_8Mb.csv` — all exist upstream; **never edit shared partition files** (8+ envs use them).
- `lib/CYD-touch/` — already present upstream.

Old local-fix disposition table (what each of the fork's 30 commits means on the new base):

| Fork commit | Verdict on new base |
|---|---|
| `42e3e154`, `daed5749`, `632f4b96` | PORT — board support + docs (Tasks 2–4) |
| `c7b65aed` Disable HAS_RGB_LED | PORT — keep RGB LED off (IO22 reserved for CC1101 GDO0); keep as comments in new io.ini |
| `f9b3e9c4` FastLED dep, `90112006` | CHECK — upstream already has `fastled/FastLED @^3.10.3` in `[env] lib_deps`; drop the old fork's duplicate pin |
| `340b518d` ESP8266Audio@^1.9.7, `e34a13fe` DAC | PORT as env flags `-DDOUT=26 -DMCLK=-1`; reassess whether the version pin is still needed against current audio code at build time |
| `8a2367a8` ILI9341_2_DRIVER / slower SPI | PORT — display driver flags in new io.ini (`-DILI9341_2_DRIVER=1`, 40/16/2.5 MHz SPIs) |
| `cc946c58` INV env, `f80fba0a` backlight, `80ea5536` LEDC order | PORT — `[env:ESP32-32E-INV]` + backlight/brightness flags; LEDC-order fix is inside `interface.cpp` |
| `d46ebe0c`, `86bcb842`, `b3a0aca0`, `a707afcc`, `729fb656` touch/color fixes | RE-EVALUATE — net effect is in the `.ini` (`-DTFT_INVERSION_OFF`, `-DTOUCH_CS=33`, `-DTOUCH_CONFIG_INT_GPIO_NUM=36`) and `interface.cpp`; compare with upstream's CYD touch handling, then verify on device (Task 7) |
| `edd109ba`, `fbdf19bf` SD non-blocking | SKIP unless Task 7 shows a hang |
| `e399a2f0`, `60016391` removed debug files | SKIP — obsolete upstream |
| `f2d54191`, `8d1c7576`, `60414dde` CC1101/NRF24 remap + docs + diagnostic tool | PORT (keep `CC1101_GDO0_PIN=22`, SS 27 — matches current upstream CYD defaults) + optional test sketch |
| `fda33382`, `eac8900e`, `a8c74c64`, `b492a919`, `c4df072f` diagnostics/guides | OPTIONAL docs + `[env:ESP32-32E-TEST]` |
| `a6c1f1ac` README install instructions | OPTIONAL — port only if desired |

## Global Constraints

- Official upstream ONLY: `https://github.com/BruceDevices/firmware.git`, branch `main`. Never commit to upstream directly.
- Foreign (non-32E) envs must build unchanged; never touch shared files (`platformio.ini` `[env]` base, partition CSVs, `[env_4mb]`, `[env_light]`) except where this plan says so.
- All custom work lives under `boards/ESP32-32E/` and the two test inos; nothing else in `src/` may diverge from upstream.
- Flash ceiling: 4 MB (GigaDevice, probed 2026-10-06). App partition from `custom_4Mb_full.csv` = 3,735,552 B. Firmware may not exceed its app partition; target ≤ 95% usage.
- Do NOT pass `-DFP`, `-DFM`, or `-DFG` in the ESP32-32E env — FastLED `^3.10.x` defines `using FP = fl::s16x16;` and collides (this broke the old fork's build). `include/precompiler_flags.h` supplies the `#ifndef` defaults (1/2/3) automatically. (Upstream CYD base still passes them; ESP32-32E must not.)
- Board ini must inherit `${env.build_flags}` and `${env_4mb.build_flags}` (upstream's 4MB flash-saving block: `-Os`, `-DCORE_DEBUG_LEVEL=0`, JTAG-disable, restricted IR decoder set) and set `lib_ignore = ${env_4mb.lib_ignore}` like `CYD_base` does.
- The board env name is `ESP32-32E`; `default_envs` in `platformio.ini` must list exactly one active env, `ESP32-32E`.
- Partition-table contingency file name: `custom_4Mb_maxapp.csv` (single-purpose, used by no other env) — never edit `custom_4Mb_full.csv`.

## Review Focus

Inputs/conditions the spec implies but no single task's tests pin down; the owning task adds the explicit check:

1. **4 MB flash overflow after port** — current upstream firmware + `custom_4Mb_full.csv` may not fit in 3,735,552 B. → Task 5 step checks build size; Task 6 is the contingency.
2. **FastLED symbol collision** — forgetting the FP/FM/FG constraint fails the build with `using FP = fl::s16x16;` errors. → Task 5 build verbatim.
3. **SD card absent on first boot** — must degrade to LittleFS and reach the main menu, never hang. → Task 7 check 2.
4. **XPT2046 touch init** — must not block startup (old base hung in calibration); device must boot to menu on first power-on. → Task 7 check 3.
5. **Shared partition file integrity** — if overflow occurs and a new partition is needed, a NEW file is created; `custom_4Mb_full.csv` stays bit-identical. → Task 6 step verifies with `git diff --exit-code`.

---

### Task 1: Repo & Branch Setup

**Files:**
- Create: `bruce-ESP32-32E/` (your new fork clone) in the parent directory you set up — already cloned and verified
- Create: branch `esp32-32e-port` in it — already created

**Interfaces:**
- Produces: working git repo whose `main` == upstream `a59213f3` (or newer), ready for the port to be added on a branch.

- [x] **Step 1: Fork current official repo on GitHub**

Done — `McThizzin/bruce-ESP32-32E`.

- [x] **Step 2: Clone the fork as a sibling of the old fork**

```bash
# from the parent dir that contains the old clone (here: ~/Projects/ESP32/bruce)
git clone https://github.com/McThizzin/bruce-ESP32-32E.git
cd bruce-ESP32-32E
```

- [x] **Step 3: Confirm the base is current and clean**

```bash
git log --oneline -1        # a59213f3 fix(headless): don't block boot forever waiting for USB CDC host (#2861)
git status --short          # empty
```

- [ ] **Step 4: Verify the upstream auto-registration and 4MB blocks exist**

```bash
grep -n "extra_configs" platformio.ini                     # expect boards/*/*.ini
grep -n "^\[env_4mb\]" platformio.ini                      # expect present
grep -n "fastled/FastLED" platformio.ini                   # expect @^3.10.3 in [env] lib_deps
ls boards/CYD-2432S028/                                    # reference board present
```

- [x] **Step 5: Create the port branch**

```bash
git checkout -b esp32-32e-port
git push -u origin esp32-32e-port    # do this after Task 8, or now behind upstream's back — either is fine
```

---

### Task 2: Port `boards/ESP32-32E/` Board Definition

**Files:**
- Copy source → new repo: `../bruceesp32e/boards/ESP32-32E/{ESP32-32E.ini,interface.cpp,pins_arduino.h,README.md,debug_patch.txt}` → `boards/ESP32-32E/`

**Interfaces:**
- Produces: `boards/ESP32-32E/ESP32-32E.ini` registering envs `ESP32-32E`, `ESP32-32E-RESISTIVE`, `LAUNCHER_ESP32-32E`, `ESP32-32E-INV` (auto-picked-up by `extra_configs`).
- Consumes: upstream `[env]` (build_flags, lib_deps), `[env_4mb]` (build_flags, lib_ignore), `lib/CYD-touch/` (XPT2046 driver, already upstream).

- [ ] **Step 1: Extract the board files from the old fork**

```bash
# from the new bruce-ESP32-32E clone; the old repo is the sibling dir bruceesp32e
mkdir -p boards/ESP32-32E
git -C ../bruceesp32e archive HEAD boards/ESP32-32E | tar -x -C .
ls boards/ESP32-32E   # expect: ESP32-32E.ini interface.cpp pins_arduino.h README.md debug_patch.txt
```

- [ ] **Step 2: Adapt `ESP32-32E.ini` to the current upstream schema — edit in this order**

Open `boards/ESP32-32E/ESP32-32E.ini` in the new repo. Reference `boards/CYD-2432S028/CYD-2432S028.ini` (current upstream schema) side by side.

a) `[ESP32_32E_base]`
   - Keep `board = esp32dev` only if the new pioarduino platform accepts it at build time (Task 5 will prove it). Alternative upstream-idiomatic option: skip the JSON board and keep `esp32dev` — do not invent a `_boards_json` entry unless `boards/_New-Device-Model`/`boards/README.md` describe a required flow; follow that template only if it documents register checks.
   - Add to `build_flags` right after `${env.build_flags}`: `${env_4mb.build_flags}`
   - Add exactly as `CYD_base` does:
     - `lib_ignore = ${env_4mb.lib_ignore}`
   - **Comment out** the three lines `-DFP=1`, `-DFM=2`, `-DFG=3` (Global Constraint — FastLED `using FP = fl::s16x16;` collision). Leave an inline comment referencing this plan.
   - Keep: `-DESP32_32E=1`, all pin defines, `-DILI9341_2_DRIVER=1`, `-DUSE_HSPI_PORT=1`, `-DSPI_FREQUENCY=40000000` family, `-DTOUCH_CS=33`, `-DTOUCH_CONFIG_INT_GPIO_NUM=36`, `-DTFT_INVERSION_OFF` (in `[env:ESP32-32E]`), `-DDIR_TX_PINS/RX_PINS` (`4`/`35`), CC1101/NRF24/W5500 block (`GDO0/CE=22`, `SS=27`), audio DAC (`-DDOUT=26 -DMCLK=-1`), serial (`1`/`3`), battery (`34`), button (`0`), brightness channel block.
   - `board_build.partitions` stays `custom_4Mb_full.csv` (exists upstream; shared, do not edit).

b) Derived envs `[env:ESP32-32E]`, `-RESISTIVE`, `-INV`, `LAUNCHER_*`: copy unchanged except
   - `LAUNCHER_ESP32-32E`: keep `${env:ESP32-32E.build_flags}` + `-DLITE_VERSION=1`; note upstream now uses `${env_light.build_flags}` and `custom_4Mb.csv` — mirror what `LAUNCHER_CYD-2432S028` does (partitions `custom_4Mb.csv`, `lib_deps = ${env_light.lib_deps}`) if the LAUNCHER variant is kept at all. It is optional; if not ported, delete the `LAUNCHER_` env from the file.

- [ ] **Step 3: Sanity-review the file**

```bash
grep -nE -- "-DFP=|-DFM=|-DFG=" boards/ESP32-32E/ESP32-32E.ini   # must show only commented (;) lines
grep -n "env_4mb" boards/ESP32-32E/ESP32-32E.ini                 # ${env_4mb.build_flags} && lib_ignore present
```

- [ ] **Step 4: Verify envs are discovered (no build yet)**

```bash
pio run -e ESP32-32E -t envdump >/dev/null 2>&1 && echo "env ok" || echo "env NOT found"
```

Expected: `env ok`.

- [ ] **Step 5: Commit**

```bash
git add boards/ESP32-32E
git commit -m "feat: add ESP32-32E board support (Waveshare 2.8in ILI9341/XPT2046)"
```

---

### Task 3: `platformio.ini` Edits

**Files:**
- Modify: `platformio.ini` (`[platformio] default_envs`; `[env] lib_deps` only if a pin proves necessary)

**Interfaces:**
- Consumes: envs registered by Task 2.
- Produces: `pio run`/`-t clean`/`-t upload` defaulting to the ESP32-32E env.

- [ ] **Step 1: Set the default env**

In `[platformio]`, comment every env in `default_envs` and set exactly one active entry: `ESP32-32E` (it is already in the list as a commented candidate; uncomment it).

- [ ] **Step 2: Reconcile lib pins**

- `fastled/FastLED @^3.10.3` already in `[env] lib_deps` — remove the old fork's duplicate; do not add a `@^3.7.0` pin (Global Constraint about FPs is the only FastLED requirement).
- `ESP8266Audio`: check upstream's audio usage during Task 5. Add `earlephilhower/ESP8266Audio@^1.9.7` **only if** the build resolves a version that fails with the current audio code; otherwise keep upstream's entry untouched.

- [ ] **Step 3: (Optional) restore the two diagnostic envs**

If the diagnostic tools are wanted, append `[env:ESP32-32E-TEST]` and `[env:ESP32-32E-CC1101-TEST]` to `platformio.ini` exactly as they exist in `../bruceesp32e/platformio.ini` (commented-out envs listing `build_src_filter = -<*> +<../test/ESP32-32E_minimal_test.ino>` / `+<../test/CC1101_hardware_test.ino>`), and copy both inos from `../bruceesp32e/test/` into `test/`. Keep them commented so they never build by default.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini test/
git commit -m "chore: default to ESP32-32E env; document diagnostic test envs"
```

---

### Task 4: Port ESP32-32E Reference Docs (vendor tree NOT committed)

**Files:**
- Create: ESP32-32E-specific root docs from the old fork — only what is still true for current firmware
- Do NOT create: `esp32e-docs/` — excluded by user decision (436 MB / 3,006 files); it stays in the sibling old repo `../bruceesp32e/esp32e-docs` for reference
- Modify: README — none; upstream README stays untouched

**Interfaces:**
- Produces: small, device-specific reference docs in the new repo; no large binary tree.

- [x] **Step 1: (Superseded) Vendor tree deliberately NOT copied**

The earlier decision to keep `esp32e-docs/` committed was reversed. The tree is 436 MB / 3,006 files and adds no build value. It remains available for reference at `../bruceesp32e/esp32e-docs`. Nothing under `esp32e-docs/` is added to git.

- [x] **Step 2: Port the small user-facing docs**

Copied from `../bruceesp32e`:
`NO_SD_CARD_FIX.md`, `CC1101_WIRING_ESP32-32E.md`, `ESP32-32E_SETUP_GUIDE.md`, `ESP32-32E_TROUBLESHOOTING.md`, `README_ESP32-32E.md`.
These are the fork's own markdown docs, not the vendor tree. Revisit any stale workaround claims before relying on them.

- [x] **Step 3: Confirm nothing unintended is staged**

`git status --short` lists only the five markdown files as new; no `esp32e-docs/` entries.

- [x] **Step 4: Commit**

```bash
git add NO_SD_CARD_FIX.md CC1101_WIRING_ESP32-32E.md ESP32-32E_SETUP_GUIDE.md ESP32-32E_TROUBLESHOOTING.md README_ESP32-32E.md
git commit -m "docs: add ESP32-32E reference docs (vendor tree not committed)"
```

---

### Task 5: Build & Fix the ESP32-32E Env (the port's "test cycle")

**Files:** all of Tasks 1–4 output; edits constrained to `boards/ESP32-32E/` + `platformio.ini` only.

**Interfaces:**
- Consumes: environment registration (Task 2), default env (Task 3), upstream `[env]`/`[env_4mb]`.
- Produces: a compiling `ESP32-32E` build whose `firmware.bin` fits its app partition.

- [ ] **Step 1: Clean build**

```bash
pio run -e ESP32-32E
```

Expected: build SUCCESS. If env not found, re-check Task 2 Step 4.

- [ ] **Step 2: Fix compile errors iteratively — allowed fixes only**

Fix order: (1) `boards/ESP32-32E/*` content (pins, flags) — not `src/`; (2) `platformio.ini` pins per Task 3 Step 2 — not shared blocks; (3) if an error points into upstream `src/` changed since the fork (touch API, audio API, `mykeyboard.cpp` expectations), adapt the *board files* (`interface.cpp`, `pins_arduino.h`, `.ini` flags) to the new API. If a genuine upstream bug blocks the env, report it as an issue to `BruceDevices/firmware` — do not patch `src/` in this repo.

Known landmine to expect: none if Task 2 Step 2a FP comment-out was done; if `using FP = fl::s16x16;` errors recur, re-confirm the three flags are commented.

- [ ] **Step 3: Verify flash usage and version**

```bash
pio run -e ESP32-32E 2>&1 | grep -E "RAM:|Flash:"          # print-memory-usage lines
ls -l .pio/build/ESP32-32E/firmware.bin
```

Expected: app image ≤ the `factory` partition from `custom_4Mb_full.csv`. **Actual: `factory` = 0x3C0000 = 3,932,160 B** (the earlier 3,735,552 B figure was the old fork's CSV; current upstream gives more room). Result 2026-10-06: `firmware.bin` = 3,577,632 B → ~9.0% free, within the ≥5% target. If over → Task 6.

- [ ] **Step 4: Re-verify a foreign env still lists (do not build it)**

```bash
pio run -e CYD-2432S028 -t envdump >/dev/null 2>&1 && echo "cyd env ok"
```

Expected: `cyd env ok` — proves shared config stayed intact.

- [ ] **Step 5: Commit any remaining fixes**

```bash
git add -A boards/ESP32-32E platformio.ini
git commit -m "fix: ESP32-32E build on current upstream (rebuild/triage pass)"
```

---

### Task 6: Flash-Space Contingency — **NOT REQUIRED** (Task 5 fit with ~9% free; kept for reference if a future feature overflows)

**Files:**
- Create: `custom_4Mb_maxapp.csv` if needed
- Modify: `boards/ESP32-32E/ESP32-32E.ini` `board_build.partitions`

**Interfaces:**
- Produces: a working partition table for the 4 MB chip with a maximized app partition; `custom_4Mb_full.csv` untouched.

- [ ] **Step 1: Create the single-owner partition file**

If and only if Task 5 Step 3 reports overflow, create `custom_4Mb_maxapp.csv` with exactly:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
app0,     app,  ota_0,   0x10000, 0x3C0000,
spiffs,   data, spiffs,  0x3D0000,0x30000,
```

(App = 3,932,160 B = 0x3C0000; LittleFS = 192 KB. Size used by 2026-10-06 old-base build: 3,914,640 B.)

- [ ] **Step 2: Point only the ESP32-32E base at it**

In `[ESP32_32E_base]`: `board_build.partitions = custom_4Mb_maxapp.csv`. No other env references this file.

- [ ] **Step 3: Prove the shared file is untouched**

```bash
git diff --exit-code custom_4Mb_full.csv   # must exit 0
```

- [ ] **Step 4: Rebuild end-to-end**

```bash
pio run -e ESP32-32E
```

Expected: SUCCESS, `Flash: used ≤ 3,932,160 B`.

- [ ] **Step 5: Commit**

```bash
git add custom_4Mb_maxapp.csv boards/ESP32-32E/ESP32-32E.ini
git commit -m "fix(esp32-32e): maximize app partition within 4MB (custom_4Mb_maxapp.csv)"
```

---

### Task 7: On-Device Functional Verification (User Executes, Assistant Triage)

**Files:** none — hardware. Run by the user; the assistant triages serial output that is pasted back.

- [ ] **Step 1: Flash**

```bash
pio run -e ESP32-32E -t upload    # /dev/ttyUSB0 @ 115200
```

- [ ] **Step 2: Boot without an SD card**

Power on with no SD card inserted. Expected: serial shows SD failure messages, then LittleFS used, main menu renders. If it hangs → port the `SDCARD_DISABLED` guard from `../bruceesp32e` `sd_functions.cpp` (see Review Focus 3).

- [ ] **Step 3: First-boot menu + touch**

Expected: menu draws with correct colors (no inversion), backlight on, XPT2046 touch responds without a calibration hang. If colors wrong → toggle `-DTFT_INVERSION_OFF` ↔ `-DTFT_INVERSION_ON` in `[env:ESP32-32E]`. If touch hangs/absent → compare `interface.cpp` touch init against upstream CYD touch handling (`boards/CYD-2432W328R-or-S024R` uses `-DUSE_TFT_eSPI_TOUCH`; the fork used CYD28_TouchscreenR directly).

- [ ] **Step 4: SD card present**

Insert card, reboot. Expected: SD mounted, Files menu works, web-UI upload (WiFi → Files → SD) writes files. This also validates the `sd_files` guidance given earlier.

- [ ] **Step 5: Peripherals smoke test**

Audio (DAC GPIO26), IR TX/RX (GPIO4/35), CC1101/NRF24/W5500 (SPI, GDO0/CE=22, SS=27), GPS serial (GPIO1/3), battery read (GPIO34), Boot button (GPIO0). Report each pass/fail.

- [ ] **Step 6: Record results as a commit note**

```bash
git commit -m "docs(esp32-32e): on-device verification results" --allow-empty
```

(Attach the results to the body before pushing.)

---

### Task 8: Finalize and Push

- [ ] **Step 1: Confirm the repo only diverges where intended**

```bash
git log --oneline main..esp32-32e-port          # the port commits only
git diff main..esp32-32e-port --stat            # boards/ESP32-32E + platformio.ini + csv + docs
```

- [ ] **Step 2: Open a PR (or fast-forward main) to the new fork**

```bash
git push origin esp32-32e-port
```

Then merge via PR into your fork's `main` (keep upstream history intact; your fork `main` may stay behind — the port branch is the deliverable).

- [ ] **Step 3: Record handoff notes in the PR body**

Include: build size, on-device results from Task 7, and the fact that the old fork `g1l34t20n/bruceesp32e` is superseded.

---

## Self-Review Notes (run at plan time, kept for executor awareness)

- **Spec coverage:** §0 decisions → Tasks 1–8 (new repo ✓, exclude `esp32e-docs/` ✓ Task 4, fresh session ✓ this file is the handoff).
- **Step scan:** every step is one checkable action; values pinned (flashes, pins, file names, partition layout). No free-form "fix whatever" steps.
- **Type/name consistency:** env `ESP32-32E` everywhere; partition file `custom_4Mb_maxapp.csv`; branch `esp32-32e-port`; official repo `BruceDevices/firmware` used consistently.
- **Review Focus:** five failure modes each own their check (Task 5 size check, Task 5 landmine note, Task 7 checks 2–3, Task 6 step 3).
- **Proportion:** this is an ops/port plan (files exist to copy, not write), so prose-heavy steps are deliberate — every instruction is a decision the executor cannot make alone (which flags, which files, which fuse).

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-10-06-port-esp32-32e-to-current-bruce.md`.

Do not start in this repo — the plan assumes a fresh fork clone in a new session:
1. Fork `BruceDevices/firmware`, clone it into a parent dir that also contains this old `bruceesp32e` clone.
2. Start a new session in the new clone.
3. In that session, implement task-by-task with **subagent-driven-development** (recommended — the port's steps depend on each other's outputs and a mistake in Task 2/3 is wasted effort) or **executing-plans**.

**For this plan I recommend subagent-driven development, because each task's output (board files, ini edits, build result) feeds the next and a shipped mistake would mean rebuilding the whole branch. Does the plan capture what you want?**