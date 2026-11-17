# Heatmiser Matter bridge

Firmware for a Waveshare ESP32-S3-Touch-LCD-7 that acts as the single RS485
master for Heatmiser V3 thermostats (PRT-N and PRT/HW-N) and, in later
milestones, presents them as Matter thermostats with a touchscreen controller.

The design is in the project's *Heatmiser Matter Bridge System Design* doc; the
wire protocol is Heatmiser's V3.9 RS485 protocol document.

## Status: milestone 1, bus bring-up

| Part | State |
| --- | --- |
| `hm_proto`: V3 frames, CRC, DCB decoder (pure C) | Done, host tested |
| `hm_bus`: UART1 at 4800 8N1, TX GPIO16 / RX GPIO15, one transaction at a time, turnaround timing | Done, needs the bench |
| Bench console: `read`, `scan`, `turnaround`, `sniff`, `soak` | Done, needs the bench |
| Turnaround measured, A/B polarity confirmed, 1-hour clean soak | Bench steps, see [docs/bench-bringup.md](docs/bench-bringup.md) |

## Layout

```
components/hm_proto/   Frames, CRC-16/CCITT-FALSE, DCB decode. No ESP-IDF dependencies.
components/hm_bus/     RS485 transport on UART1 (Kconfig: pins, baud, master address, timeouts).
main/                  Bring-up console (esp_console REPL) and soak test.
test/host/             Host unit tests for hm_proto, including the spec's Appendix A CRC as reference.
tools/soak_logger.py   PC side of the 1-hour gate: starts the soak, logs, counts errors, watches for resets.
tools/hm_frame.py      Build or CRC-check V3 frames on the PC.
docs/                  Bench instructions.
```

## Host tests

Any C compiler and CMake:

```
cmake -S test/host -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Firmware

ESP-IDF 5.4 (5.x should work):

```
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

The console is on UART0 at 115200, which is the board's USB-C port marked
**UART**. Bus settings are under `idf.py menuconfig` → *Heatmiser RS485 bus*.

CI runs the host tests, an ESP-IDF build and a check of the PC tools on every
pull request.
