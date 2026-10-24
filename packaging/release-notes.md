Artilody Watts Trainer is a free, open-source (GPL-3.0) desktop app for structured, power-based indoor cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride and row locally on your computer.

## What's new in 1.1.4

A Concept2 PM5 fix release on top of 1.1.3 – the **stroke rate is shown again** when power, pace and distance already worked (reported on a Raspberry Pi with BlueZ):

- **Root cause**: the PM5 sends the stroke rate only in *Additional Status 1* (0x0032, byte 5). When that channel stayed silent (a rejected/lost notification subscription on BlueZ) or arrived on the multiplexed channel 0x0080 shorter than the documented 19 bytes, the whole packet was dropped – and nothing noticed, because pace is also derived from power and distance/time/power come from 0x0031/0x0036.
- **Fixes**: shortened 0x0032 packets (17/18 bytes, direct or multiplexed) are decoded; failed notification subscriptions are detected and the multiplexed channel is used instead; if data flows but 0x0032 stays silent for 5 s, its notifications are enabled again and then moved to the multiplexed channel. The FTMS rower path (split *Rower Data* records, stroke rate only when the *More Data* bit is 0, 0.5 SPM units) keeps the last stroke rate across packets without it.
- **Stroke rate fallback**: if a rowing machine reports no stroke rate for more than ~6 s while strokes come in, the stroke rate is computed from the time between the last strokes (smoothed over up to 4 strokes, 60 / mean interval, 5–80 SPM, missed stroke packets accounted for), decays when you slow down and drops to 0 after 6 s without a stroke. The monitor's own value is preferred whenever it reports one. Live display, average stroke rate and the saved cadence samples all use it.
- **Richer Bluetooth diagnostics**: a *Rower data* row on the Devices page shows the active data source (Concept2 rowing service with the direct channels in use, 0x0080 multiplex or FTMS Rower Data) and the stroke rate (≈ when computed). *Details…* adds a *[Rower data]* section: subscription state, packet counter, undecoded packets and the last raw payload (hex) per characteristic (0x31/0x32/0x33/0x36/0x80 incl. each multiplexed id, 0x2AD1), how *Additional Status 1* arrives, the last decoded stroke rate and where the shown stroke rate comes from. Please copy it into a bug report if anything is still wrong.

## Includes everything from 1.1.3

A Bluetooth release on top of 1.1.2 – the **Concept2 PM5 is now found and connected on Raspberry Pi / Linux** (BlueZ):

- **PM5 detected from its FTMS advertisement**: current PM5 firmware advertises the standard Bluetooth fitness-machine service (FTMS, 0x1826) with the machine type *Rower* rather than only the Concept2 services. The app reads the FTMS advertisement data and lists the PM5 under **Rowers** – also when its name arrives late or not at all, as often happens with BlueZ. Before, such a PM5 could end up under *Smart Trainers* or not be listed.
- **Automatic trainer → rower switch**: a device connected as trainer that offers rowing data (Concept2 rowing service, or FTMS Rower Data without Indoor Bike Data) is moved to **Rowers**, connected with the rower driver and remembered. Every trainer entry also has a **Connect as rower** button.
- **FTMS rower support**: if the Concept2 rowing service (CE060030) is missing, the standard **FTMS Rower Data** (stroke rate and count, distance, pace, power, calories, heart rate, elapsed time; multi-packet records are merged) is used instead. This also makes other FTMS rowing machines work.
- **More robust PM5 connection**: notifications are enabled only after the service details are known plus a short settle time (as Concept2 recommends), missing characteristics are tolerated, service discovery that hangs is retried, and longer packets from newer firmware (average power in the 19-byte *Additional Status 1*, work per stroke) are parsed.
- **More robust discovery on BlueZ**: scans run for 60 s, devices whose name or services arrive later are re-classified, the scan keeps running while connecting, Bluetooth that is off is switched on, and scan errors are shown in red with a hint.
- **Bluetooth diagnostics**: *Bluetooth Diagnostics → Details…* opens a report you can **Copy** – adapter, Qt version, every device seen with signal, service UUIDs, service data, parsed FTMS machine type and classification, the GATT services/characteristics found after each connection attempt, scan errors and the log. Start the app with `QT_LOGGING_RULES="qt.bluetooth*=true"` for a detailed terminal log.
- **Show all devices / Connect as rower**: a switch on the Devices page lists every unrecognised Bluetooth LE device under *Other Bluetooth devices*, with a **Connect as rower** button.
- **macOS launch hardening**: the app no longer fails to start when a `QT_QPA_PLATFORM` value (e.g. from a development shell) is inherited whose platform plugin is not in the app bundle.
- Raspberry Pi / Debian / Ubuntu packages now list `libqt6bluetooth6`, `bluez` and `dbus` explicitly; updated user manual (section *Rowing machine not found* with Raspberry Pi commands).

## Includes everything from 1.1.2

A small usability release on top of 1.1.1 – reordering and deleting intervals in the **Workout Builder** is now obvious:

- **Grip strip for dragging**: every block card has a ⋮⋮ grip strip at the top (with a tooltip and an open-hand cursor). While you drag, the moved card is dimmed, a copy follows the pointer, a **blue drop indicator** shows exactly where the block will land, and the block list **scrolls automatically** near its edges.
- **‹ / › move buttons** on each card move a block one place earlier or later (greyed out on the first and last block).
- **Right-click menu** on a card: *Move earlier*, *Move later*, *Move to start*, *Move to end*, *Duplicate* and *Delete interval*.
- **Keyboard**: <kbd>Alt</kbd>+<kbd>←</kbd>/<kbd>→</kbd> (<kbd>⌥</kbd>+<kbd>←</kbd>/<kbd>→</kbd> on a Mac) moves the selected block; it stays selected so you can keep pressing.
- **Clear delete button**: the trash button is red and has a tooltip; every button on the card has a tooltip, and a hint line above the block list explains how to reorder and delete.
- **Fix**: dropping a block in the gap between two blocks no longer sends it to the end of the workout.
- Updated user manual (new section *Reordering and deleting blocks*) and Builder screenshot.

## Includes everything from 1.1.1

- **Music playlist stays inside its frame**: the *Up Next* list on the Dashboard music card now always stays inside the card: when the card is small (e.g. in a smaller window) the list is clipped at the card frame and scrolls, instead of drawing past the bottom of the card.
- **Music Library**: the track and playlist lists have scrollbars, playlist names use the available width, and long titles are shortened with "…" instead of overlapping.

## Includes everything from 1.1.0

- **Workout calendar**: the new **Calendar** page shows your training plan as a month view. A single click selects a day or workout; double-click a day, click **+** or press **Add Workout** to add a workout (search, Cycling/Rowing filter, optional notes). Drag workouts between days, and right-click or press and hold a workout for *Ride/Row this workout*, *Mark as completed*, *Move to…*, *Move to next day* and *Remove* (with confirmation). Double-click a planned workout or press **Ride**/**Row** in the side panel to start it – cycling workouts on the Dashboard, rowing workouts as a watt plan on the Rowing page. A side panel shows the selected day and your upcoming workouts. When you complete a workout started from the calendar, that plan entry is ticked off (on any day); stopped or aborted rides don't count, and deleting a ride reopens its entry. Workouts can also be planned from the Workouts page (**Add to calendar…**).
- **Rowing with a Concept2 PM5** over Bluetooth LE: connect the PM5 under **Devices → Rowers (Concept2 PM5)** (on the PM5: *More Options → Turn Wireless ON → Connect Device*), or try it with the **Virtual Rower**. The new **Rowing** page has three modes:
  - **Free Row**: row a distance (or open-ended) and watch your boat move along the course.
  - **Watt Plan**: row a rowing workout with interval blocks, a rower marker along the plan, target vs. actual watts, interval time left, next target and a compliance score. The PM5 has no ERG mode, so you follow the target yourself.
  - **Ghost Race**: race your personal best over 500 m, 1k, 2k, 5k, 6k, 10k or a custom distance against a ghost boat, with the gap in metres and seconds. New personal bests are saved automatically (not for paused pieces or implausible times) and listed in a personal-bests table.
  
  Rows start on the first stroke (or **Start now**) and can be paused, finished and reset. They are saved to History as *Rowing* with distance and can be exported as FIT (as an indoor-rowing activity with distance) and CSV. A dedicated heart-rate strap takes priority over heart rate from the PM5. Only the PM5 on a rower is supported (not BikeErg or SkiErg).
- **Sport type for workouts**: workouts are *Cycling* or *Rowing*. The Workouts page shows sport badges and a sport filter and has a **Row Workout** button for rowing workouts; the Builder has a Cycling/Rowing selector (rowing uses stroke rate in SPM). History has a sport filter and shows the distance of rows. `.awt` files keep the sport; `.zwo` files are always cycling.
- **Rowing FTP**: every profile has a separate rowing FTP; rowing watt plans are a percentage of it.
- **FIT export for Garmin Connect, Strava, TrainingPeaks, intervals.icu …**: in **History → Export**, save the selected ride as a `.fit` activity file, or export every ride of the selected period as one FIT file per ride. The ride details dialog has an **Export FIT** button too. The files contain 1-second power, heart rate and cadence and a session summary (avg/max power and HR, cadence, NP, TSS, IF, threshold power, work and calories). Indoor rides contain no GPS or distance. The FIT encoder was written independently for this project; FIT is a format by Garmin, and this project is not affiliated with Garmin.
- **Share workouts**: export any workout (**⋯ → Export…** in the workout preview) as an Artilody `.awt` file or as a Zwift `.zwo` file, or export all your own workouts in one `.awt` file. The new **Import** button on the Workouts page imports `.awt` and `.zwo` files (several at once). Intensities are stored relative to FTP, so shared workouts fit every rider. Duplicates are skipped and name clashes are imported as "… (imported)".
- **New app icon**.
- **UI polish**: larger, easier-to-read menus; the CSV export now sits next to FIT in the **Export** menu on the History page; the rowing boat animation lines up exactly with the start and finish lines; <kbd>Space</kbd> also pauses/resumes a row.
- **Fixes**: light-theme contrast and colour fixes, more robust rowing sessions and PM5 handling, reliable calendar completion, and many smaller fixes from code review.
- Updated user manual (workout calendar, rowing with a Concept2 PM5, sport types, FIT export, sharing workouts, the `.awt` file format, checksums).

## Upgrade notes

- Install 1.1.4 over 1.1.3, 1.1.2, 1.1.1, 1.1.0 or 1.0.0 – no need to uninstall first.
- Coming from 1.0.0: on first start the database is **migrated automatically to schema version 2** (sport type, rowing FTP, training plan, rowing records). All existing rides, workouts, profiles and settings are preserved; existing rides and workouts become *Cycling*. If you might want to go back to 1.0.0, make a copy of `watt-trainer.sqlite` first (the path is shown under **Settings**).

## Known limitations

- **Concept2 PM5 support has so far only been tested with a PM5 simulator**, not yet with a real PM5. Please report your experience. The PM5 has **no ERG mode**: watt plans are followed by feel, the app can't set the resistance.
- Only the **PM5 on a rower** is supported – **BikeErg and SkiErg are not supported**, nor PM3/PM4 monitors.
- **`.zwo` files have no rowing**: rowing workouts are exported as cycling workouts, and imported `.zwo` files are always cycling. Use `.awt` to share rowing workouts.
- **macOS**: the app is ad-hoc signed, not notarized. The first time, **right-click (Control-click) the app → Open → Open** (or run `xattr -cr "/Applications/Artilody Watts Trainer.app"`).
- **Windows**: the app is not code-signed; SmartScreen may ask you to click **More info → Run anyway**.
- ANT+ is not supported (Bluetooth LE only).

## Highlights

- **Smart trainer control**: connects to Bluetooth LE smart trainers that use the standard Fitness Machine Service (FTMS) and sets the target power for you in ERG mode.
- **Concept2 rowing**: connects to a Concept2 PM5 for free rows, watt-plan rowing workouts and ghost races against your personal best.
- **Training calendar**: plan rides and rows by day and start them with a double-click.
- **Heart-rate sensors**: works with any Bluetooth LE heart-rate strap or armband (standard Heart Rate Service).
- **Workout library and builder**: ready-made workouts plus a visual drag-and-edit builder with warm-up, steady, interval, recovery, ramp and cool-down blocks. Targets can be set in watts or % of FTP, with cadence and RPE. Workouts can be shared as `.awt` or Zwift `.zwo` files.
- **Live dashboard**: power, heart rate, cadence, the current interval and the time left, power zones, and a workout chart that updates as you ride.
- **FTP test and profiles**: run the built-in FTP test, keep several rider profiles, and have training zones calculated automatically.
- **History and analysis**: every ride is saved to a local SQLite database. You get average/max/normalized power, TSS, time in zones and charts, and you can export rides as FIT (Strava, Garmin Connect, TrainingPeaks, …) or CSV.
- **Music while you ride**: plays your own music library and playlists. Two sample tracks are included.
- **Private by design**: no account, no cloud, no tracking. Your data stays on your computer.
- Light and dark themes, built with Qt 6 / QML.

## Downloads

| Platform | File |
|---|---|
| **Windows 10/11 (64-bit)**, installer | `ArtilodyWattsTrainer-{{VERSION}}-windows-x64-setup.exe` |
| **Windows 10/11 (64-bit)**, portable ZIP | `ArtilodyWattsTrainer-{{VERSION}}-windows-x64.zip` |
| **macOS 12 Monterey or newer** (Apple Silicon and Intel) | `ArtilodyWattsTrainer-{{VERSION}}-macOS-universal.dmg` |
| **Ubuntu 24.04 LTS** (and derivatives, amd64) | `artilody-watts-trainer_{{VERSION}}_ubuntu24.04_amd64.deb` |
| **Debian 12 Bookworm** (amd64) | `artilody-watts-trainer_{{VERSION}}_debian12_amd64.deb` |
| **Raspberry Pi OS 64-bit (Bookworm)**: Raspberry Pi 3, 4, 5, 400 | `artilody-watts-trainer_{{VERSION}}_raspberrypi-os-bookworm_arm64.deb` |
| User manual (PDF) | `Artilody-Watts-Trainer-Manual.pdf` |
| Checksums | `SHA256SUMS.txt` (verify with `sha256sum -c SHA256SUMS.txt --ignore-missing`) |

## Installation

### Windows

1. Download `ArtilodyWattsTrainer-{{VERSION}}-windows-x64-setup.exe` and run it. It creates a Start menu entry and, if you choose, a desktop shortcut.
   If you prefer not to install anything, extract the ZIP and start `Artilody Watts Trainer.exe` from the extracted folder.
2. **SmartScreen:** the app is not code-signed yet, so Windows may show "Windows protected your PC". Click **More info → Run anyway**.
3. Bluetooth must be turned on (Settings → Bluetooth & devices). Bluetooth LE requires Windows 10 or newer.

### macOS

1. Open the DMG and drag **Artilody Watts Trainer** onto the **Applications** folder.
2. **Gatekeeper:** the app is ad-hoc signed but not notarized by Apple. The first time you open it, **right-click (or Control-click) the app → Open → Open**.
   If macOS says the app is "damaged" or still won't open it, run this once in Terminal:
   ```sh
   xattr -cr "/Applications/Artilody Watts Trainer.app"
   ```
3. **Bluetooth permission:** when macOS asks, allow Bluetooth access. If you denied it by mistake, turn it on again under System Settings → Privacy & Security → Bluetooth.

### Ubuntu 24.04 / Debian 12

Download the matching `.deb` and install it with apt. Apt also installs all required Qt 6 libraries:

```sh
sudo apt install ./artilody-watts-trainer_{{VERSION}}_ubuntu24.04_amd64.deb    # Ubuntu 24.04
sudo apt install ./artilody-watts-trainer_{{VERSION}}_debian12_amd64.deb        # Debian 12
```

Start **Artilody Watts Trainer** from the application menu, or run `artilody-watts-trainer` in a terminal.
Bluetooth needs the BlueZ service to be running (`sudo systemctl enable --now bluetooth`). Ubuntu 22.04 and older are **not** supported, because they ship Qt 6.2 and the app needs Qt 6.4 or newer.

### Raspberry Pi

You need **Raspberry Pi OS (64-bit) Bookworm** on a Raspberry Pi 3, 4, 5 or 400. The 32-bit edition is not supported. A Pi 4 or 5 is recommended for a smooth UI.

```sh
sudo apt update
sudo apt install ./artilody-watts-trainer_{{VERSION}}_raspberrypi-os-bookworm_arm64.deb
```

The built-in Bluetooth of the Pi 3/4/5 works with smart trainers and heart-rate sensors. Make sure Bluetooth is turned on in the desktop panel.

### Uninstall

- Windows: Settings → Apps → Artilody Watts Trainer → Uninstall
- macOS: move the app from Applications to the Trash
- Linux: `sudo apt remove artilody-watts-trainer`

Your rides and settings are stored separately in your user profile and are kept when you uninstall.

## Feedback

Found a bug or have an idea? Please open an issue: https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/issues
