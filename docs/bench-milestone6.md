# Bench: milestone 6, clock sync, settings, updates and the week

## Bench results

_Not run yet. Fill in from the results file as the week goes._

Milestone 6 finishes the design's last build step. The schedule editor,
holiday for every zone, "Sync clocks now" and the bus diagnostics screen
arrived in milestones 3 to 5. This milestone adds:

- **Automatic clock sync.** Every stat's clock is set once a day at 03:10,
  and straight away if it is more than 2 minutes out.
- **Firmware updates** from GitHub Releases, or from a PC on the LAN for
  bench builds, with automatic rollback.
- **A settings PIN**, and a **master address** you can change.
- **A health line** in the log every hour, and a PC logger for the week.

**Gate:** a full week running without intervention.

Allow about an hour for sections 0 to 4. Section 5 needs the PR merged and a
release tagged. Section 6 is the week.

## What changed underneath

- **Clock sync.** Once the bridge has the time, it checks every stat once a
  minute:
  - If the stat's clock is more than 2 minutes out, it is set now. That
    happens after a power cut at the stat, after you change its clock by
    hand, or when the bridge first gets the time after its own restart.
  - Otherwise each stat is set once a day at 03:10, which is after the
    summer time changes at 01:00 and 02:00.
  - A stat is written at most once every 15 minutes, so one that ignores
    the write isn't written every minute.
  - A restart of the bridge doesn't reset clocks that are already right.
  - A setpoint override is put back after the clock write, as "Sync clocks
    now" has done since PR #21.
- **Updates.**
  - The System screen has **Check for update** and **Install**. Check asks
    GitHub for the latest release and compares its tag with the running
    version.
  - Install downloads the release's `heatmiser_bridge.bin` over HTTPS into
    the other app slot and restarts.
  - The new firmware is **on trial**. It is kept once it has run 90 seconds
    and a stat answers. If no stat answers within 5 minutes, or it crashes or
    hangs first, the bootloader goes back to the previous firmware.
  - The thermostats carry on by themselves throughout.
- **PIN.**
  - Four digits guard Settings and Zones and scan. The dashboard's Scan now
    button asks for it too.
  - The PIN is asked once and stays unlocked until you go back to the
    dashboard or the screen blanks.
  - Five wrong PINs in a row lock the keypad for a minute.
  - `pin off` on the console clears a forgotten PIN.
- **Master address.** The tile offers 0x81 to 0x88. The choice is saved and
  used at the next frame. The console's `master` command still reaches the
  rest for a test, but doesn't save it.

**`sdkconfig.defaults` changed** (rollback, updates over HTTP, the
certificate bundle, and more internal RAM for Bluetooth: its controller code
out of IRAM, 8 Wi-Fi transmit buffers instead of 16, and buffers over 1 KB
in PSRAM), and rollback needs a
new bootloader. So this time, delete `sdkconfig` and flash the whole board
over USB. An older `sdkconfig` stops the build with "delete sdkconfig and
build again".

## 0. Build and flash

```
git fetch
git checkout claude/project-thread-7ir2b9
del sdkconfig            (rm sdkconfig on Linux)
idf.py build
idf.py -p COM5 flash monitor
```

At boot, look for:

```
ota: running <version> from ota_0
```

The boot log also shows `BLE_INIT: Put all controller code in flash` with
no `Malloc failed` after it, and `main: RAM after Matter and Wi-Fi: ...`
then `console ready`. Type `display`: its last line should show internal
RAM free, with the lowest since boot above about 10,000 bytes.

Type `health`. You should get one line like this:

```
HMBRIDGE-HEALTH up_s=95 fw=<version> zones=12 online=12 offline=- polls=... clk_ok=0 ... time=1 wifi=1 ... matter=1 fabrics=2 ota="idle" trial=0
```

Type `ota`. It should say `Firmware <version>. Updates: idle.`

Within a minute or two of the clock being set, some stats may get a clock
write: any that were more than 2 minutes out. The log shows
`stat N: clock is M min out; setting it`, then
`automatic clock sync: ... queued`.

## 1. Clock sync

1. Pick one PRT-N and use its own buttons to set its clock 10 minutes wrong.
2. Within about 2 minutes the log should show
   `stat N: clock is 10 min out; setting it`, and the stat's display should
   show the right time again. If that stat's clock was set in the last 15
   minutes (at boot, say), allow up to 15 minutes.
3. Pick a stat the bridge hasn't set in the last 15 minutes. Set its clock
   10 minutes wrong first, then straight away turn its setpoint up 2
   degrees, all within about a minute. (Changing the clock at the stat
   itself drops an override, so the setpoint has to go up after the clock
   change.) After the bridge's clock write the log should show
   `stat N: setpoint 23 written back after the clock write`, and the stat
   should keep the raised setpoint.
4. Run `health` and check that `clk_ok` went up by 2 and `clk_fail` is 0.

The daily sync happens at 03:10, so you'll see it in the week log (section
6): `automatic clock sync: 12 stats queued (12 daily, 0 out)`.

## 2. Settings PIN

1. Open Settings and tap **PIN**. Enter 4 digits, then the same 4 again. The
   tile should say **On**.
2. Go back to the dashboard and tap the settings button. The keypad should
   show **Enter the PIN**. Enter it, and Settings should open.
3. From Settings open Zones and scan, then come back. It shouldn't ask
   again.
4. Go back to the dashboard, open Settings, and enter a wrong PIN 5 times.
   The keypad should close with "Too many wrong PINs. Try again in a minute".
   Check that it refuses for a minute and then lets you try again.
5. Leave the screen in Settings until it blanks, then wake it. It should
   show the dashboard, and Settings should ask for the PIN again.
6. Type `pin off` on the console. Settings should open without a PIN.
7. Set a PIN again if you want one for the week. Tap **PIN** then **Change
   PIN** or **Turn off PIN** to check both.

## 3. Master address

1. Settings → **Master address** → **0x82**.
2. Run `zone` after 20 s. Every stat should still be online.
3. Bus diagnostics should show no new timeouts.
4. Press reset. The boot log should show `master address 0x82 (saved)`.
5. Set it back to **0x81**.

If any stat stops answering at 0x82, note which one and set 0x81 back. The
V3 document says a stat answers any master address, but that hasn't been
tried on your stats before.

## 4. Updates from your PC, and a rollback

**An update that works.**

1. Leave the bridge running the build from section 0. On the PC, in the
   repository, run:
   ```
   python tools/ota_serve.py
   ```
   It prints a line to type on the bridge, such as
   `ota install http://192.168.1.20:8070/heatmiser_bridge.bin`.
   Allow Python through the Windows firewall if it asks.
2. Type that line on the bridge console. The log shows
   `HMBRIDGE-OTA progress=10` up to 100, then `result=OK ... restarting`.
   The System screen shows "Updating, N% downloaded" meanwhile.
3. After the restart:
   - The boot log shows `ota: running <version> from ota_1` and
     `HMBRIDGE-OTA trial`.
   - About 90 s later, `HMBRIDGE-OTA kept`.
   - `ota` no longer says "on trial".
4. Check the bridge kept its zones, names, Matter endpoints (`matter`), Wi-Fi
   and PIN.

Note how long the download took. Expect a minute or two for about 3 MB.

**An update that doesn't start properly.** The new firmware is only kept
once a stat answers, so a disconnected bus stands in for a broken build.

5. Start `ota_serve.py` again, and type the same `ota install` line.
6. As soon as the bridge restarts, unplug the RS485 pair at the bridge.
7. After 5 minutes the log should show `HMBRIDGE-OTA rollback`, and the
   bridge should restart into the previous slot (`running ... from ota_1`
   again).
8. Plug the bus back in. The System screen's Firmware row should say the
   last update didn't start properly and was undone.

## 5. Updates from GitHub (after merging)

Releases come from main only, so do this once the PR is merged.

1. Tag the merge on main and push the tag:
   ```
   git checkout main
   git pull
   git tag v0.6.0
   git push origin v0.6.0
   ```
2. The **Release** workflow runs on your runner. It builds the firmware with
   version `v0.6.0`, checks the built version matches the tag, and publishes
   the release with `heatmiser_bridge.bin` and its SHA-256.
3. On the bridge, open System and tap **Check for update**. Your bench build
   is not a release version, so the screen should say `v0.6.0 available`.
4. Tap **Install v0.6.0** and confirm. It should download from GitHub,
   restart, go on trial and be kept, as in section 4.
5. Tap **Check for update** again. It should say `v0.6.0, up to date`.

The bridge runs this release for the week. Later updates are a new tag
(v0.6.1 and so on) and the two buttons.

## 6. The gate: a week without intervention

1. With v0.6.0 running and all 12 stats online, start the logger on the PC:
   ```
   pip install pyserial
   python tools/week_logger.py --port COM5
   ```
   It writes `week-YYYYMMDD-HHMM.log` and prints each hourly health line.
2. Then leave the bridge alone for 7 days. Using the stats, the screen,
   Apple Home and Aqara as you normally would is fine. Don't press reset,
   reflash, power cycle or change settings.
3. Each day, glance at the logger and the screen and note in the results
   file anything odd: a toast, a tile offline, an app showing "No response".
4. After 7 days the logger prints a summary and `GATE: PASS` or `FAIL`.

The logger fails the gate if any of these happen:
- the board resets, or a firmware rollback happens;
- no health line arrives for 2.5 hours;
- once the bridge has run a day, a stat clock write is more than 26 hours
  old;
- the bridge loses the time.

It reports these for you to judge: stats offline, failed writes, Wi-Fi drops
and Matter controllers.

The PC must stay awake for the week. Turn off sleep, and keep the USB cable
in. In Windows' power plan, under *Advanced settings → USB settings*, turn
**USB selective suspend** off too, or Windows may drop the serial port.

If Windows does drop the port, the logger notes it, waits for the port to
come back and carries on by itself. If the logger itself stopped, start it
again with the same log file, and it picks the week up where it was:

```
python tools/week_logger.py --port COM5 --log week-20261122-2213.log
```

The time it couldn't see shows as a logger gap, not a failure. The bridge's
`up_s` in the next health line still shows whether the bridge restarted, and
the logger fails the gate if it did. Run
`python tools/week_logger.py --parse <log>` to re-check a log at any time.

## What to send back

1. The `health` and `ota` output from section 0.
2. Sections 1 to 4: pass or fail for each step, and the log lines for
   anything that failed.
3. Section 4: how long the download took.
4. Section 5: the release link, and screenshots of the System screen before
   and after installing.
5. Section 6: the logger's summary, and the log file.

## Console reference (new in milestone 6)

| Command | Does |
| --- | --- |
| `health` | Prints the health line (the bridge also prints it every hour) |
| `ota` | Firmware version, update state, latest release, and why the last attempt failed |
| `ota check` | Asks GitHub for the latest release |
| `ota install` | Installs the release found by `ota check` |
| `ota install <url>` | Installs a build from any http:// or https:// URL, such as `tools/ota_serve.py` on your PC |
| `pin` / `pin off` | Shows whether the settings PIN is on, or turns it off |
