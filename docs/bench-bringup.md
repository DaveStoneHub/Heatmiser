# Bench bring-up: RS485 to one PRT-N

This is milestone 1 of the build plan. The firmware side is done; these steps
need hands on the bench. Allow about 30 minutes plus the 1-hour soak.

**Gate:** clean full-DCB reads from one PRT-N for an hour with no CRC errors.
The soak is stricter than that: any timeout, short frame, bad frame or UART
line error also fails it.

## What you need

- The Waveshare board and a USB-C cable to the PC, in the port marked **UART**
  (the console; the other port is native USB and is not used yet).
- One PRT-N, powered, with its comms address known. The address is set in the
  stat's setup features (see the PRT-N manual); it is 1 to 32.
- A short twisted pair from the board's RS485 terminals to the stat's RS485
  terminals. Keep the board's 120 Ω terminator fitted.
- ESP-IDF 5.4 (the Windows installer from Espressif is the easy way), and
  Python with `pip install pyserial` for the soak logger.
- Optional: an oscilloscope or a USB logic analyser for cross-checking timing.

> The PRT-N is mains powered. Wire the RS485 pair with the stat's power off,
> and leave any mains wiring to someone qualified.

> **Only one master on the bus.** For the bench, use a stat that is not wired
> to the Touchpad, or disconnect the Touchpad first.

## 0. Build and flash

From an ESP-IDF command prompt in the repo:

```
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Use your board's COM port (Device Manager → Ports). You should see
`HMBRIDGE-BOOT`, a `hm_bus` line showing `UART1 TX=GPIO15 RX=GPIO16 4800 8N1`,
and the `hm>` prompt. Type `help` for every command.

If your stat is not at address 1, either pass the address to each command
(`read 5`) or set *Default thermostat address* under `idf.py menuconfig` →
*Heatmiser RS485 bus*.

## 1. First read and A/B polarity

Vendors disagree on which line is A and which is B, so we confirm it by trying
both ways.

1. Wire the board's A to the stat's A and B to B, as labelled.
2. Run `scan`. It probes 1 to 32 in about 10 s and lists the stats that answer.
3. Run `read <addr>`. A good read prints `Result: ok`, the raw request and reply
   in hex, the turnaround, the decoded DCB (model PRT, temperature, set point,
   clock, comfort levels) and a C array of the reply.
4. **Swap the two wires** and run `read <addr>` again. It should now fail.
5. Swap back, and read once more to confirm it works again.

How to read the result:

| What you see | Meaning |
| --- | --- |
| `Result: ok` one way, timeout the other | Polarity confirmed. Note which terminal names joined up. |
| Timeout both ways, no UART errors | The stat isn't hearing us: check the address, power, the pair at both ends, and that no other master is connected. |
| `UART line errors` or `bad frame` | Bytes are arriving garbled: the classic sign of swapped A/B, or a missing ground or termination on a long run. |
| `transceiver echoes our TX` | Harmless; the driver strips the echo. Mention it, as it tells us how the auto-direction circuit behaves. |
| Works both ways | Unusual; tell me, as it points to biasing on the board that hides polarity. |

Optional check with a scope or logic analyser: run `sniff 60` on the bridge
while nothing else is on the bus to make sure the line is quiet (no bursts).

## 2. Turnaround

Run:

```
turnaround <addr> 100
```

It does 100 full-DCB reads and 100 one-byte reads and prints two lines starting
`TURNAROUND`, with min, average and max time from the end of our request to the
start bit of the stat's reply. The timing comes from an interrupt on the RX pin,
so it is good to a few microseconds. The design assumed 100 to 300 ms; the
number matters for the 10-second sweep budget with 12 stats.

Optional cross-check: put a scope or logic analyser on the board's TX and RX
test points (GPIO15 and GPIO16) or across A and B, run `read <addr>`, and
measure from the last stop bit of the 10-byte request to the first start bit
of the reply. It should agree with the firmware to well under a millisecond.

## 3. The 1-hour soak (the gate)

Close `idf.py monitor` first (the port can only be open once), then from the
repo on the PC:

```
python tools/soak_logger.py --port COM5 --addr <addr>
```

This starts `soak start <addr> 3600 1000` on the board, which reads the full
DCB once a second for an hour (about 3,600 reads). The logger writes every line
with a timestamp to `soak-YYYYMMDD-HHMM.log`, prints a progress line each
minute and any error the moment it happens, and watches for a board reset or a
stalled console. At the end it prints:

```
SOAK-RESULT PASS duration=3600s reads=3600 ok=3600 crc=0 timeout=0 ...
GATE: PASS
```

Ctrl+C stops the soak early and prints the result so far. You can also run the
soak by hand in any terminal (`soak start <addr>`, `soak status`, `soak stop`)
and check a saved capture later with `python tools/soak_logger.py --parse <file>`.

Leave the stat running normally during the soak; pressing its buttons is fine
and a good test. For a harder run, `--period 0` reads back to back.

## What to send back

Paste these into the project thread:

1. The full output of one good `read <addr>`, including the C array (it goes
   into the unit tests as a real captured frame).
2. Which board terminal joined to which stat terminal when it worked.
3. The two `TURNAROUND` lines, and any scope figure.
4. The soak log file, or at least its last lines.
5. Anything odd: echo, UART errors, a reset.

## Troubleshooting

| Symptom | Try |
| --- | --- |
| No `hm>` prompt | Wrong USB port (use the one marked UART) or wrong COM port; 115200 baud. |
| `scan` finds nothing | Swap A/B; check the address; check the stat is powered and not on another master's bus. |
| Occasional CRC errors | Termination at the far end, twisted pair, ground reference between the board and the stat, cable away from mains. |
| Regular timeouts on one-byte reads only | Tell me; it would point to a stat that is slow to answer short reads. |
| Board resets during the soak | Power: use a solid 5 V USB supply, not a laptop hub. |
