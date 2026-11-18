# Bench: milestone 2, poller and writes on every stat

## Bench results (18 Nov 2026)

**Gate passed on 11 of 12 stats; stat 2 set aside.** House bus, Touchpad
removed, master 0x81.

| Check | Result |
| --- | --- |
| Single stat on the bench (`gate 1 slow`) | PASS: 6 of 6 steps, no retries; the display followed every change |
| House bus, stats 1 and 3-12 (10 PRT-N, 1 PRT/HW-N at address 3) | PASS: 66 of 66 steps confirmed first time, 0 retries |
| Write plus read-back | 460 ms each; up to about 1.5 s when a poll of another stat was already queued |
| Stat 2 | Not answering; judged to be a faulty stat or damaged wiring. Left out of the gate for now |

**The Touchpad must be physically disconnected.** On the first house run the
Touchpad was still wired in, in its "shutdown" mode. Every stat acked every
write but put back the Touchpad's value straight away, so only writes of the
value the stat already had read back correctly (40 of 72 steps failed). A
30-second `sniff` heard nothing from it, so a quiet sniff does not prove the
Touchpad is inactive. With it unplugged, every write took first time.

Signature to recognise: `read-back differs after 6 attempt(s)` on every
stat, for every write that changes a value.

Milestone 2 adds the scheduler that will run the bridge: a round-robin poll of
every stat's full DCB, a write queue that goes first, read-back confirmation of
every write, retries, offline back-off and a bus scan. This guide checks it on
the real bus.

**Gate:** setpoint and mode writes confirmed on every stat. The `gate` command
does the whole check and prints `GATE2-RESULT PASS` or `FAIL`.

Allow about 30 minutes. The gate itself takes about a minute for 12 stats.

## Before you start

- **The Touchpad must be unplugged from the bus, not just shut down.** Only
  one master may talk on it, and in "shutdown" mode it still resets the stats
  (see the results above). Note
  the zone names from the Touchpad first if you have not already; they live in
  the Touchpad, not the stats.
- Connect the bridge to the house bus at one end of the chain, board A to the
  yellow wire and B to the blue, as on the bench. Keep the board's 120 Ω
  terminator fitted, and fit one at the far end if there is not one already.
- The gate changes each stat's setpoint by 1 °C and switches its mode to frost,
  heat and off for a second or two each, then puts it back as it was. Run it
  when that doesn't matter. Nothing is stored on the bridge; if the gate is cut
  short, a stat may be left one degree off or in another mode (it says which).

> The stats are mains powered. Leave any mains wiring to someone qualified.

## 0. Build and flash

Same as milestone 1, from an ESP-IDF prompt in the repo, on the board's
**UART** USB-C port:

```
git pull
idf.py build
idf.py -p COM5 flash monitor
```

You should see `HMBRIDGE-BOOT`, then `poller ready (paused)` and the `hm>`
prompt. The poller does nothing until you start it.

## 1. Scan

```
poll scan
```

This probes addresses 1 to 32 (100 ms timeout, then a 300 ms second pass over
the empty ones), adds every stat that answers, starts polling and prints a
table. Expect 12 stats: 11 `PRT` and one `PRT/HW`, all `online`. The scan
should take about 10 to 15 seconds.

| What you see | Meaning |
| --- | --- |
| Fewer than 12 | Note which addresses are missing; check each one's comms address in its setup menu, and its pair at both ends. `read <addr>` shows what happens on the wire. |
| More than 12, or one address twice in the stats' menus | Two stats share an address; they will garble each other. |
| CRC or `bad` counts rising in `poll status` | Termination or wiring on the long run: far-end terminator, twisted pair, away from mains. |

## 2. Watch the poll

```
poll status
```

Run it a few times over a minute. Each stat's `polls` count should rise by
about 6 a minute (one read every 10 s), with `miss`, `to`, `crc` and `bad` at
0, and `seen` under 10 s. The table shows set point, room temperature (from
whichever sensor the stat uses), mode, heating, key lock and hold.

## 3. One write by hand

Pick one stat you can see, say address 3:

```
set 3 temp 22
set 3 mode frost
set 3 mode heat
set 3 temp <its old set point>
```

Each line should print `confirmed, 1 attempt(s)` in about half a second, and
the stat's own display should follow. Mode `frost` should show the stat's
frost symbol; `off` turns the display off.

## 4. The gate

```
gate
```

For every stat it writes set point +1 and back, then mode frost, heat and off,
then back to the mode it started in. Each step must be acked and read back.
Lines look like this:

```
GATE2 stat=3 model=PRT start: set 20 C, mode heat
GATE2 stat=3 step=setpoint->21  CONFIRMED frames=1 time=510ms
...
GATE2-RESULT PASS stats=12 steps=84 failed=0 retries=0 worst=1120ms total=70.3s
```

To watch one stat's display change, `gate 3 slow` pauses 5 s after each change.

| What you see | Meaning |
| --- | --- |
| `FAIL`, many frames | The stat never acked: wiring or address. Check `poll status` for that stat's timeouts. |
| `FAIL(read-back)` | The stat acked but reports another value. Tell me which step and stat; it would mean a model-specific rule we don't know yet. |
| `retries` above 0 but `PASS` | Writes needed a second try. A few is fine; many points to noise on the run. |

## 5. Offline back-off (no hardware needed)

```
poll add 30
```

Address 30 has no stat. After about 30 s `poll status` shows it `OFFLINE`,
and from then on its `polls` count rises by only one a minute while the real
stats keep their 10 s pace. Then `poll remove 30`.

## 6. Optional: hot water

On the PRT/HW-N (say address 12): `set 12 hw on`, `set 12 hw off`, then
`set 12 hw timer` to hand it back to its programme. The first two should show
`confirmed`; `timer` shows `confirmed` once acked (either state is right then).
This belongs to milestone 5, but a quick look now is useful.

## What to send back

1. The whole `gate` output, ending with the `GATE2-RESULT` line.
2. One `poll status` table after a few minutes of polling.
3. How long `poll scan` took and what it found.
4. Anything odd: a stat that misses polls, retries, a display that didn't follow.

## Console reference

| Command | Does |
| --- | --- |
| `poll` or `poll status` | Table of every stat: state, values, counters |
| `poll start [s]`, `poll stop` | Run or pause polling; optional interval 5 to 60 s |
| `poll scan` | Scan 1 to 32, add what answers, start polling |
| `poll add <addr>...`, `poll remove <addr>` | Manage the list by hand |
| `poll log on/off` | Event lines (online, offline, write results) |
| `set <addr> temp/frost/floor <C>` | Set point, frost temperature, floor limit |
| `set <addr> mode heat/frost/off` | Mode, as the bridge will map it to Matter |
| `set <addr> lock on/off`, `hold <min>`, `holiday <h>` | Key lock, temperature hold, holiday |
| `set <addr> hw timer/on/off` | PRT/HW-N hot water |
| `gate [addr...] [slow]` | The milestone 2 gate |

The milestone 1 commands (`read`, `scan`, `turnaround`, `sniff`, `soak`, ...)
still work alongside the poller; the bus takes one transaction at a time.
