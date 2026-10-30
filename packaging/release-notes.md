Artilody Watts Trainer is a free, open-source (GPL-3.0) desktop app for structured, power-based indoor cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride and row locally on your computer.

## What's new in 1.4.0

**GPX Ride** – ride any GPX route in a 3D world (sidebar → **GPX Ride**, under Video Ride).

- **Any GPX track**: from a bike computer, Strava, Komoot or a route planner – no video needed. Every `.gpx` file in the GPX folder (`Documents/Artilody GPX Routes`, and its subfolders) is listed with name, distance and climbing; **Open GPX File…** rides a single file. More folders can be added like in the video library.
- **3D view**: the route is drawn as a road through a generated landscape with hills, fields, houses, trees, sky and a rider on a bike, seen from just behind the rider. The view follows your position and speed from the app's own physics – the same road model, trainer control and recording as the Video Ride – so the 3D road, the gradient sent to the trainer and the elevation profile always agree.
- **Same page as the Video Ride**: power, cadence, heart rate, speed and time at the top, the route's elevation profile at the bottom (click it to start part-way), and on the right Simulation / ERG, difficulty, a workout with Workout ERG, virtual gears and the Zwift Click. GPX Ride has its own settings (mode, difficulty, last route).
- **History**: GPX rides are saved as *GPX: <route>* and can be exported as FIT/CSV like any ride.
- **Graphics**: the app now draws with OpenGL on all platforms (needed for the 3D view; before, macOS used Metal and Windows Direct3D). On a computer without OpenGL 3.1 (OpenGL ES 3.0 on a Raspberry Pi) the app keeps its previous graphics and the GPX Ride page says that the 3D view is not available – the route can still be ridden with the profile.
- No database change – install over 1.3.10 (or older) and keep all your data.

## Also included from 1.3.10

Safer switching between **ERG** and **virtual shifting with the Zwift trainer protocol** (Zwift Hub, Jetblack Victory).

- **ERG ends the Zwift protocol cleanly**: choosing ERG (Free Ride ERG, a video ride's ERG, a workout, the ERG test) or the FTMS gradient while the Zwift protocol starts or runs now ends the hub session first. The trainer no longer keeps getting the old road and gear every few seconds after the ERG target, and a Zwift Hub that had taken over FTMS control is asked for it again so the ERG target is applied.
- **A Zwift Click alone never takes the trainer out of ERG**: the Zwift protocol starts again only when a ride in Ride / Simulation asks for the hub – not because a Click connects or reconnects. A restarted hub only gets the values of the ride that asked for it.
- **No resistance hop from ERG to the hub**: the first hub gear is the one at which the trainer's own physics – with exactly the values sent to it (incline, CWa 0.51, Crr 0.004, rider + bike, no wind) – brake with the power you push now; it stays there during the handshake and then ramps over 4 s in 10 steps to your gear. This match is used only with fresh data (power and speed within 2 s) from a trainer whose speed is verified as its flywheel's, at ≥ 30 W, ≥ 7 km/h, not on descents and only when the result is well determined. Otherwise – unknown speed type, old data, 0 W, standstill, descents – the hub starts from the light gear and ramps up the same way.
- **Zwift Hub gets road and gear at once**: when it answers the start of the data stream without riding data (flywheel standing), the app makes it active immediately instead of waiting 4 s with the trainer on its own default resistance.

## Also included from 1.3.9

Fixes for **virtual shifting with the Zwift trainer protocol** (Zwift Hub, Jetblack Victory) and the **Zwift Click**, plus a **start gear for your FTP**.

- **Zwift Hub: the Zwift protocol now starts**: the Hub switches to the Zwift protocol and reports *control permission lost* on FTMS. The app asked for FTMS control again, which switched the Hub back – no riding data, and after 4 s the app fell back to the FTMS gradient, which felt heavy. Now FTMS control is left to the Zwift protocol while it starts or runs. A trainer that answers the handshake but sends no riding data while its flywheel stands still is waited for (gear and road are sent); the app falls back to FTMS only if the trainer moves for 8 s without sending riding data.
- **Start gear from your FTP**: the hardest of Zwift's gears whose flat-road power at 85 rpm is at most 60 % of the profile's FTP, between gear 6 and 12 – never harder than Zwift's start gear 12 (FTP 75 → gear 9, FTP 110 → gear 10, FTP ≥ ~155 or unknown → 12). Set when the app starts and when you switch rider; once you shift, your gear carries over from ride to ride.
- **Light pedaling counts as pedaling**: cadence ≥ 15 rpm or ≥ 30 W – the Victory reads 0 W when you spin lightly (36–107 rpm). The speed then follows the trainer's flywheel for your gear instead of sinking as if you were coasting; downhill it never brakes the bike.
- **No speed drop at the soft start**: the coasting speed is now based on your virtual gear, not on the light gear the soft start sends (it dropped from 18 to 13.5 km/h in one step).
- **Gentle stop**: when the flywheel stops, the speed brakes to 0 within about 2 s instead of jumping to 0 (also downhill in a video ride).
- **Zwift Click**: after a rider switch and at the start of a free ride or video ride the app looks for a known Click that is not connected (about 5 minutes) and tells you to press a button on it to wake it. The diagnostic log says why a found Click does not connect by itself, and the "not known to the Bluetooth stack" hint now fits the device (no longer "PM5: Connect screen" for a Click).

## Also included from 1.3.8

A fix for the **speed shown while coasting** with the **Zwift trainer protocol** (Jetblack Victory, virtual shifting with a Zwift Click).

- **The speed no longer rolls on after the flywheel has stopped**: before, the app's road model let a 77 kg bike roll on for 10–15 s after the trainer's flywheel had stopped. Now, while coasting (not pedaling while the flywheel still turns), the shown speed on the flat and uphill follows the trainer's own simulated speed down – smoothly, without a jump. Downhill the bike and the video still roll on.
- **Flywheel stopped = stopped**: as soon as the flywheel stands still, the speed is 0 on any grade and the ride is no longer shown as coasting.
- "Pedaling" means at least 30 W – the trainer's cadence lags both ways (0 rpm while you already push 180 W at the start, still 83 rpm at 0 W when you stop pedaling), the power does not.

## Also included from 1.3.7

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

- **GPX Ride's 3D view** supports routes up to 200 km with track points at most 2 km apart; it has so far been tested on macOS only (not yet on Windows or a Raspberry Pi). On a Pi, prefer a Pi 4 or 5.
- **GPX Rides cannot be planned in the calendar yet.**

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
- **GPX Ride in 3D**: ride any GPX route through a 3D landscape with the same physics and trainer control as a video ride.
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
