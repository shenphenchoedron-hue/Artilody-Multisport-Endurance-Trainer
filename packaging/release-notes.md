Artilody Watts Trainer is a free, open-source (GPL-3.0) desktop app for structured, power-based indoor cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride and row locally on your computer.

## What's new in 1.3.7

A small follow-up for **virtual shifting with the Zwift trainer protocol** (Jetblack Victory).

- **Gentler shifting up**: a virtual shift to a harder gear now ramps in over **3 s** (was 1.5 s). Shifting up at the same cadence makes the trainer accelerate the virtual bike and rider to the higher speed; over about a second that gave a power spike of +80 … 110 W right after the shift. The extra power is roughly that energy divided by the ramp time, so the spike is about halved. Shifting easier still acts at once.

## Also included from 1.3.6

A follow-up test release for **virtual shifting with the Zwift trainer protocol** on the **Jetblack Victory**: the start was fine in 1.3.5, but shifting up still gave a heavy jump before the resistance settled.

- **Smoother shifting up**: a virtual shift to a harder gear now ramps in over 1.5 s instead of at once – the trainer's inertia emulation turned a sudden +9 % into a power spike. Shifting easier still acts at once.
- **No second, delayed step after a shift**: the bike's real gear on the trainer is now taken as the median of the ride's measurements and only changes beyond 6 %. The Victory's coarse, lagging cadence made single measurements scatter (3.4 … 4.4 for a chain on 44×11), which moved the compensation a few seconds after a shift. A real chain shift is still picked up after three measurements, and nothing is measured for 3 s after a gear change.

## Also included from 1.3.5

A test release for the **Zwift trainer protocol with virtual shifting** (Zwift Click), made for the **Jetblack Victory**: with the bike in a hard real gear (e.g. 44×11) every virtual gear rode ~1.6× too hard – 400 W to get going, ~350 W in gear 12 at 80 rpm.

- **Virtual gears adapted to the real gear on the trainer**: the app measures the bike's real gear from the trainer's speed and cadence and sends each virtual gear × 2.43 / real gear (gear 12 in 44×11 ≈ 1.46). The 2.43 (34×14 on a 14T Zwift Cog) is a **model assumption** fitted to the Victory's logs, not a published Zwift spec.
- **Safeguards**: measured only while actively pedaling (≥ 20 W, never while freewheeling), from one coherent update per trainer packet; whether the trainer reports its flywheel speed is tested on gear changes; a first value is provisional (never saved, dropped after 20 s without verification); the result is saved **per trainer** (*Bluetooth diagnostics → Real gear on the trainer → Measure again* resets it); a power check switches the compensation off if the trainer calibrates the real gear itself.
- **Light start on the flat**: on the Zwift protocol path the soft start now softens the **gear** (× 0.5, gear 12 ≈ gear 5) until the flywheel turns and while the real gear is unknown; every gear change of this kind ramps over 4 s.
- **Soft start fix**: 0 rpm and 0 W count as freewheeling – a flywheel still spinning above 10 km/h no longer ends the waiting (it used to cycle every ~5 s during a coast).
- **Diagnostic log**: `GEAR` and `RES` lines show the measured real gear, its verification and the gear ratios sent.

## Also included from 1.3.4

A fix release for the **Zwift trainer protocol** (trainers with the Zwift trainer service, e.g. **Jetblack Victory, Zwift Hub, Wahoo KICKR Core with Zwift Cog**).

- **The Zwift trainer protocol now follows Zwift's message sequence**: after every incline update the app re-sends the current virtual gear ratio and then requests the trainer's data – the same order as Zwift. Before, the start gear went out only once, before the first incline, and was ignored by some trainers, so they rode without the virtual gear until the first shift. **This should fix too-hard resistance with Zwift-capable trainers like the Jetblack Victory.**
- **No FTMS commands while the Zwift protocol drives the trainer**: no FTMS start, neutral state or ERG during the handshake, at pauses or as the start state – only the FTMS control request (like Incyclist). If the app has to fall back to FTMS, the held-back FTMS start is sent first.
- **Zwift's physics constants** on the Zwift protocol path: air drag (CWa) **0.51**, rolling resistance **0.004**, no wind; an incline of 0 % is sent as 0.
- **Corrected diagnostics decoding** in the diagnostic log: riding-data speed (m/s × 100), the 4th field is **power** (not heart rate), the trainer's incline echo in whole percent, rider-weight label.
- Updated user manual and README.

## Also included from 1.3.3

- **ERG test and gradient test stop automatically** (leaving the Devices page, session start, ride resistance command, 5-minute safety timeout).
- **Neutral trainer state after control is granted** – FTMS simulation 0 % (or a low resistance level), so an ERG target or grade left behind by another app no longer lingers.
- **Soft start** – 0 % until the flywheel turns (≥ 40 rpm or ≥ 10 km/h for 2 s), then a 4 s ramp to the route's grade; never for ERG.

## Also included from 1.3.2

- **Virtual shifting = Zwift Click, nothing to set up**: with a Click connected you shift **Zwift's fixed 24 virtual gears** (ratio 0.75 … 5.49); without a Click the trainer gets the normal FTMS gradient and you shift your bike's own gears. A status line shows the active mode.
- **Zwift trainer protocol, automatically** on trainers with the Zwift trainer service (e.g. Jetblack Victory, Zwift Hub, Wahoo KICKR Core with Zwift Cog); other trainers use FTMS gradient virtual gears – **never ERG** as a fallback.
- **Automatic diagnostic log files** (Settings → Diagnostic log → Open logs folder / Export logs…; on a Raspberry Pi in `~/.local/share/Artilody/Watt Trainer/logs`).
- **FTMS hardening**: rejected / unanswered commands are retried and shown, control is re-requested after *control permission lost*, full GATT listing and a Gradient test in Bluetooth Diagnostics, 2-byte FTMS advertisements accepted, forward-looking route grade in Video Ride.

## Also included from 1.3.1

- **Manual start only** – sessions never start or resume by pedaling or rowing; press **Start** (or Space).
- **Dashboard renamed to "Watt Training"**; **Start is never blocked without an explanation** – a yellow card names the session in the way with **Go to …**, **End & Save** and **Discard**.
- Video Ride controls stay usable while another session is paused; the video library scan can't hang.

## Also included from 1.3.0

- **Free Ride and Video Ride in the calendar** – plan them with duration, Ride/ERG or route, Simulation/ERG and an optional workout; start them from the calendar with everything loaded; automatic tick-off.
- **Ride a watts workout during a Video Ride** – workout strip above the elevation profile, **Workout ERG**, intensity ±5 %, skip interval, end workout.
- **Free Ride planned duration** shown in the Time tile; database schema version 5.

## Also included from 1.2.0

- **Free Ride page** – ride a road with a grade (shift on your bike or with virtual gears) or hold an ERG wattage; save to History with FIT/CSV export.
- **Zwift Click support and virtual gears** – Click v1 / v2 under *Devices → Controllers*, shared by Free Ride and Video Ride (the 1.2.0 Gear Setup is replaced by Zwift's 24 gears in 1.3.2).
- **Real Life Video** – ride filmed routes (video + GPS) at the speed you ride, with gradient simulation, ERG and a difficulty slider; a route library; AV1 detection with HandBrake conversion help.
- **Weight-aware road physics and auto CdA**, **smooth power (3 s average)**, reordered sidebar, FTP tests that detect their protocol, database schema version 4 and many fixes.

Earlier releases (1.1.4 PM5 stroke-rate fix, 1.1.3 PM5 on Raspberry Pi / Linux, 1.1.2 Builder reordering, 1.1.1 music fix, 1.1.0 calendar, rowing, FIT export and workout sharing) are described on the [Releases page](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases).

## Upgrade notes

- Install 1.3.4 over 1.3.3, 1.3.2, 1.3.1, 1.3.0, 1.2.0, 1.1.x or 1.0.0 – no need to uninstall first.
- **No database change from 1.3.0 – 1.3.3** (schema version 5). Coming from 1.2.0 or older, the database is **upgraded automatically to schema version 5** on first start (Free Ride / Video Ride plan entries; from older versions also schema 4: gear setup, rider height, ride speed). All data is preserved. If you might want to go back to an older version, make a copy of `watt-trainer.sqlite` first (the path is shown under **Settings**).
- Coming from 1.0.0: on first start the database is **migrated automatically to schema version 2** (sport type, rowing FTP, training plan, rowing records). All existing rides, workouts, profiles and settings are preserved; existing rides and workouts become *Cycling*. If you might want to go back to 1.0.0, make a copy of `watt-trainer.sqlite` first (the path is shown under **Settings**).
- Your chainring / cassette Gear Setup from 1.2.0–1.3.1 stays in the database but is no longer used: the Zwift Click now always shifts Zwift's 24 gears.
- If you used *Start When Pedaling* or *Start on first stroke* (before 1.3.1): press **Start** (or Space) after you are clipped in / seated.

## Known limitations

- **The Video Ride workout strip shows no actual-power line yet** – only the target blocks and the elapsed-time cursor; your actual power is shown in the Power tile and saved with the ride.
- **Changing the route of a planned Video Ride replaces the entry** (it is removed and added again with the new route, keeping date and notes).
- **The neutral start state and the soft start have so far only been tested with the virtual trainer and in unit tests**, not yet on every real trainer. Some trainers ignore a 0 % grade while standing still; that is harmless. Please send the diagnostic log if a trainer behaves oddly at the start of a ride.
- **The 1.3.4 Zwift trainer protocol changes have so far been verified against reference captures, unit tests and the simulated trainer**, not yet in a full ride on a real Zwift Hub, Jetblack Victory or Zwift Cog trainer. Feedback is very welcome – please attach the diagnostic log file (Settings → Diagnostic log → Export logs…) to your report.
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
- **Live Watt Training page**: power, heart rate, cadence, the current interval and the time left, power zones, and a workout chart that updates as you ride.
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
