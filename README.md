# Heatmiser Matter bridge

Firmware for a Waveshare ESP32-S3-Touch-LCD-7 that acts as the single RS485
master for Heatmiser V3 thermostats (PRT-N and PRT/HW-N) and shows them on its
touchscreen; a later milestone presents them as Matter thermostats.

The design is in the project's *Heatmiser Matter Bridge System Design* doc; the
wire protocol is Heatmiser's V3.9 RS485 protocol document.

## Status: milestone 3, the touchscreen (on the bench)

| Part | State |
| --- | --- |
| `hm_proto`: V3 frames, CRC, DCB decode, unique address map, write encoders and read-back checks (pure C) | Done, host tested |
| `hm_sched`: write queue first, round-robin full-DCB poll, read-back confirmation, retries, offline back-off, scan (pure C) | Done, host tested against simulated stats |
| `hm_bus`: UART1 at 4800 8N1, TX GPIO16 / RX GPIO15, one transaction at a time | Done, bench proven (milestone 1) |
| `hm_poller`: the bus task running `hm_sched` on the board | Done, bench proven (milestone 2) |
| `zone_store`: saved zone list and names in NVS, a live view of each stat, pending writes with failure notices | Done; the view and the saved format are host tested |
| `board`: Waveshare 7" RGB panel with bounce buffers, GT911 touch, CH422G backlight, LVGL 9 via esp_lvgl_port | Built; first run on the board due |
| `ui`: dashboard, zone detail, setup (scan, rename, identify, remove), drift test pattern | Built; desktop preview renders every screen |
| `net`: Wi-Fi station, SNTP clock, timezone | Built |
| Gate: no drift during a 24 h soak with Wi-Fi on | Due; see [bench-milestone3.md](docs/bench-milestone3.md) |

Milestone 1 (bus bring-up) passed its gate on 17 Nov 2026 and milestone 2
(poller and writes) on 18 Nov 2026 on 11 of 12 stats; see
[bench-bringup.md](docs/bench-bringup.md#bench-results-17-nov-2026) and
[bench-milestone2.md](docs/bench-milestone2.md#bench-results-18-nov-2026).

### Protocol notes found on the way

- Over RS485 only these are writable (yellow in the V3.9 DCB tables): frost
  temperature 17, set temperature 18, floor limit 19, on/off 21, key lock 22,
  run mode 23, holiday 24, hold 32, hot water 42, clock 43, comfort levels and
  hot water timers. Temperature format, sensor selection, programme mode and
  calibration are read only, and a write to them gets no reply.
- In 7-day mode the 5/2 comfort blocks stay in the DCB and the Mon-Sun blocks
  follow them (unique address 103 on). PRT: Mon-Sun at index 64; PRTHW: at
  index 97, then its 7-day hot water timers at 181.
- A PRT/HW-N DCB is 97 bytes in 5/2 mode (293 in 7-day).
- Frost protection enable (byte 7) reads 00 when enabled on a real PRT-N,
  the reverse of the document.

## Host tests

Any C compiler and CMake:

```
cmake -S test/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## UI preview

The screens run on the desktop against made-up zones and write a PNG of each
one, so layout changes can be checked without the board:

```
cmake -S tools/ui_preview -B build-preview
cmake --build build-preview
./build-preview/ui_preview <out-dir>
```

It fetches LVGL 9.4.0 from GitHub (or pass `-DLVGL_DIR=` a local checkout).

## Firmware

ESP-IDF 5.4 or later; CI builds with 5.4.1 and 6.1.1. LVGL, esp_lvgl_port and the GT911 driver come from the
ESP component registry on the first build:

```
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

The console is on UART0 at 115200, which is the board's USB-C port marked
**UART**. Bus settings are under `idf.py menuconfig` → *Heatmiser RS485 bus*.

CI runs the host tests, the UI preview (screenshots are kept as a build
artifact), an ESP-IDF build and a check of the PC tools on every pull request.
