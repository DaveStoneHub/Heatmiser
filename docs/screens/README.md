# Touchscreen screens

Every screen of the Copper UI as the firmware draws it, at the panel's native
800x480. They are rendered on the host by the desktop preview in
`tools/ui_preview`, which runs the real `ui` component against fake data, so
they match the code rather than a mock-up. The clock in each shot is the time
the preview was run.

To regenerate them:

```sh
cmake -S tools/ui_preview -B build-preview
cmake --build build-preview -j
./build-preview/ui_preview docs/screens
```

CI renders the same set on every push (the "UI preview screenshots" job, artifact `ui-screenshots`).

| Screen | |
|---|---|
| 01-dashboard-empty | ![01-dashboard-empty](01-dashboard-empty.png) |
| 02-dashboard | ![02-dashboard](02-dashboard.png) |
| 03-quick-adjust | ![03-quick-adjust](03-quick-adjust.png) |
| 04-zone-heating | ![04-zone-heating](04-zone-heating.png) |
| 05-zone-setting | ![05-zone-setting](05-zone-setting.png) |
| 06-zone-frost | ![06-zone-frost](06-zone-frost.png) |
| 07-zone-offline | ![07-zone-offline](07-zone-offline.png) |
| 08-zone-floor-hold | ![08-zone-floor-hold](08-zone-floor-hold.png) |
| 09-zone-fault | ![09-zone-fault](09-zone-fault.png) |
| 10-zone-hold | ![10-zone-hold](10-zone-hold.png) |
| 11-dashboard-toast | ![11-dashboard-toast](11-dashboard-toast.png) |
| 12-hot-water | ![12-hot-water](12-hot-water.png) |
| 13-hot-water-boost | ![13-hot-water-boost](13-hot-water-boost.png) |
| 14-schedule | ![14-schedule](14-schedule.png) |
| 15-schedule-edit | ![15-schedule-edit](15-schedule-edit.png) |
| 16-hot-water-timer | ![16-hot-water-timer](16-hot-water-timer.png) |
| 17-rename | ![17-rename](17-rename.png) |
| 18-settings | ![18-settings](18-settings.png) |
| 19-settings-timezone | ![19-settings-timezone](19-settings-timezone.png) |
| 20-zones-scanning | ![20-zones-scanning](20-zones-scanning.png) |
| 21-zones | ![21-zones](21-zones.png) |
| 22-bus-diagnostics | ![22-bus-diagnostics](22-bus-diagnostics.png) |
| 22b-bus-diagnostics-scrolled | ![22b-bus-diagnostics-scrolled](22b-bus-diagnostics-scrolled.png) |
| 23-system | ![23-system](23-system.png) |
| 23b-system-update | ![23b-system-update](23b-system-update.png) |
| 23c-system-install | ![23c-system-install](23c-system-install.png) |
| 23d-system-updating | ![23d-system-updating](23d-system-updating.png) |
| 25-matter-pairing | ![25-matter-pairing](25-matter-pairing.png) |
| 26-matter-paired | ![26-matter-paired](26-matter-paired.png) |
| 27-pin-entry | ![27-pin-entry](27-pin-entry.png) |
| 28-settings-pin-on | ![28-settings-pin-on](28-settings-pin-on.png) |
