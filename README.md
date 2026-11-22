# Heatmiser Matter bridge

Firmware for a Waveshare ESP32-S3-Touch-LCD-7 that acts as the single RS485
master for Heatmiser V3 thermostats (PRT-N and PRT/HW-N) and shows them on its
touchscreen and to Matter controllers as thermostats.

The design is in the project's *Heatmiser Matter Bridge System Design* doc; the
wire protocol is Heatmiser's V3.9 RS485 protocol document.

## Status: milestone 5, hot water on Matter (gate passed)

| Part | State |
| --- | --- |
| `hm_proto`: V3 frames, CRC, DCB decode, unique address map, write encoders and read-back checks (pure C) | Done, host tested |
| `hm_sched`: write queue first, round-robin full-DCB poll, read-back confirmation, retries, offline back-off, scan (pure C) | Done, host tested against simulated stats |
| `hm_bus`: UART1 at 4800 8N1, TX GPIO16 / RX GPIO15, one transaction at a time | Done, bench proven (milestone 1) |
| `hm_poller`: the bus task running `hm_sched` on the board | Done, bench proven (milestone 2) |
| `zone_store`: saved zone list and names in NVS, a live view of each stat (including its programme), pending writes with failure notices, hot water boost and overrides that survive a restart, clock sync, holiday for all, bus counters and frame trace | Done; the view, programme helpers and saved format are host tested |
| `board`: Waveshare 7" RGB panel with bounce buffers, GT911 touch, CH422G backlight, LVGL 9 via esp_lvgl_port | Done, bench proven (milestone 3 soak) |
| `ui`: the Copper design. Dashboard, zone detail, hot water with boost, schedule and hot water timer editing, rename keyboard, settings, zones and scan, bus diagnostics, system, drift test pattern | Built; desktop preview renders every screen ([docs/screens](docs/screens/)) |
| `net`: Wi-Fi station, SNTP clock, timezone (Matter drives Wi-Fi when it runs) | Built |
| `matter_bridge`: esp-matter bridge, an Aggregator and one bridged Thermostat per zone with saved endpoint IDs; setpoint, Heat/Off and key lock both ways; pairing screen with QR code and controller list | Done, bench proven with Aqara Hub M3 and Apple Home (milestone 4); mapping host tested |
| `matter_bridge`, milestone 5: the PRT/HW-N's hot water as a bridged On/Off Plug-in Unit ("Hot water") with a saved endpoint ID. On boosts for 60 min, Off holds the water off until the timer period ends; never forced for good | Done, bench proven with Aqara Hub M3 and Apple Home (milestone 5); switch logic host tested |
| Gate: a boost survives a reboot and ends on time | Passed 22 Nov 2026; see [bench-milestone5.md](docs/bench-milestone5.md#bench-results-22-nov-2026) |
| Gate: pair Aqara Hub M3 and Apple Home together, reboot and rescan without losing rooms | Passed 21 Nov 2026; see [bench-milestone4.md](docs/bench-milestone4.md#bench-results-21-nov-2026) |
| Gate: no drift during a soak with Wi-Fi on | Passed 19 Nov 2026 (15 h 40 min); see [bench-milestone3.md](docs/bench-milestone3.md#bench-results-19-nov-2026) |

Milestone 1 (bus bring-up) passed its gate on 17 Nov 2026 and milestone 2
(poller and writes) on 18 Nov 2026 on 11 of 12 stats, and milestone 3 (the
touchscreen) on 19 Nov 2026, and milestone 4 (Matter, with the Aqara Hub M3
and Apple Home together) on 21 Nov 2026, and milestone 5 (hot water on
Matter, a boost surviving a restart) on 22 Nov 2026; see
[bench-bringup.md](docs/bench-bringup.md#bench-results-17-nov-2026),
[bench-milestone2.md](docs/bench-milestone2.md#bench-results-18-nov-2026),
[bench-milestone3.md](docs/bench-milestone3.md#bench-results-19-nov-2026),
[bench-milestone4.md](docs/bench-milestone4.md#bench-results-21-nov-2026) and
[bench-milestone5.md](docs/bench-milestone5.md#bench-results-22-nov-2026).

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

ESP-IDF 6.1 or later; CI builds with 6.1.1. LVGL, esp_lvgl_port and the GT911 driver come from the
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
The firmware build is skipped when a change only touches documentation
(Markdown files or `docs/`). It runs on the self-hosted runner (labels
`self-hosted`, `linux`; Linux or WSL2). The first run installs ESP-IDF v6.1.1
into `_heatmiser-ci-cache` beside the runner's work folder, and later runs
reuse it along with the compiler cache and the component manager's downloads.
The runner needs the ESP-IDF Linux prerequisites:
`sudo apt-get install git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0`.
Pull requests from forks never run on that runner.
