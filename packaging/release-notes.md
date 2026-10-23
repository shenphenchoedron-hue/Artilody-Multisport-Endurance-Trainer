Artilody Watts Trainer is a free, open-source (GPL-3.0) desktop app for structured, power-based indoor cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride and row locally on your computer.

## What's new in 1.1.0

- **Workout calendar**: the new **Calendar** page shows your training plan as a month view. A single click selects a day or workout; double-click a day, click **+** or press **Add Workout** to add a workout (search, Cycling/Rowing filter, optional notes). Drag workouts between days, and right-click or press and hold a workout for *Ride/Row this workout*, *Mark as completed*, *Move to…*, *Move to next day* and *Remove* (with confirmation). Double-click a planned workout or press **Ride**/**Row** in the side panel to start it – cycling workouts on the Dashboard, rowing workouts as a watt plan on the Rowing page. A side panel shows the selected day and your upcoming workouts. When you complete a workout started from the calendar, that plan entry is ticked off (on any day); stopped or aborted rides don't count, and deleting a ride reopens its entry. Workouts can also be planned from the Workouts page (**Add to calendar…**).
- **Rowing with a Concept2 PM5** over Bluetooth LE: connect the PM5 under **Devices → Rowers (Concept2 PM5)** (on the PM5: *More Options → Turn Wireless ON → Connect Device*), or try it with the **Virtual Rower**. The new **Rowing** page has three modes:
  - **Free Row**: row a distance (or open-ended) and watch your boat move along the course.
  - **Watt Plan**: row a rowing workout with interval blocks, a rower marker along the plan, target vs. actual watts, interval time left, next target and a compliance score. The PM5 has no ERG mode, so you follow the target yourself.
  - **Ghost Race**: race your personal best over 500 m, 1k, 2k, 5k, 6k, 10k or a custom distance against a ghost boat, with the gap in metres and seconds. New personal bests are saved automatically (not for paused pieces or implausible times) and listed in a personal-bests table.
  
  Rows start on the first stroke (or **Start now**) and can be paused, finished and reset. They are saved to History as *Rowing* with distance and can be exported as FIT (as an indoor-rowing activity with distance) and CSV. A dedicated heart-rate strap takes priority over heart rate from the PM5. Only the PM5 on a rower is supported (not BikeErg or SkiErg).
- **Sport type for workouts**: workouts are *Cycling* or *Rowing*. The Workouts page shows sport badges and a sport filter and has a **Row Workout** button for rowing workouts; the Builder has a Cycling/Rowing selector (rowing uses stroke rate in SPM). History has a sport filter and shows the distance of rows. `.awt` files keep the sport; `.zwo` files are always cycling.
- **Rowing FTP**: every profile has a separate rowing FTP; rowing watt plans are a percentage of it.
- The database is upgraded automatically to schema version 2 on first start; all existing rides, workouts and settings are kept.
- **FIT export for Garmin Connect, Strava, TrainingPeaks, intervals.icu …**: in **History → Export**, save the selected ride as a `.fit` activity file, or export every ride of the selected period as one FIT file per ride. The ride details dialog has an **Export FIT** button too. The files contain 1-second power, heart rate and cadence and a session summary (avg/max power and HR, cadence, NP, TSS, IF, threshold power, work and calories). Indoor rides contain no GPS or distance. The FIT encoder was written independently for this project; FIT is a format by Garmin, and this project is not affiliated with Garmin.
- **Share workouts**: export any workout (**⋯ → Export…** in the workout preview) as an Artilody `.awt` file or as a Zwift `.zwo` file, or export all your own workouts in one `.awt` file. The new **Import** button on the Workouts page imports `.awt` and `.zwo` files (several at once). Intensities are stored relative to FTP, so shared workouts fit every rider. Duplicates are skipped and name clashes are imported as "… (imported)".
- The CSV export is now in the same **Export** menu on the History page.
- Updated user manual (workout calendar, rowing with a Concept2 PM5, sport types, FIT export, sharing workouts, the `.awt` file format).

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
| User manual (PDF) | `Artilody-Watts-Trainer-Manual.pdf` (if attached) |

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
