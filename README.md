<div align="center">

<img src="docs/screenshots/icon-256.png" alt="Artilody Watts Trainer icon" width="128">

# Artilody Watts Trainer

**Free, open-source, offline power training for indoor cycling and rowing – with your Bluetooth smart trainer or Concept2 PM5.**

[![Latest release](https://img.shields.io/github/v/release/shenphenchoedron-hue/Artilody-Watts-Trainer?sort=semver)](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/latest)
[![Release build](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/actions/workflows/release.yml/badge.svg)](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/actions/workflows/release.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux%20%7C%20Raspberry%20Pi-informational)](#download--install)
[![Qt 6](https://img.shields.io/badge/Qt-6%20%2F%20QML-41CD52?logo=qt&logoColor=white)](https://www.qt.io/)

[Download](#download--install) · [User manual](docs/Artilody-Watts-Trainer-Manual.pdf) · [Quick start](#quick-start) · [Build from source](#building-from-source) · [Report a bug](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/issues)

</div>

<p align="center">
  <img src="docs/screenshots/dashboard.png" alt="Artilody Watts Trainer dashboard during a workout" width="900">
</p>

Artilody Watts Trainer is a desktop app for structured, power-based indoor cycling. It connects to Bluetooth LE smart trainers (FTMS) and heart-rate sensors, sets the trainer resistance for you in **ERG mode**, and saves every ride to a local database on your computer. It also connects to **Concept2 rowers (PM5)** for free rows, watt-plan workouts and ghost races against your personal best, and a **training calendar** lets you plan rides and rows ahead. You don't need an account, a subscription or an internet connection.

It is written in C++17 with Qt 6 / QML and runs on **Windows**, **macOS**, **Linux (Debian / Ubuntu)** and **Raspberry Pi OS**.

---

## New: Free Ride, virtual gears and Zwift Click

- **Video Ride (Real Life Video)** – ride filmed routes: pick a route from a drop-down (name, distance, climbing and duration), and the video plays at the speed you ride – faster when you push, slower on the climbs, paused when you stop. Power, cadence, heart rate, speed and time at the top, the video in the middle, the route's elevation profile with your position at the bottom. Classic **gradient simulation** (the trainer makes climbs harder, you shift on your bike), **virtual gears** or a fixed **ERG** wattage set right on the page, a difficulty slider and Zwift Click support. See [Real Life Video](#real-life-video).
- **Free Ride page** – ride without a workout. In **Gears** mode you shift virtual gears (on screen, with the keyboard or with a Zwift Click): a harder gear means more watts and more speed at the same cadence, and a grade setting adds a virtual climb. In **ERG** mode you set a wattage (±5/10/25 W steps or an exact value) and the trainer holds it for as long as you like; **Free** mode leaves the resistance alone. Live power, speed, cadence, heart rate, time and distance, a power/speed/heart-rate chart, start on pedaling, pause, and save to History (FIT/CSV export included).
- **Zwift Click v1 / v2** – connect it under **Devices → Controllers (Zwift Click)**: **+** shifts harder, **−** easier (in ERG mode ±10 W). Battery level and a live button test are shown on the Devices page.
- **Smooth power (3 s average)** – cycling watts on the Dashboard, the Free Ride page, the sidebar and the Devices page are shown as a rolling 3-second average by default, so the number no longer jumps with every uneven pedal stroke. Turn it off (raw trainer value) or pick 1 / 3 / 5 / 10 s under **Settings → Smooth power**, or click the small **3 s avg** badge on the power tile. Recorded rides, averages, NP/TSS, ERG and the FTP test still use the raw per-second power.
- **Sidebar order** – the training pages are now together at the top: **Dashboard** (cycling workouts), **Free Ride**, **Video Ride**, **Rowing**, then Workouts, Calendar, Builder, History and Devices.
- **Gear setup** – enter your own chainrings (e.g. 50/34, 53/39 or a single 1× ring) and cassette (e.g. 11–34) from presets or as numbers, the wheel size, and *sequential* virtual gears (one list, easiest to hardest) or *front + rear* shifting like a real bike. A live preview lists every gear with its ratio, the speed at 90 rpm and the power on the flat. Saved per profile.

## What's new in 1.1.4

- **Concept2 PM5: stroke rate fixed** – the stroke rate stayed empty on some PM5 connections (Raspberry Pi / BlueZ) while power, pace and distance worked: it comes only from the PM5's *Additional Status 1* (0x0032), and when that channel stayed silent or arrived shortened on the multiplexed channel, nothing noticed (pace is also derived from power). Shortened 0x0032 packets are now decoded, a silent 0x0032 is re-enabled and then moved to the multiplexed channel 0x0080, and failed notification subscriptions are detected.
- **Stroke rate fallback** – if a rowing machine reports no stroke rate for more than ~6 s while strokes come in, the stroke rate is computed from the time between the last strokes (smoothed over 4 strokes, 5–80 SPM) and falls back to 0 after 6 s without a stroke; the monitor's own value is preferred. Averages and the saved cadence samples use it too.
- **Richer Bluetooth diagnostics** – a *Rower data* row shows the active data source (Concept2 rowing service, 0x0080 multiplex or FTMS Rower Data) and stroke rate; the report adds a *[Rower data]* section with packet counters, subscription state and last raw bytes per characteristic (0x31/0x32/0x33/0x36/0x80/0x2AD1) and the last decoded stroke rate. See the [1.1.4 release notes](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/tag/v1.1.4).

## What's new in 1.1.3

- **Concept2 PM5 found on Raspberry Pi / Linux** – current PM5 firmware advertises the standard Bluetooth fitness-machine service (FTMS) as a *Rower*; the app now reads that advertisement and lists the PM5 under **Rowers** even when its name arrives late (BlueZ). A rowing machine connected as trainer is moved to Rowers automatically, and every trainer entry has a **Connect as rower** button. Discovery on BlueZ is more robust (longer 60 s scans, late names and services are picked up).
- **FTMS rower support** – if the Concept2 rowing service is missing, the standard FTMS Rower Data is used; other FTMS rowing machines work too. PM5 data from newer firmware (longer packets) is parsed leniently.
- **Bluetooth diagnostics** – *Bluetooth Diagnostics → Details…* shows a copyable report (adapter, Qt version, every device seen with services, FTMS machine type and classification, GATT services after connecting, scan errors); **Show all devices** lists unrecognised devices with **Connect as rower**.
- **macOS**: the app no longer fails to start when `QT_QPA_PLATFORM` is inherited from the environment. See the [1.1.3 release notes](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/tag/v1.1.3).

## What's new in 1.1.2

- **Easier reordering and deleting in the Workout Builder** – drag a block by the ⋮⋮ grip strip at the top of its card (a blue line shows where it lands and the list scrolls automatically), use the ‹ / › buttons on the card, the right-click menu (*Move earlier/later*, *Move to start/end*, *Duplicate*, *Delete interval*) or Alt+←/→ (⌥←/→ on a Mac). The trash button is red, every button has a tooltip and a hint line explains it all. Fixed: dropping a block between two blocks no longer sends it to the end. See the [1.1.2 release notes](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/tag/v1.1.2).

## What's new in 1.1.1

- **Music playlist fix** – the *Up Next* list on the Dashboard music card now stays inside the card and scrolls when space is tight, instead of drawing past the card frame; the Music Library lists have scrollbars and long names are shortened with "…". See the [1.1.1 release notes](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/tag/v1.1.1).

## What's new in 1.1.0

- **Workout calendar** – plan rides and rows by day, drag to move, start with a double-click, automatic tick-off.
- **Concept2 PM5 rowing** – *Free Row*, *Watt Plan* and *Ghost Race* against your personal best, plus a *Virtual Rower*.
- **Sport type & Rowing FTP** – cycling and rowing workouts, a sport filter in Workouts and History, a separate rowing FTP.
- **FIT export** – upload rides and rows to Garmin Connect, Strava, TrainingPeaks, intervals.icu (rows as *indoor rowing* with distance).
- **Workout sharing** – export/import Artilody `.awt` and Zwift `.zwo` files.
- **New app icon**, larger and more readable menus, light-theme fixes and many smaller fixes.

Existing data is kept: the database is upgraded automatically on first start. See the [release notes](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/tag/v1.1.0) for details.

---

## Contents

- [New: Free Ride, virtual gears and Zwift Click](#new-free-ride-virtual-gears-and-zwift-click)
- [What's new in 1.1.4](#whats-new-in-114)
- [What's new in 1.1.3](#whats-new-in-113)
- [What's new in 1.1.2](#whats-new-in-112)
- [What's new in 1.1.1](#whats-new-in-111)
- [What's new in 1.1.0](#whats-new-in-110)
- [Features](#features)
  - [Real Life Video](#real-life-video)
- [Screenshots](#screenshots)
- [Download & install](#download--install)
- [User manual](#user-manual)
- [Quick start](#quick-start)
- [Sharing workouts](#sharing-workouts)
- [Supported hardware](#supported-hardware)
- [Privacy](#privacy)
- [Building from source](#building-from-source)
- [Architecture](#architecture)
- [Contributing](#contributing)
- [License](#license)
- [Acknowledgements](#acknowledgements)

## Features

### Ride
- **Live dashboard** with power, cadence, heart rate and elapsed/remaining time.
- **Smooth power**: the displayed watts are a time-weighted rolling average over the last 3 s (on by default; off, 1, 5 or 10 s under **Settings** or by clicking the **3 s avg** badge on the power tile). It only affects the display – rides are recorded with the raw power.
- **Workout chart** with targets coloured by RPE/power zone, your heart-rate curve and a position marker, plus a timeline of the intervals.
- **On-the-fly control**: change the intensity in ±5 % steps, skip the current interval, and turn ERG on or off during the ride.
- **Start on pedaling**: the workout loads and waits until you start pedaling (cadence ≥ 20 rpm, or power ≥ 30 W on trainers that don't report cadence). Press **Start now** to start it yourself.
- **ERG or Free Ride**: in ERG mode the app sends the target power to the trainer. In Free Ride you still see the targets, but the trainer simulates a flat road and you control the effort.

### Free Ride & virtual shifting
- **Free Ride page** (sidebar → **Free Ride**): cycle without a workout. Choose a mode at the top: **Gears**, **ERG** or **Free**. Big tiles show power, speed, cadence, heart rate, time and distance (with averages), and a chart draws power, virtual speed and heart rate.
- **Virtual gears**: the current gear is shown large (e.g. *50 × 17*, *Gear 9 / 16*) with a sketch of the chainrings and cassette. **Easier** / **Harder** (or <kbd>↓</kbd>/<kbd>↑</kbd>, <kbd>−</kbd>/<kbd>+</kbd>, or the Zwift Click) shift; the app computes the speed from your cadence and the gear and sets the trainer to the power that speed needs on the road, so shifting harder gives more watts and more speed. A **grade** control (−15 % to +20 %, ±0.5 % steps) adds a virtual climb or descent.
- **ERG**: set a target wattage with the −25/−10/−5/+5/+10/+25 buttons or type an exact value; the trainer holds it until you change it. The Zwift Click changes the target by 10 W.
- **Start** now or **when pedaling**, **Pause** (<kbd>Space</kbd>), **Finish** (save or discard) and **Reset**; the ride is saved to History as a free ride and can be exported as FIT/CSV. A finish summary shows time, distance, average/max power, average speed and energy.
- **Zwift Click v1 / v2** over Bluetooth LE under **Devices → Controllers (Zwift Click)**, with battery level and a live button test. Press a button on the Click to wake it; a Click v2 has two pods – pair both.
- **Gear setup** (**Gear Setup…** on the Free Ride page): chainrings and cassette from presets (compact, semi-compact, standard, GRX, 1×, 11–28 … 10–50) or typed as numbers, wheel size, sequential or front + rear shifting, and (advanced) bike weight, CdA and Crr. A live preview lists every gear with ratio, speed at 90 rpm and power on the flat; invalid input is explained.

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/screenshots/freeride.png" alt="Free Ride with virtual gears"><br>
      <sub><b>Free Ride</b>: shift virtual gears for more watts and speed</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/screenshots/gear-setup.png" alt="Gear setup"><br>
      <sub><b>Gear Setup</b>: your chainrings, cassette and wheel</sub>
    </td>
  </tr>
</table>

### Real Life Video
- **Video Ride page** (sidebar → **Video Ride**): ride a filmed route. The video follows your position on the route's GPS track: its playback speed follows your riding speed (from about 0.25× to 2.5×; it pauses when you stop), and when it drifts more than a moment from your position it jumps back in sync. The page shows **power** (3 s average), **cadence**, **heart rate**, **speed** and **elapsed time** at the top, the **video** in the middle – with the gradient you ride, the resistance mode and difficulty and the route's on-screen texts as overlays – and at the bottom the **distance line**: the elevation profile of the whole route, coloured by gradient, with the ridden part highlighted, your position (*4.3 / 12.0 km*), km marks and small ticks where the route has texts. Before you start, click the profile to start part-way along the route.
- **Choosing a route**: the drop-down at the top lists every route in your library with its name, distance, climbing, video length and country. The folder button next to it adds folders, changes the videos folder, opens it or rescans the library.
- **Folder layout**: each route is a folder with the video (**.mp4**) and its GPS track (**.gpx**, with elevation), optionally a route file (**.xml**, the common RLV format with title, country, start/end frame and on-screen texts):

  ```
  Artilody Videos/
    FR_Le_petit_Menglonnais/
      FR_Le_petit_Menglonnais.mp4
      FR_Le_petit_Menglonnais.gpx
      FR_Le_petit_Menglonnais.xml
  ```
  The default videos folder is `Documents/Artilody Videos` in your home folder (created when you open it from the app); you can choose any other folder and add more folders, e.g. on an external drive.
- **Resistance** – three modes on the **Resistance** card (the choice is remembered):
  - **Simulation** (default, the classic RLV feel as in Tacx, Rouvy or Kinomap): climbs get harder like on the road – shift on your bike. The app sends the gradient to the trainer (FTMS *Set Indoor Bike Simulation Parameters*: grade, wind 0, Crr 0.004, Cw 0.51 kg/m – the values Zwift uses) when it changes by 0.1 % (at most once a second, refreshed every 3 s); the card shows the grade sent. Your speed comes from your power on the gradient, with the bike's momentum, so it changes smoothly. **Trainer difficulty** (0–100 %) scales the gradient; descents are limited to −10 %. If your trainer can't simulate a gradient, the card says so and the app uses virtual gears instead.
  - **Virtual gears**: the app shifts for you – **−** / **+**, <kbd>↓</kbd>/<kbd>↑</kbd> or the **Zwift Click**; set up your bike with **Gear Setup…** (shared with Free Ride). Speed comes from cadence and gear, and the trainer is set (ERG) to the power that speed needs on the gradient.
  - **ERG**: set a target wattage on the page (±5/10/25 W, an exact value, <kbd>↑</kbd>/<kbd>↓</kbd> ±5 W, <kbd>PgUp</kbd>/<kbd>PgDn</kbd> or the Zwift Click ±10 W) and the trainer holds it. Climbs make the video slower, not the pedals heavier.
  - **Difficulty** (Simulation and Virtual gears; <kbd>PgUp</kbd>/<kbd>PgDn</kbd> in 10 % steps) scales the gradients: 50 % makes a 6 % climb feel like 3 %.
- **Start** now or **when pedaling**, **Pause** (<kbd>Space</kbd>), **Finish** (save or discard) and **Reset**; reaching the end of the route finishes and saves the ride. Video rides are saved to History with the route name and can be exported as FIT/CSV.
- **Video formats**: H.264 MP4 plays everywhere. **AV1** videos only play on computers with AV1 hardware decoding (e.g. recent Apple Silicon or recent GPUs) – the page tells you when a video cannot be decoded and shows how to convert it. To convert a video to H.264 with the free [HandBrake](https://handbrake.fr):
  1. Open the video in HandBrake and choose the preset **General → Fast 720p30** (or **HQ 720p30 Surround**).
  2. On the **Video** tab set **Video Encoder** to **H.264 (x264)**, **Framerate** to **Same as source** and select **Constant Framerate** (important, so the video stays in sync with the GPS route).
  3. Save it as **.mp4** in the route folder with the **same file name** as the original, replacing it (keep the original somewhere else).
  4. Click **Rescan Library** (on the page, or in the folder menu next to the route drop-down).

  Or with ffmpeg: `ffmpeg -i video.mp4 -c:v libx264 -crf 21 -an video-h264.mp4`, then give the new file the original name.
- **Sound**: RLV videos are usually silent, so the sound is off (the speaker button on the video turns it on).
- **Raspberry Pi**: prefer **720p H.264** videos (1080p and AV1 are too heavy for a Pi) and install the GStreamer decoders: `sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-good` (both are recommended by the .deb package).

<p align="center">
  <img src="docs/screenshots/video-ride.png" alt="Video Ride with a Real Life Video" width="900"><br>
  <sub><b>Video Ride</b>: the video follows your speed; elevation profile with your position at the bottom.<br>Video: cycling.vangestel.online, CC BY-NC-SA 4.0</sub>
</p>

### Plan
- **Workout calendar**: a month view of your training plan. Click a day or a planned workout to select it; double-click a day, click its **+** or press **Add Workout** to add a workout (search, Cycling/Rowing filter, optional notes). Drag planned workouts between days, and right-click (or press and hold) for *Ride/Row this workout*, *Mark as completed*, *Move to…*, *Move to next day* and *Remove* (removing asks for confirmation). Double-click a workout or press **Ride**/**Row** in the side panel to start it: cycling workouts open on the Dashboard, rowing workouts as a watt plan on the Rowing page. A side panel shows the selected day and your upcoming workouts. When you complete a workout you started from the calendar, exactly that plan entry is ticked off (even if it was planned for another day), and a completed workout started elsewhere ticks off a matching open entry on that day. Stopped or aborted rides don't count, and deleting a ride in History reopens its plan entry.

<p align="center">
  <img src="docs/screenshots/calendar.png" alt="Workout calendar" width="820">
</p>

### Row (Concept2 PM5)
- **Concept2 PM5 over Bluetooth LE**: on the PM5 choose *More Options → Turn Wireless ON → Connect Device*, then connect it under **Devices → Rowers (Concept2 PM5)**. A **Virtual Rower** lets you try everything without a rower. Opening the Rowing page gets the rower ready (the row starts with your first stroke); if you leave the page before you start, the setup is cleared so a stroke can't start a row in the background. Heart rate comes from a connected heart-rate strap if you have one, otherwise from the PM5.
- **Free Row**: row a set distance (or open-ended) and watch your boat move along the course, with power, distance, pace /500 m, stroke rate, heart rate and a live power/pace/heart-rate chart.
- **Watt Plan**: row any rowing workout from your library. The interval blocks are shown at the bottom with your position, and you see target vs. actual watts, the time left in the interval, the next target, interval average and a compliance score. The PM5 has no ERG mode, so you follow the target by feel.
- **Ghost Race**: race your own personal best over 500 m, 1k, 2k, 5k, 6k, 10k or a custom distance. A ghost boat replays your record stroke by stroke, and the gap is shown in metres and seconds. A new best is saved automatically and the personal-bests table keeps your records. A time is only saved as a personal best if the piece was rowed without a pause and the time is plausible.
- The row starts on your first stroke (or press **Start now**), with **Pause** (or <kbd>Space</kbd>), **Finish** and **Reset**. Rows are saved to History as *Rowing* with distance and can be exported as FIT/CSV.
- **Rowing FTP**: every profile has a separate rowing FTP; watt-plan targets are a percentage of it.

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/screenshots/rowing-ghost.png" alt="Ghost race against your personal best"><br>
      <sub><b>Ghost Race</b>: race your personal best</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/screenshots/rowing-plan.png" alt="Rowing watt plan"><br>
      <sub><b>Watt Plan</b>: follow target watts on the erg</sub>
    </td>
  </tr>
</table>

### Train
- **Workout library** with 8 built-in workouts (Sweet Spot, Endurance, VO₂ Max, Threshold, Tempo, Over-Unders, Recovery and a Ramp FTP Test). It has search, category filters, sorting, favourites and a preview, and lets you duplicate or delete workouts. Built-in workouts can't be deleted.
- **Cycling and rowing workouts**: every workout has a sport. The library shows *Cycling*/*Rowing* badges and a sport filter, rowing workouts have a **Row Workout** button, and the Builder has a Cycling/Rowing selector (rowing targets use your rowing FTP and stroke rate in SPM). Any workout can be added to the calendar.
- **Workout Builder**, a visual editor: add blocks from presets (Warm Up, Steady, Intervals, Recovery, Cool Down, Ramp) and set the duration, target in watts or % FTP, ramp end power, cadence and RPE. The preview updates as you edit. If you edit a built-in workout, it is saved as your own copy. **Reorder** a block by dragging the ⋮⋮ grip strip at the top of its card (a blue line shows where it lands), with the ‹ / › buttons on the card, from the right-click menu (Move earlier/later, Move to start/end, Duplicate, Delete) or with Alt+←/→ (⌥←/→ on a Mac) for the selected block. **Delete** a block with the red trash button on its card. A hint line above the block list and tooltips on every button explain this.
- **Share workouts**: export any workout as an Artilody `.awt` file or a Zwift `.zwo` file, export all your own workouts in one file, and import `.awt`/`.zwo` files from friends, coaches or other apps (Zwift, TrainerRoad, Golden Cheetah, MyWhoosh, …). See [Sharing workouts](#sharing-workouts).
- **FTP test**: when you finish the built-in *Ramp FTP Test*, the app estimates your FTP as 75 % of your best 1-minute power and asks whether to update your profile. You can also pick any FTP test ride in History and use it as your FTP.

### Analyse
- **History** with a sport filter (All / Cycling / Rowing), totals, a chart of the selected ride (target, actual power and heart rate), weekly training time and time in zones.
- **Ride metrics**: average/max power, Normalized Power (NP), Intensity Factor (IF), Training Stress Score (TSS) and more.
- **FIT export** for Garmin Connect, Strava, TrainingPeaks, intervals.icu and other platforms: export the selected ride, or every ride in the selected period as one `.fit` file per ride. The file contains 1-second power, heart rate and cadence plus a session summary (NP, TSS, IF, threshold power, work).
- **CSV export** of every second of a ride, or a summary of all rides. You can also delete rides.

### Devices & profiles
- **Bluetooth LE** scanning for FTMS smart trainers (power, cadence, speed, ERG target power, flat-road simulation) and heart-rate sensors (HRS).
- Auto-connect and auto-reconnect, Bluetooth adapter selection, an ERG control test and a connection log.
- A **virtual trainer and heart-rate sensor**, so you can try every feature without hardware.
- **Concept2 PM5 rowers** (Concept2's own Bluetooth rowing service) with live power, pace and stroke rate on the Devices page.
- **Multiple rider profiles**, each with weight, FTP, rowing FTP, max/resting HR, preferred cadence, default mode and an avatar. Power zones can be edited, and heart-rate zones are calculated with the Karvonen method.

### Extras
- **Music player** for your own files (mp3, m4a, flac, wav, ogg, …) with playlists, favourites, shuffle/repeat and album art. Two sample tracks are included.
- **Light and dark theme.**
- Larger, easy-to-read menus (Workouts sort/export/**⋯** menus, History **Export**, the Calendar's right-click/press-and-hold menu).
- **Keyboard shortcuts:** <kbd>Space</kbd> pauses/resumes a ride on the Dashboard (and starts a waiting workout), a free ride on the Free Ride page or a row on the Rowing page; on the Free Ride page <kbd>↑</kbd>/<kbd>+</kbd> and <kbd>↓</kbd>/<kbd>−</kbd> shift harder/easier (±5 W in ERG), <kbd>Shift</kbd>+<kbd>↑</kbd>/<kbd>↓</kbd> change the chainring (or jump 3 gears) and <kbd>Page Up</kbd>/<kbd>Page Down</kbd> change the grade by 0.5 % (the ERG target by 10 W); <kbd>Esc</kbd> closes dialogs.
- The **UI scales** to small screens such as the official 7" Raspberry Pi display (800 × 480).
- **Fully offline**: all data is stored in a local SQLite database.

## Screenshots

<table>
  <tr>
    <td align="center" width="50%">
      <img src="docs/screenshots/workouts.png" alt="Workout library"><br>
      <sub><b>Workouts</b>: library with filters, favourites and preview</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/screenshots/builder.png" alt="Workout builder"><br>
      <sub><b>Builder</b>: visual block editor with live preview</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="docs/screenshots/history.png" alt="History and analysis"><br>
      <sub><b>History</b>: ride analysis, zones, NP / IF / TSS</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/screenshots/devices.png" alt="Devices"><br>
      <sub><b>Devices</b>: Bluetooth trainers and heart-rate sensors</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="docs/screenshots/rowing-solo.png" alt="Free row"><br>
      <sub><b>Free Row</b>: row a distance and watch the boat move</sub>
    </td>
    <td align="center" width="50%">
      <img src="docs/screenshots/calendar.png" alt="Calendar"><br>
      <sub><b>Calendar</b>: plan rides and rows, drag to move</sub>
    </td>
  </tr>
</table>

## Download & install

The current version is **1.1.4**. All files are on the **[Releases page](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/latest)**; direct links:

| Platform | Download |
|---|---|
| **Windows 10/11** (64-bit), installer | [`ArtilodyWattsTrainer-1.1.4-windows-x64-setup.exe`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/ArtilodyWattsTrainer-1.1.4-windows-x64-setup.exe) |
| **Windows 10/11** (64-bit), portable | [`ArtilodyWattsTrainer-1.1.4-windows-x64.zip`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/ArtilodyWattsTrainer-1.1.4-windows-x64.zip) |
| **macOS 12 Monterey or newer** (Apple Silicon and Intel) | [`ArtilodyWattsTrainer-1.1.4-macOS-universal.dmg`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/ArtilodyWattsTrainer-1.1.4-macOS-universal.dmg) |
| **Ubuntu 24.04 LTS** (amd64) | [`artilody-watts-trainer_1.1.4_ubuntu24.04_amd64.deb`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/artilody-watts-trainer_1.1.4_ubuntu24.04_amd64.deb) |
| **Debian 12 Bookworm** (amd64) | [`artilody-watts-trainer_1.1.4_debian12_amd64.deb`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/artilody-watts-trainer_1.1.4_debian12_amd64.deb) |
| **Raspberry Pi OS 64-bit Bookworm** (Pi 3 / 4 / 5 / 400) | [`artilody-watts-trainer_1.1.4_raspberrypi-os-bookworm_arm64.deb`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/artilody-watts-trainer_1.1.4_raspberrypi-os-bookworm_arm64.deb) |
| User manual (PDF) | [`Artilody-Watts-Trainer-Manual.pdf`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/Artilody-Watts-Trainer-Manual.pdf) |
| Checksums | [`SHA256SUMS.txt`](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/download/v1.1.4/SHA256SUMS.txt) – verify with `sha256sum -c SHA256SUMS.txt --ignore-missing` (Linux) or `shasum -a 256 <file>` (macOS) |

**System requirements:** Windows 10/11 (x64) · macOS 12 Monterey or newer (Apple Silicon or Intel) · Debian 12 or Ubuntu 24.04 LTS (amd64) · Raspberry Pi OS 64-bit Bookworm (arm64, Pi 3/4/5/400) · a Bluetooth LE adapter for real devices · minimum window size 800 × 480.

> [!TIP]
> **Upgrading from 1.0.0?** Just install the new version over the old one. Your rides, workouts, profiles and settings are kept; the database is upgraded automatically on first start.

<details>
<summary><b>Windows</b></summary>

1. Run `ArtilodyWattsTrainer-1.1.4-windows-x64-setup.exe`. It adds a Start menu entry and, if you choose, a desktop shortcut.
   If you'd rather not install anything, extract the ZIP and run `Artilody Watts Trainer.exe` from the extracted folder.
2. **SmartScreen:** the app is not code-signed yet, so Windows may show *"Windows protected your PC"*. Click **More info → Run anyway**.
3. Turn on Bluetooth under *Settings → Bluetooth & devices*. Bluetooth LE requires Windows 10 or newer.

</details>

<details>
<summary><b>macOS</b></summary>

1. Open the DMG and drag **Artilody Watts Trainer** into **Applications**.
2. **Gatekeeper:** the app is ad-hoc signed but not notarized by Apple. The first time you open it, **right-click (or Control-click) the app → Open → Open**.
   If macOS says the app is "damaged" or still won't open it, run this once:
   ```sh
   xattr -cr "/Applications/Artilody Watts Trainer.app"
   ```
3. Allow **Bluetooth access** when macOS asks. If you denied it by mistake, turn it on again under *System Settings → Privacy & Security → Bluetooth*.

</details>

<details>
<summary><b>Ubuntu 24.04 / Debian 12</b></summary>

Install the matching package with `apt`. It also installs the Qt 6 libraries the app needs:

```sh
sudo apt install ./artilody-watts-trainer_1.1.4_ubuntu24.04_amd64.deb   # Ubuntu 24.04
sudo apt install ./artilody-watts-trainer_1.1.4_debian12_amd64.deb      # Debian 12
```

Start **Artilody Watts Trainer** from the application menu or run `artilody-watts-trainer`.
Bluetooth needs the BlueZ service to be running: `sudo systemctl enable --now bluetooth`.
Ubuntu 22.04 and older are **not** supported, because they ship Qt 6.2 and the app requires Qt 6.4 or newer.

</details>

<details>
<summary><b>Raspberry Pi OS</b></summary>

Requires **Raspberry Pi OS (64-bit) Bookworm** on a Raspberry Pi 3, 4, 5 or 400. The 32-bit edition is not supported. A Pi 4 or 5 gives the smoothest UI.

```sh
sudo apt update
sudo apt install ./artilody-watts-trainer_1.1.4_raspberrypi-os-bookworm_arm64.deb
```

The built-in Bluetooth on the Pi 3/4/5 works with smart trainers and heart-rate sensors. Make sure Bluetooth is turned on in the desktop panel.

</details>

**Bluetooth requirements:** a Bluetooth LE (4.0+) adapter, either built in or a USB dongle. On Linux the app uses BlueZ. On macOS you need to allow Bluetooth access the first time you scan.

**Uninstall:** on Windows use *Settings → Apps*. On macOS move the app to the Trash. On Linux run `sudo apt remove artilody-watts-trainer`. Your rides and settings are kept in your user profile.

## User manual

The **[user manual (PDF)](docs/Artilody-Watts-Trainer-Manual.pdf)** covers every screen in detail. It is also attached to each [release](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/releases/latest).

## Quick start

1. **Connect your devices.** Open **Devices → Scan for Devices**, then connect your smart trainer and heart-rate sensor (and/or your Concept2 PM5 under *Rowers*). If you have no hardware, connect the **Virtual Trainer**, **Virtual HR Sensor** or **Virtual Rower** instead.
2. **Set up your profile.** Click **Profile** in the left sidebar (or open the profile menu by clicking your avatar/name at the top right and choose **Edit profile…**) to enter your weight, FTP (and rowing FTP, if you row) and heart-rate values. If you don't know your FTP, run the **Ramp FTP Test** first.
3. **Choose a workout** under **Workouts**, or create your own in **Builder**.
4. **Choose ERG or Free Ride**, then press **Start Workout**.
5. **Start pedaling.** The clock starts as soon as you pedal, or you can press **Start now**. On the dashboard, <kbd>Space</kbd> pauses and resumes (on the Rowing page too).
6. **Stop** saves the ride, and completed workouts are saved automatically. Open **History** to see the analysis, or use **Export** to save it as a FIT file (for Strava, Garmin Connect, TrainingPeaks, …) or CSV.
7. **Rowing?** Open **Rowing**, pick *Free Row*, *Ghost Race* or *Watt Plan* and take your first stroke. Use **Calendar** to plan the coming weeks.

## Sharing workouts

On the **Workouts** page, click **Export** in the filter bar (next to **Import**) and choose **Export selected workout…** to save the selected workout, or **Export all my workouts…** to save all your own workouts in one file. The selected workout can also be exported with the share button next to **Edit** at the bottom of the Workout Preview (or from its **⋯** menu). Use the **Import** button to import one or more files.

| Format | Best for | Notes |
|---|---|---|
| **`.awt`** (Artilody workout) | Sharing with other Artilody Watts Trainer users, backups | Keeps everything: name, sport (cycling/rowing), category, focus, description and every block (type, name, duration, start/end intensity, cadence, RPE). One file can hold many workouts. |
| **`.zwo`** (Zwift workout) | Zwift, TrainerRoad, Golden Cheetah, MyWhoosh and other apps | One workout per file, always cycling (rowing workouts are exported as bike workouts; imports become cycling workouts). RPE settings are not stored; on import, `IntervalsT` repeats are expanded into individual blocks, `FreeRide` and `MaxEffort` become steady blocks at 60 % / 150 % FTP. |

All intensities are stored **relative to FTP**, so a shared workout scales to the FTP of whoever imports it. Imported workouts are added to your own (*Custom*) workouts and can be edited or deleted. A workout that is already in your library is skipped, and a different workout with the same name is imported as *Name (imported)*. Files are validated before import (sizes, durations, intensities, categories).

An `.awt` file is plain UTF-8 JSON:

```json
{
  "format": "artilody-watts-trainer/workout",
  "version": 1,
  "exportedAt": "2026-10-23T12:00:00Z",
  "app": "Artilody Watts Trainer 1.1.4",
  "workouts": [
    {
      "name": "Sweet Spot 3 x 12",
      "sport": "cycling",
      "category": "Intervals",
      "focus": "Sweet Spot",
      "description": "Three 12-minute sweet spot efforts ...",
      "intervals": [
        { "name": "Warm Up", "type": "warmup", "duration": 360, "start": 0.40, "end": 0.65 },
        { "name": "Sweet Spot", "type": "interval", "duration": 720, "start": 0.90, "end": 0.90, "cadence": 90 }
      ]
    }
  ]
}
```

`sport` is `cycling` (default when missing) or `rowing`; `type` is one of `warmup`, `steady`, `interval`, `recovery`, `cooldown`, `ramp`; `duration` is in seconds; `start`/`end` are fractions of FTP (0.90 = 90 % FTP); `cadence` (RPM) and `rpe` (zone 0–5) are optional. The full description is in the [user manual](docs/Artilody-Watts-Trainer-Manual.pdf) (appendix C).

> [!NOTE]
> **About FIT files:** rides are written as FIT activity files by an independent encoder that is part of this project (no Garmin FIT SDK code is included). The files work with Garmin Connect, Strava, TrainingPeaks, intervals.icu and other platforms; upload them on the platform's website. Indoor rides contain no GPS, speed or distance. Rows are exported as **indoor rowing** activities (FIT sport *rowing*, sub-sport *indoor rowing*) with total distance, average speed, power, heart rate and stroke rate as cadence; the per-second distance curve is estimated by spreading the rowed distance in proportion to power^(1/3) (Concept2: speed ∝ power^(1/3)). FIT is a file format by Garmin; this project is not affiliated with Garmin.

## Supported hardware

**Smart trainers.** The app should work with any trainer that implements the standard Bluetooth **Fitness Machine Service (FTMS)**. Most current trainers do, for example Tacx NEO, Wahoo KICKR, Saris H3, Elite (Suito, Direto, …) and Zwift Hub. ERG control needs a trainer that supports FTMS *Set Target Power*.

**Rowers.** **Concept2 PM5** monitors (RowErg and other Concept2 machines with a PM5) over Bluetooth LE. On the PM5 choose *More Options → Turn Wireless ON → Connect Device* before scanning. The PM5 has no ERG mode, so watt plans are followed by feel. Older PM3/PM4 monitors are not supported, and BikeErg and SkiErg are not supported (the app only handles rowing data). If both a heart-rate strap and the PM5 report heart rate, the strap is used.

**Controllers.** **Zwift Click** (v1 and v2) for virtual shifting on the Free Ride page, without the Zwift app. Limits: firmware that insists on Zwift's encrypted mode is not supported (the connection log says so – update the Click in Zwift Companion), and the left pod of a Click v2 only sends buttons after Zwift has unlocked it (once per 24 h); its right pod always works. Zwift Play and Zwift Ride are not supported.

**Heart-rate sensors.** Any Bluetooth LE strap or armband using the standard **Heart Rate Service (HRS)** should work, for example Polar H10, Garmin HRM-Dual/HRM-Pro or Wahoo TICKR.

> [!NOTE]
> **ANT+ is not supported**, only Bluetooth LE. Older trainers that only offer a proprietary Bluetooth protocol (no FTMS) can't be controlled.

### Rowing machine not found

- The PM5 only advertises while it shows the **Connect Device** screen (*More Options → Turn Wireless ON → Connect Device*). Open it, then press **Scan for Devices** (a scan runs for 60 s).
- The PM5 accepts **one app at a time** – disconnect ErgData, the Concept2 app or any phone/watch connected to it.
- Current PM5 firmware advertises the standard **FTMS** fitness-machine service (0x1826) with the *Rower* machine type, not only the Concept2 services. Since 1.1.3 the app reads that FTMS advertisement and lists the PM5 under **Rowers** even before its name arrives. Older versions could list it under **Smart Trainers** – if a rowing machine still shows up there, press the **rowing button** next to it (*Connect as rower*). A device connected as trainer that turns out to offer rowing data (Concept2 rowing service or FTMS Rower Data) is moved to Rowers automatically and remembered.
- If the PM5 has no Concept2 rowing service (CE060030), the app uses its standard **FTMS Rower Data** instead – this also makes other FTMS rowing machines work.
- **Stroke rate missing** while power/pace/distance work: the stroke rate comes from the PM5's *Additional Status 1* (0x0032). Since 1.1.4 a silent 0x0032 is re-enabled and moved to the multiplexed channel, and otherwise the stroke rate is computed from the stroke timing (shown with ≈ on the Devices page). The *[Rower data]* section of the diagnostics report lists packet counters and raw bytes per characteristic – please include it in bug reports.
- Turn on **Show all devices** in *Available Devices*: unrecognised Bluetooth LE devices are listed under **Other Bluetooth devices** with a **Connect as rower** button.
- **Bluetooth Diagnostics → Details…** shows (and **Copy** copies) a report with the adapter, Qt version, every device seen with its service UUIDs, service data, parsed FTMS advertisement (machine type) and classification, the GATT services/characteristics found after each connection attempt, and any scan errors. Please attach it to bug reports. Scan errors are also shown in red above the device list.
- **Raspberry Pi / Linux:** `bluetoothctl show` must say `Powered: yes` (otherwise `bluetoothctl power on`, `sudo rfkill unblock bluetooth`), `bluetoothctl scan le` should list `PM5 430…` within a few seconds, and your user should be in the `bluetooth` group (`groups`). For a detailed log start the app from a terminal: `QT_LOGGING_RULES="qt.bluetooth*=true" artilody-watts-trainer 2>&1 | tee ~/artilody-bt.log` – lines starting with `artilody.ble:` show every device seen and how it was classified.
- Update an old PM5 with Concept2 Utility.

## Privacy

Artilody Watts Trainer runs **fully offline**:

- No account, no cloud and no telemetry or analytics. Nothing is uploaded: FIT, CSV and workout files are only written when you export them yourself.
- Rides and rows, profiles, workouts, the training plan, rowing records and playlists are stored in a local SQLite database (`watt-trainer.sqlite` in the user's app-data folder). The exact path is shown under **Settings** (the gear icon).
- To use a different database file, set the `WATT_DB_PATH` environment variable.

## Building from source

**Requirements:** CMake ≥ 3.21, a C++17 compiler, and **Qt ≥ 6.4** with the modules Core, Gui, Qml, Quick, QuickControls2, Sql (SQLite driver), Bluetooth (qtconnectivity) and Multimedia.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<path-to-Qt>/6.x/<platform>
cmake --build build --parallel
ctest --test-dir build --output-on-failure        # unit tests
```

On Debian 12, Ubuntu 24.04 and Raspberry Pi OS Bookworm you can use the distribution's Qt packages. Then `cd build && cpack -G DEB` builds an installable `.deb`.

See **[docs/BUILDING.md](docs/BUILDING.md)** for the full package lists, platform notes for macOS (`macdeployqt`) and Windows (MSVC + `windeployqt`), CMake options and developer tips.

## Architecture

The UI and the application logic are kept completely separate. The C++ libraries have no QML or UI dependencies and are unit tested on their own.

```
src/core    watt_core         Domain model (Workout, Interval, Zones), SQLite database, ride analysis
                              (NP/IF/TSS), CSV and FIT export, workout import/export (.awt/.zwo),
                              WorkoutEngine (pure state machine), WorkoutPlanner (calendar),
                              TrainingSession, RowingSession + RowingRecords (watt_rowing),
                              SensorHub, profile/library/builder/history managers
src/ble     watt_bleprotocol  Pure FTMS/HRS/Concept2 PM5 parsing and encoding (QtCore only, fully testable)
            watt_ble          BleLink, BleTrainer, BleHeartRateSensor, Pm5Rower, simulated devices, DeviceManager
src/media   watt_media        MusicPlayer and playlists
src/app     main.cpp          Wires the modules together and exposes them to QML
qml/        Main.qml, Theme.qml, components/, pages/, dialogs/  (presentation only)
tests/      QtTest unit tests (workout, engine, analysis, database + migration, session, builder, FIT,
            workout import/export, BLE/PM5 protocol, rower device, rowing session/records, planner)
```

- **`SensorHub`** decouples training from the sensors. Real BLE devices and the simulator send measurements to the hub, and `TrainingSession` asks for ERG targets through signals. This lets the tests drive a complete ride without hardware.
- The backend is exposed to QML as **singletons** (`import Artilody.Backend`): `Training`, `Rowing`, `Planner`, `Workouts`, `Builder`, `History`, `Devices`, `Sensors`, `Profiles` and `Music`.

## Contributing

Contributions are welcome:

- **Bugs and ideas:** open an [issue](https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer/issues). For device problems, include your OS, trainer or sensor model and the connection log from the Devices page.
- **Pull requests:** keep changes focused, follow the existing code style, keep UI code in `qml/` and logic in `src/`, and add or update tests in `tests/`. Make sure `ctest` passes before you submit.
- **Trainer compatibility reports** are especially useful. Let us know which trainers and sensors work for you.

## License

Artilody Watts Trainer is free software, licensed under the **GNU General Public License v3.0**. See [`LICENSE`](LICENSE) for details.

## Acknowledgements

- Built with the [Qt](https://www.qt.io/) framework (Qt Quick, Qt Bluetooth, Qt Multimedia, Qt SQL), used under the LGPL/GPL.
- Data storage by [SQLite](https://sqlite.org/).
- Uses the Bluetooth SIG's open **Fitness Machine** and **Heart Rate** service specifications and Concept2's public *PM5 Bluetooth Smart Interface Definition*. Concept2, PM5 and RowErg are trademarks of Concept2, Inc.; this project is not affiliated with Concept2.
- The training metrics NP, IF and TSS follow the widely used definitions by Dr. Andrew Coggan.
- FIT is a file format by Garmin; the FIT encoder in this project was written independently. Zwift is a trademark of Zwift, Inc.; `.zwo` support is for interoperability only. This project is not affiliated with Garmin or Zwift.
- Zwift Click support (virtual shifting) is an independent C++ implementation based on public protocol research: [jat255/zwift_click_handling](https://github.com/jat255/zwift_click_handling) (MIT License, © 2024 Joshua Taillon), Makinolo's articles on the [Zwift Play](https://www.makinolo.com/blog/2023/10/08/connecting-to-zwift-play-controllers/) and [Zwift Ride](https://www.makinolo.com/blog/2024/07/26/zwift-ride-protocol/) protocols, [ajchellew/zwiftplay](https://github.com/ajchellew/zwiftplay), [OpenBikeControl/bikecontrol](https://github.com/OpenBikeControl/bikecontrol) and [qdomyos-zwift](https://github.com/cagnulein/qdomyos-zwift). Zwift Click is a trademark of Zwift, Inc.
