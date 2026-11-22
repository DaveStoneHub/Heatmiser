# Bench: milestone 5, hot water on Matter

## Bench results (22 Nov 2026)

**Gate passed.** Firmware from main at a848a5b (PR #19), all 12 stats on the
bus, Aqara Hub M3 and Apple Home on the bridge.

| Check | Result |
| --- | --- |
| Hot water endpoint | Endpoint 14 (0x000E), stat 3. The 12 thermostats kept endpoints 2 to 13 |
| Shows in Apple Home | Without re-pairing, as "Hot water", shown as a power socket (Display As can change that) |
| Shows in Aqara Home | Without re-pairing, as "A4254000": Aqara reads names only at pairing, so it was renamed in the app |
| On with the timer off, from both apps | 60 minute boost |
| Off during a boost with the timer off | Back to the timer |
| Off with the timer on (Sun 13:50, Apple Home) | Forced off for 491 min, to the end of the timer period at 22:00; stat confirmed; countdown on the screen |
| Boost across a reset | Carried on, minutes left correct |
| Boost across a power cycle, board off 5 min | Carried on, still counting down accurately |
| Boost end | Due 14:26, ended on time: back to the timer, `stat 3 hot water timer: confirmed (1 attempt)` |

Found on the way, fixed in PR #20:

- **"Force off" lit during a held off.** The screen highlighted the Force off
  button for an Off from Matter, although the hold ends with the timer
  period. A held off now lights no mode button, as a boost does.
- **Bare `chip[EM]:` and `chip[IM]:` lines in the log**, mostly about 5 s
  after boot. The Matter SDK prints the line header with printf and only the
  text through the log filter. The bridge now filters whole lines.

Still to watch: the held off ending at 22:00 on its own, and section 4 (the
stat's own buttons during a boost).

Milestone 5 puts the PRT/HW-N's hot water (stat 3) on Matter, and makes a
boost survive a restart. The bridge gets one new bridged endpoint, a switch
called "Hot water" (an On/Off Plug-in Unit, which every controller shows).
The 12 thermostats are unchanged and keep endpoints 2 to 13.

**Gate:** a boost survives a reboot and ends on time.

Allow about an hour and a half. Most of it is waiting for boosts to run out.

## What the switch does

The apps only show a plain on/off switch, so the bridge never leaves the water
forced on or off with no end (you chose this on 21 Nov):

| In Apple Home or Aqara | What the bridge does |
| --- | --- |
| **On** while the water is off | A 60 minute boost, then back to the timer |
| **On** while it is held off and the timer has it on | Back to the timer (so it comes on) |
| **On** while it is already on | Nothing |
| **Off** while the timer has it on | Held off until that timer period ends, then back to the timer |
| **Off** during a boost, or forced on, with the timer off | Back to the timer (so it goes off) |
| **Off** while it is already off | Nothing |

The switch shows whether the water is really on now, as the stat reports it.
On the bridge screen a held off shows as "Held off" with "Timer resumes in N
min". The screen's own buttons (Resume timer, Force on, Force off, Boost 30 /
60 / 120) work as before.

If someone changes the hot water with the stat's own buttons while the bridge
holds it on or off, the bridge lets go rather than fight it. A notice says so
and the hot water goes back to "Following timer" on the screen.

## What changed underneath

- **A boost carries on after a restart.** The bridge saves when the boost
  ends, in real time (UTC). After a restart it waits for the clock (SNTP,
  usually under a minute once Wi-Fi is up), then runs the boost to the same
  end time. If the end has already passed, the water goes back to its timer
  at once. If the clock is not set within 5 minutes, the bridge gives up and
  puts the water back on its timer, so it is never left forced on. Before this
  milestone a restart always ended a boost.
- **Forced on and forced off are remembered** across a restart too, so the
  screen still says "Forced on" afterwards.
- **A failed "back to the timer"** (the stat did not answer) is tried again
  every 30 s until it works, even across a restart.
- The hot water endpoint's number is saved apart from the zone list. The zone
  list format did not change, so going back to milestone 4 firmware keeps
  every zone.

`sdkconfig.defaults` is unchanged, so there is no need to delete `sdkconfig`
this time.

## 0. Build and flash

```
git pull
idf.py build
idf.py -p COM5 flash monitor
```

At boot, look for these lines after Matter starts:

```
matter: zone 3 '<name>' is endpoint <n>      (its thermostat, unchanged)
...
matter: stat 3 hot water is endpoint 14
```

The number may be higher than 14 if an endpoint was ever retired. Note it.
On later boots the line must show the same number.

Type `matter`. As well as the 3 controllers and 12 thermostats, it should end
with:

```
1 hot water endpoint(s):
  ep 14  stat 3  Hot water        reachable    off  (timer)
```

Type `hw`. It shows the hot water state, the mode, and the endpoint:

```
Stat 3 '<name>': hot water off, following timer; Matter endpoint 14
```

| What you see | Meaning |
| --- | --- |
| No "hot water is endpoint" line | Send me the boot log from `HMBRIDGE-BOOT` to `hm>`, and `zone` |
| `hot water endpoint failed` | As above |
| `No PRT/HW-N in the zone list` from `hw` | Stat 3 has not been read as a PRT/HW-N yet. Wait 10 s, or send me `zone` |

## 1. Find it in both apps

You do not need to pair again. The bridge tells both controllers that a new
device has joined.

- **Apple Home:** a new "Hot water" switch should appear within a minute or
  so, probably in the bridge's default room. Put it in a room. It may show as
  an outlet.
- **Aqara Home:** it should appear under the bridge as "Hot water". If it does
  not show after a few minutes, pull down to refresh on the bridge's page, or
  restart the app.

If one app never shows it, **do not remove and re-pair the bridge**: that
would lose every thermostat's room. Send me a screenshot and the `matter`
output, and we will find another way.

## 2. The switch, from both apps

Do these with a timer period in mind. `hw` shows the mode and minutes left
at any point; the bridge's hot water screen shows the same.

1. With the timer off, turn **Hot water** on in Apple Home. Within about 10 s
   the stat's hot water comes on, the bridge screen shows "Boost, 60 min left",
   and Aqara shows it on.
2. Turn it off in Aqara. The water goes off, the screen shows "Following
   timer", and Apple Home shows it off.
3. During a timer period (or set a short one on the bridge's hot water timer
   screen), turn it off in either app. The screen shows "Held off" and "Timer
   resumes in N min", where N is the time to the end of the period. At the end
   of the period, check the screen goes back to "Following timer".
4. While it is held off, turn it on again. It should go back to the timer and
   come on.
5. Start a boost on the bridge screen. Both apps should show the switch on.

## 3. The gate: a boost survives a restart and ends on time

Write down the time for each step.

1. On the bridge screen (or `hw boost 30`), start a **30 minute** boost while
   the timer has the water off. Note the time it should end. `hw` shows
   `boost, 30 min left`.
2. After about 5 minutes, press the reset button. As the bridge comes back:
   - The boot log shows `stat 3: hot water boost restored; waiting for the
     clock to time it`.
   - The hot water stays on throughout (the stat holds it).
   - The screen shows "Boost, waiting for the clock" for a short while, then
     the minutes left. The log shows `hot water override carries on, N min
     left`, with N about 25.
3. After about 10 more minutes, power cycle the board at the supply, and leave
   it off for 2 minutes before you turn it back on. It should carry on in the
   same way, now with about 13 minutes left.
4. Wait. The boost should end within a minute of the time you wrote down:
   the stat's hot water goes off, the screen shows "Following timer", and both
   apps show it off.
5. Run `matter`. The hot water endpoint has the same number as in section 0.

The gate passes when the boost carried on across both restarts and ended
within a minute of its planned end.

Then two shorter checks:

6. **A boost that ends while the bridge is off.** Start a 5 minute boost
   (`hw boost 5`), then turn the board off at the supply for 7 minutes. When
   it comes back, the log shows the boost restored, then the water goes back to
   its timer as soon as the clock is set.
7. **Held off across a restart.** During a timer period, turn the switch off
   from an app (held off), press reset, and check the screen still shows "Held
   off" with the right minutes, and that the water comes back on at the end of
   the period.

## 4. The stat's own buttons

While a boost is running from the bridge, turn the hot water off with the
PRT/HW-N's own buttons. Within about 10 s the bridge should show a notice
like "hot water changed at the stat; the bridge is no longer holding it on",
the screen should say "Following timer", and both apps should show the switch
off. The bridge should not turn it back on.

Note anything the stat itself does that surprises you here, such as the stat
dropping a forced on at the next timer change by itself. The design left that
as an open question, and the bridge now follows the stat if it does.

## What to send back

1. `matter` and `hw` output from section 0 and after section 3.
2. The boot log for the restart in section 3 step 2, from `HMBRIDGE-BOOT` to
   `hot water override carries on`.
3. The times from section 3: when the boost should have ended and when it did.
4. Screenshots of the Hot water switch in Apple Home and Aqara Home.
5. Anything odd: a switch that bounced back, a boost that ended early or
   late, the water left on, or a thermostat that changed endpoint or room.

## Console reference (new in milestone 5)

| Command | Does |
| --- | --- |
| `hw` | Hot water on the PRT/HW-N: on or off now, the mode (timer, boost, forced on or off), minutes left, and its Matter endpoint |
| `hw on` / `hw off` | What the Matter switch does, from the console |
| `hw boost <minutes>` | Boost for 1 to 1440 minutes; `hw boost 0` ends a boost |
| `hw timer` | Back to the timer |
| `matter` | Now also lists the hot water endpoint |
