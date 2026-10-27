Artilody Watts Trainer is a free, open-source (GPL-3.0) desktop app for structured, power-based indoor cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride and row locally on your computer.

## What's new in 1.3.0

A feature release: plan Free Rides and Video Rides in the calendar, and ride a watts workout during a Video Ride.

- **Free Ride and Video Ride in the calendar**: the calendar's **Add** dialog has three tabs – **Workout**, **Free Ride** and **Video Ride**.
  - **Free Ride** options: a planned duration (30/45/60/90/120 min, any value, or *Open*) and **Ride** or **ERG** with the watts.
  - **Video Ride** options: a route from your video library (distance, climbing, video length, country), **Simulation** with a difficulty or **ERG** with the watts, and optionally a cycling workout to ride during the video, with or without Workout ERG.
  - Each kind has its own colour and icon (Free Ride green, Video purple, cycling workouts blue, rowing cyan) and a tooltip with the details. Edit (pencil or *Edit…*), drag between days, *Mark as completed*, *Move to…* and *Remove* work as for workouts. A route that is no longer in the video library shows **Route not found**.
- **Start from the calendar**: **Ride** (or a double-click, or *Start Free Ride* / *Start Video Ride* in the right-click menu) opens the Free Ride page with the mode, ERG watts and planned duration, or the Video Ride page with the route, resistance mode, difficulty/watts and workout loaded.
- **Completion rules**: a planned Free Ride is ticked off by a saved Free Ride of at least half the planned time (at least 10 min for an *Open* ride); a planned Video Ride by a saved ride on that route that reached the finish or 90 % of the route distance. Discarded rides don't count, and deleting the ride in History reopens the entry.
- **Ride a watts workout during a Video Ride**: the new **Workout** card on the Video Ride page lists your cycling workouts. The workout's intervals run on the ride's clock (pausing with the ride) and are shown as a **workout strip** above the route's elevation profile – blocks in % FTP coloured by power zone, ramps as slopes, a cursor at the elapsed time – with the current interval, target, time left, next target, workout time and your *in target* share (±10 %). In small windows workout and route share the bottom card as **Route | Workout** tabs; the Power tile shows the target and turns green/yellow/orange with your deviation.
  - **Workout ERG** (on by default): the trainer holds the workout's watts and the route only sets the video speed. **Intensity** ±5 % (buttons, ↑/↓, PgUp/PgDn or the Zwift Click), **Skip Interval** and **End Workout** (the ride goes on). With Workout ERG off the targets are a guide and the Resistance card applies.
  - When the workout ends before the route, you continue in Simulation or ERG (as set on the Resistance card); when the route ends first, the ride finishes and is saved. The saved ride records the workout's targets.
- **Free Ride planned duration**: a Free Ride started from the calendar shows *Planned 60 min • 23:10 left* in the Time tile – an informational target; the ride doesn't stop by itself.
- **Database upgrade to schema version 5** happens automatically on first start (Free Ride / Video Ride plan entries). All rides, workouts, profiles, plans and settings are preserved.
- Updated user manual (planning Free Rides and Video Rides, riding a workout during a Video Ride) and screenshots.

## Includes everything from 1.2.0

- **Free Ride page** – ride a road with a grade (shift on your bike or with virtual gears) or hold an ERG wattage; save to History with FIT/CSV export.
- **Zwift Click support and virtual gears** – Click v1 / v2 under *Devices → Controllers*, a *Virtual gears* card with **Gear Setup…** (chainrings, cassette, wheel, sequential or front + rear shifting), saved per profile and shared by Free Ride and Video Ride.
- **Real Life Video** – ride filmed routes (video + GPS) at the speed you ride, with gradient simulation, ERG and a difficulty slider; a route library; AV1 detection with HandBrake conversion help.
- **Weight-aware road physics and auto CdA**, **smooth power (3 s average)**, reordered sidebar, FTP tests that detect their protocol, database schema version 4 and many fixes.

Earlier releases (1.1.4 PM5 stroke-rate fix, 1.1.3 PM5 on Raspberry Pi / Linux, 1.1.2 Builder reordering, 1.1.1 music fix, 1.1.0 calendar, rowing, FIT export and workout sharing) are described on the [Releases page](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases).

## Upgrade notes

- Install 1.3.0 over 1.2.0, 1.1.x or 1.0.0 – no need to uninstall first.
- On first start of 1.3.0 the database is **upgraded automatically to schema version 5** (Free Ride / Video Ride plan entries; from older versions also schema 4: gear setup, rider height, ride speed). All data is preserved. If you might want to go back to an older version, make a copy of `watt-trainer.sqlite` first (the path is shown under **Settings**).
- Coming from 1.0.0: on first start the database is **migrated automatically to schema version 2** (sport type, rowing FTP, training plan, rowing records). All existing rides, workouts, profiles and settings are preserved; existing rides and workouts become *Cycling*. If you might want to go back to 1.0.0, make a copy of `watt-trainer.sqlite` first (the path is shown under **Settings**).

## Known limitations

- **The Video Ride workout strip shows no actual-power line yet** – only the target blocks and the elapsed-time cursor; your actual power is shown in the Power tile and saved with the ride.
- **Changing the route of a planned Video Ride replaces the entry** (it is removed and added again with the new route, keeping date and notes).
- **Zwift Click and gradient simulation have so far only been tested with virtual devices** (the built-in virtual trainer and a Click simulator), not yet with real hardware. Please report your experience.
- Zwift Play / Click controllers with **encrypted firmware are not supported**. On a **Click v2** the left pod may need to be unlocked in the Zwift app first.
- **AV1 videos** need AV1 hardware decoding – otherwise convert them to H.264 (720p recommended) with HandBrake, as the Video Ride page explains.
- **Real trainers use their own rider-weight setting** for gradient simulation; set your weight in the trainer's app too. The app's weight drives the virtual speed and virtual gears.
- **Concept2 PM5 support has so far only been tested with a PM5 simulator**, not yet with a real PM5. Please report your experience. The PM5 has **no ERG mode**: watt plans are followed by feel, the app can't set the resistance.
- Only the **PM5 on a rower** is supported – **BikeErg and SkiErg are not supported**, nor PM3/PM4 monitors.
- **`.zwo` files have no rowing**: rowing workouts are exported as cycling workouts, and imported `.zwo` files are always cycling. Use `.awt` to share rowing workouts.
- **macOS**: the app is ad-hoc signed, not notarized. The first time, **right-click (Control-click) the app → Open → Open** (or run `xattr -cr "/Applications/Artilody Watts Trainer.app"`).
- **Windows**: the app is not code-signed; SmartScreen may ask you to click **More info → Run anyway**.
- ANT+ is not supported (Bluetooth LE only).

## Highlights

- **Smart trainer control**: connects to Bluetooth LE smart trainers that use the standard Fitness Machine Service (FTMS) and sets the target power for you in ERG mode.
- **Free Ride, virtual gears and Real Life Video**: ride a road with a grade or a filmed route – optionally with a watts workout and Workout ERG – shift virtual gears with a Zwift Click, or hold an ERG wattage.
- **Concept2 rowing**: connects to a Concept2 PM5 for free rows, watt-plan rowing workouts and ghost races against your personal best.
- **Training calendar**: plan workouts, rows, Free Rides and Video Rides by day and start them from the calendar.
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
