# Bench: milestone 4, Matter

Milestone 4 puts every zone on Matter. The board is now a Matter bridge: a
root node, an Aggregator, and one bridged Thermostat endpoint per saved zone.
A controller sees one thermostat per room, named as on the screen, with the
room temperature, the setpoint, Heat or Off and whether it is calling for
heat. Each zone keeps its endpoint number across reboots, so rooms and
automations stay put.

Hot water (the PRT/HW-N on stat 3) is milestone 5. Stat 3's heating zone is
a thermostat here like the others; its hot water is not on Matter yet.

**Gate:** pair with the Aqara Hub M3 and Apple Home at the same time, then
reboot and rescan without losing rooms in either.

Allow about an hour, most of it the first build.

## What a controller can do

| On the controller | On the stat |
| --- | --- |
| Setpoint (5 to 35 °C, whole degrees) | Set temperature. Half degrees round to the nearest whole degree, which is all the stat holds |
| Heat | Stat on, normal programme |
| Off | Frost protection (the stat stays on and keeps its frost temperature). Off on the screen and Off from Matter both show as Off |
| Keypad lockout, where the app shows it | Key lock |
| Room temperature, heating demand, running state | Read from the stat; "No response" while it is offline |

Hold and the temperature display mode are shown but read only; a write to
anything else is refused.

## 0. Build and flash

This build adds Bluetooth and the Matter stack, so start from a clean
configuration once. The partition table has not changed, so there is no
need to erase: zones, names and Wi-Fi stay.

```
git pull
del sdkconfig
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

The first build downloads esp-matter and the Matter SDK (connectedhomeip)
into `managed_components/`. That is large, and the first compile takes a long
time (40 to 60 minutes on the CI machines); later builds are quick. On
Windows the Matter SDK has very long file paths. If the build fails with a
"file not found" on a path that plainly exists, turn on long paths (as
administrator, `reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v
LongPathsEnabled /t REG_DWORD /d 1 /f`, then a new prompt) and keep the repo
near the drive root, such as `C:\hm`.

You should see `HMBRIDGE-BOOT`, the display coming up, Matter starting with
`Matter is joining '<your network>'` (Matter now drives Wi-Fi and uses the
network saved in milestone 3), the pairing codes, then `hm>`.

Type `matter`. It should show the QR code and manual code, "Pairing window
open", 0 controllers, and 12 thermostat endpoints with their names and
current values. Note the `ep` number beside each stat: the gate checks they
don't change.

| What you see | Meaning |
| --- | --- |
| `Matter failed to start; running as a local controller` | The bridge carries on without Matter. Send me the boot log from `HMBRIDGE-BOOT` to that line |
| `Matter is not running` from `matter` | As above |
| Fewer than 12 endpoints | Send me `matter` and `zone` |
| A reset loop | Send me the boot log; it usually means memory |

## 1. Before pairing

- On the screen: sliders icon, **Matter**. The QR code is on the left with
  "Ready to pair" and a countdown. The window opens by itself at boot while
  the bridge has no controller.
- Phone on the same Wi-Fi as the bridge (2.4 GHz), Bluetooth on, within a
  few metres of the board.
- The bridge uses Espressif's test device certificates (vendor 0xFFF1), so
  every app will say the accessory is not certified. That is expected for a
  home-built device; carry on past the warning.

## 2. Pair with the Aqara Hub M3

In the Aqara Home app: **+**, **Add device**, then the Matter option (it may
be listed as **Matter device** or under **Scan code**). Choose the Hub M3
as the controller if asked, and scan the QR code on the bridge's screen.
Accept the uncertified-device warning. Pairing takes a minute or two; the
bridge's Matter screen then shows the controller and "Paired".

You should get 12 thermostats, named as on the bridge. Put each in its room.

If the Aqara app refuses the bridge (it may not accept test certificates),
write down its exact message and do section 3 first. Then share from Apple
Home to Aqara: in the Home app, a thermostat's settings, **Turn On Pairing
Mode**, copy the code, and in Aqara Home add a Matter device with that code.
If Aqara refuses that too, the gate cannot pass with test certificates;
send me the message and we will decide on a way round it.

## 3. Add Apple Home

On the bridge: **Pair another controller** on the Matter screen (or
`matter pair`). The QR code comes back with a 15 minute countdown. This
time the bridge is found over Wi-Fi rather than Bluetooth.

In the Home app: **+**, **Add Accessory**, scan the code, **Add Anyway** at
the uncertified warning. Put each thermostat in its room.

The bridge's Matter screen should now list two controllers (Aqara and Apple
Home), and `matter` should show 2 controllers.

## 4. Use it, both ways, in both apps

For two or three zones, including stat 2 (Emma's bedroom):

1. Change the setpoint in Aqara. The stat's display and the bridge screen
   follow within about 10 s, and Apple Home shows the new setpoint.
2. Change it on the bridge screen. Both apps follow.
3. Turn a zone Off in Apple Home. The stat goes to frost protection; the
   bridge tile and Aqara show Off. Turn it back to Heat in Aqara.
4. Turn the stat's key lock on from the bridge screen. If Aqara shows a
   keypad lock for the thermostat, it should follow (Apple Home does not
   show one).
5. Note how long a change made on a stat's own buttons takes to appear in
   each app (it depends on the poll interval in Settings).

## 5. Reboot and rescan (the gate)

1. Press the reset button. After about 30 s both apps should show every
   thermostat again, in its room, responding. Run `matter`: the same 2
   controllers and the same `ep` numbers as before.
2. Power cycle the board at the supply and check the same again.
3. Rescan: sliders icon, **Zones and scan**, **Scan the bus**. When it
   finishes, check both apps and `matter` once more: no thermostat gone, no
   duplicates, rooms unchanged, same `ep` numbers.
4. Change a setpoint from each app once more to show they still control the
   stats.

The gate passes when, after the reboot, the power cycle and the rescan,
both apps still have all 12 thermostats in their rooms and both still
control them.

## What to send back

1. `matter` output before the first reboot and after the rescan.
2. Screenshots of the thermostats in Aqara Home and Apple Home after the
   rescan.
3. Any message an app gave while pairing, word for word.
4. Anything odd: a setpoint that bounced back, a thermostat stuck on "No
   response", a slow update, a reset.

## Removing a controller, and notes

- Matter screen, **Remove** beside a controller (or `matter remove
  <fabric>`) makes the bridge forget it. The app will then show the
  thermostats as not responding; delete the bridge there too.
- After removing the last controller, reset the board so the first pairing
  can use Bluetooth again.
- Removing a zone from the zones screen removes its thermostat from every
  controller. Adding it back gives it a new endpoint, so it comes back
  without its room. Factory reset forgets the zones, Wi-Fi and all Matter pairings.

## Console reference (new in milestone 4)

| Command | Does |
| --- | --- |
| `matter` | Pairing codes, whether pairing is open, the controllers with their fabric numbers, and each zone's endpoint and current values |
| `matter pair [minutes]` | Open pairing for another controller, 3 to 15 minutes (15 by default) |
| `matter remove <fabric>` | Forget a controller, by the fabric number `matter` shows |
| `screen matter` | Open the Matter screen |
