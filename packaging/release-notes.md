Artilody Multisport Endurance Trainer (formerly Artilody Watts Trainer) is a free, open-source (GPL-3.0) desktop app for structured indoor cycling, rowing and treadmill running. It connects to your Bluetooth smart trainer, FTMS treadmill, heart-rate sensor and Concept2 PM5 rower, controls the trainer resistance in ERG mode and records every ride, row and run locally on your computer.

## What's new in 1.9.0

**GPX Ride: loop a route, with lap times.**

- **Loop route**: a new switch in GPX Ride – ride the route lap after lap until you press Finish. A **round trip** (start and end within 60 m) goes round and round: the 3D road is closed, so you ride on smoothly over the finish line into the next lap (no START arch right behind the FINISH arch). **Any other route** goes out and back: the way back is the route backwards, with the grade reversed (felt on the trainer too) and a finish arch at the turn.
- **Lap board**: a *LAPS* board just after the finish line shows the laps done.
- **Kilometre posts** are now bright, reflective yellow and count on lap after lap.
- **Lap times**: a panel on the 3D view shows the lap being ridden and every finished lap, the fastest in gold with the others' gap to it (a shorter first lap from a start point inside the route is marked *part*). Each finished lap shows a toast with its time.
- **History**: a looped ride keeps its lap times – in the ride's details (*Lap times*), in the **FIT** export as one lap per lap (Garmin Connect, Strava, TrainingPeaks, intervals.icu show them as laps) and in the **CSV** export (lap lines, a lap column; the summary has *laps* and *best_lap_s*).
- Sprints and KOMs are timed on every lap.
- **Database**: schema version 10 (the lap times). The update is automatic and keeps all data; after it, older versions of the app cannot open the database.

## Also included from 1.8.4

**Raspberry Pi: full screen fills the screen the first time.**

- **Full screen on the Raspberry Pi**: the first time the 3D view (or a video) went full screen on Raspberry Pi OS's Wayland desktop, a black band ran along the top and the left of the screen. The Raspberry Pi package now runs through XWayland (X11) on a Wayland desktop, where Qt's full-screen switch is clean. `QT_QPA_PLATFORM=wayland` still chooses Wayland.
- No database change – install over 1.8.3.

## Also included from 1.8.3

**Raspberry Pi: opening files and folders works again.**

- **Opening a GPX file, a video or a folder** crashed the self-contained Raspberry Pi package from 1.8.1 / 1.8.2: Qt's file and folder dialogs need a QML module (Qt.labs.folderlistmodel) the package lacked. It is included now, and the build checks that every QML module the bundled ones load is there.
- **Open folder** (logs, music, videos) on the Raspberry Pi starts the file manager with the system's libraries, not the app's.
- No database change – install over 1.8.2.

## Also included from 1.8.2

**Raspberry Pi: the self-contained package starts on the Wayland desktop.**

- **Raspberry Pi 4 / 5**: 1.8.1 did not start on Raspberry Pi OS's default Wayland desktop (*Failed to initialize graphics backend for OpenGL*): the package lacked Qt's Wayland EGL plugin. It is included now.
- **No OpenGL at all** (Linux): the app now starts with software drawing instead of not starting – everything works except the 3D views, which say why.
- No database change – install over 1.8.1.

## Also included from 1.8.1

**A self-contained Raspberry Pi package.**

- **Raspberry Pi OS**: the arm64 package now brings everything the app needs – Qt, GStreamer with the music and video decoders, and all other libraries – inside it (in `/opt/artilody-watts-trainer`). Installing it downloads nothing and needs no internet connection; it only uses what Raspberry Pi OS (64-bit Bookworm, desktop) already has: the graphics driver, fonts and the Bluetooth service. Install it over 1.8.0 or older as before: `sudo apt install ./artilody-watts-trainer_{{VERSION}}_raspberrypi-os-bookworm_arm64.deb`.
- **Diagnostic log**: records the app and Qt version, the graphics used for the 3D view, and which landscape each GPX route gets (e.g. `3D landscape australian (-33.87, 151.21; ...)`) – so a log shows at once which version ran and what it drew.
- **Graphics on other Linux computers**: when the default OpenGL context is too old for the 3D view (some Mesa drivers), the app asks for OpenGL 3.3, 3.1 or OpenGL ES 3.0 before giving up.
- No database change – install over 1.8.0.

## Also included from 1.8.0

**GPX Ride: landscapes from around the world, and birds.**

- **Six landscapes**: the 3D world now matches where a GPX route is on the earth and how high it goes – chosen automatically from the route's position and profile:
  - **Nordic** – green fields, spruce and broadleaf woods, farms and wind turbines (exactly the world of earlier versions);
  - **Tropical** – coconut palms, lush green, turquoise water, colourful houses;
  - **Australian** – red-ochre earth, gum trees and umbrella acacias, corrugated-iron roofs;
  - **Desert** – sand, saguaro cacti, sandstone, rare oases and earthen houses (the Sahara, Arabia, the south-west USA, the Atacama ...);
  - **Icy** – snow, frozen lakes and snowy spruce (the Arctic, Greenland, Alaska, Siberia);
  - **Mountain** – routes high up or climbing more than 25 m per km: snow above the snow line, trees thinning out below it, snow-capped peaks around.
  Sky, sunlight, water, houses, animals, hay bales, tractors and fences follow the landscape.
- **Birds**: flocks wheel over the fields beside the road, a bird of prey circles above it – a white-tailed sea eagle over the Nordic countryside, a wedge-tailed eagle in Australia, a golden eagle in the mountains, a vulture over the desert – and little birds fly from tree to tree, often across the road in front of you (finches and magpies, bee-eaters, lorikeets, sparrows, snow buntings). They fly in and out of view and rest hidden in the trees – they never just vanish.
- **Trees in the distance**: distant gum trees and acacias keep their branches (no floating crowns).
- **Hay bales**: no more black-wrapped bales (they looked like odd dark boxes from a distance).
- Light on the Raspberry Pi: the birds are a few thousand vertices per frame; snow is only computed where there can be snow.
- No database change – install over 1.7.1.

## Also included from 1.7.1

**Mountains, finishes and polish.**

- **Mountain climbs look like mountainsides**: on switchback climbs (Alpe d'Huez) the land rises from the lower road to the upper one as one slope, with rock on the steepest parts – no more riding along the top of a wall. Flat routes look as before.
- **Clean hairpins**: no gravel strips cutting up through the asphalt in tight, steep bends.
- **Finisher tunnel**: crowd barriers with coloured banners and flags line the road around the FINISH arch; after the finish the road runs on 90 m and tapers gently away instead of ending in the grass.
- **Bike wheels**: the tyres now sit in the rims (no gaps between tyre and rim).
- **Treadmill Run**: Finish and Reset buttons like the other training pages.
- **Rowing**: a red single scull with egg-white decks; the rower's unisuit has straps over the shoulders and clean armholes and neckline.
- No database change – install over 1.7.0.

## Also included from 1.7.0

**3D rowing, the GPX Workshop, training plans and full screen.**

- **3D rowing**: the Rowing page shows the row in 3D, seen from the side – a sculler in a single scull on a regatta lake, rowing at your stroke rate (catch, drive, finish and recovery), the boat moving with your pace. Lane buoys, distance boards every 250 m, the same trees as GPX Ride and, on rows with a distance, a finish line with a judges' tower and a cheering crowd. In Ghost Race a half-transparent ghost rows your record in the next lane. The page is laid out like GPX Ride: the 3D view in the middle, a smaller power / pace / heart-rate chart below.
- **Full screen** on GPX Ride, Video Ride, Treadmill Run and Rowing: the button in the corner of the picture, a double-click or <kbd>F</kbd> (<kbd>Esc</kbd> leaves). A half-transparent HUD on the left shows the ride's numbers and a mini elevation profile; <kbd>H</kbd> hides it.
- **GPX Workshop** (sidebar, under the Builder): see a route's elevation profile, place your own **sprints** (any length) and **KOMs** (foot and top) with names, and save them in the GPX file (a GPX extension other apps ignore). GPX Ride draws and times exactly these – as many as you like; routes without any have none. Sprint and KOM pins and their names show on GPX Ride's profile.
- **Elevation spikes removed**: single bad elevation points are removed and steeper steps than 20 % (typical at hairpins in GPS / map data) become even ramps, the height kept. The limit can be set per route in the GPX Workshop (5–30 %). The GPX file's track is never changed.
- **Training plans**: export weeks of your calendar as a shareable *.awplan* file (workouts and GPX routes included; video rides by route name), and import a plan onto any date from the **+** on a day. New calendar kinds: **GPX Ride**, **Free Row** and **Free Run**.
- **Roads**: where a route meets itself, the roads join in asphalt with rounded corners instead of a gravel verge across the other road.
- **Database**: sprint / KOM times are now kept per segment (no schema change) – install over 1.6.1.

## Also included from 1.6.1

**GPX Ride fixes and polish.**

- **Out-and-back routes**: after a turnaround the way back now joins the road already ridden gradually – no sideways jump of the road and the camera. Roads more than 3.5 m higher or lower (a hairpin on a climb, a bridge) are never merged.
- **Discarded rides**: a sprint or KOM time ridden in a ride you discard is deleted with it, so it can no longer become your best.
- **Smoother on the Raspberry Pi**: the cut-out letters (road text, kilometre posts, banners) are drawn with their own shader, so the landscape keeps the GPU's early depth test. Distant animals are simpler, and at most 150 are drawn around you.
- **Animals**: cows and horses have shorter, better proportioned bodies.
- **Wind turbines**: the blades are lit correctly from both sides and no longer flicker.
- **KOM panel**: times just under a minute boundary show correctly (2:00.0, not 1:60.0).
- **Database**: upgrades from development builds of 1.6.0 no longer fail on old sprint times of deleted profiles or rides. No schema change – install over 1.6.0.

## Also included from 1.6.0

**GPX Ride: sprint, KOM and a livelier countryside.**

- **Sprint** on every GPX route of at least 1 km: 200 m, a quarter into the route. *SPRINT IN 200 m* and a thin green line on the road, then a thick green start line with *SPRINT* and a green sign, and a red *SPRINT FINISH* line and sign 200 m later.
- **KOM** on the route's longest climb averaging at least 2.5 %: a white arch with red polka dots at the foot, a red *KOM FINISH* arch at the top.
- **Your times are saved and compared**: every sprint and KOM ridden all the way is timed to 0.01 s (paused time never counts), saved for your profile and the route – recognised by its track, so a renamed or moved GPX file keeps its times – and linked to the ride. A panel shows the distance to go, the running time, and at the line your time, the difference to your best and *New best!*.
- **A livelier countryside**: lakes beside the road with reeds, jetties and rowing boats; coastline with beaches; wild flowers and rocks; fenced pastures with cows, sheep and horses; hay bales and tractors; wind turbines with turning blades; benches; a white **kilometre post** at every kilometre; a display bike by the START arch.
- **Out-and-back routes and loops**: where the route comes back along a road already ridden, the 3D view now draws one road (no double edge lines or asphalt beside the road).
- **Sharper 3D**: no more flickering windows, doors and dark surfaces in the distance.
- **Database upgrade**: on first start the database is upgraded automatically to schema 9 (sprint and KOM times); all rides, workouts, profiles, plans and settings are kept – install over 1.5.1 (or older).

## Also included from 1.5.1

**Speed correction for treadmills and foot pods** (Devices → Treadmills).

- When a treadmill or a foot pod (e.g. **Runn**) is connected, its row has a new **Speed correction** tile with **−** / **+** buttons. Compare the speed with your watch or foot pod and press until they agree – in **0.1 km/h** steps (up to ±3 km/h), not a percentage you have to guess.
- The corrected speed is used everywhere: the Devices page, Treadmill Run, the 3D runner, distance, pace, calories, the AnT test, History and FIT export. A standing belt stays at 0.
- The correction is remembered per device. No database change – install over 1.5.0.

## Also included from 1.5.0

**A new name and a new sport**: the app is now **Artilody Multisport Endurance Trainer** – and it runs on your treadmill.

- **Treadmill Run** (sidebar → Treadmill Run, under GPX Ride): connect a Bluetooth **FTMS treadmill** (Devices → Treadmills, or the Virtual Treadmill) and run through the 3D world. Speed and incline are read from the treadmill – the app never controls the belt. Run a GPX route (GPX on/off) or the built-in **Artilody Lakeside 10K**, a 10 km loop with 11 corners, lap by lap.
- **A 3D runner** that walks and runs and changes gait at **your own walk → run speed** (new profile field *Walk → Run*, 4–10 km/h).
- **Running workouts** are ordinary workouts: the **Running** filter and *New running workout* on the Workouts page, the Workout Builder (speed in % of AnT or km/h, a suggested incline per block, running zones), the calendar (*Run*), `.awt` files and Zwift *run* `.zwo` files. Eight built-in running workouts plus the AnT test.
- **AnT test**: 10 min warm-up, 30 min test, 5 min cool-down; the anaerobic threshold speed is the average of the last 20 minutes. Test history, manual entry, running zones (78 / 88 / 95 / 100 / 106 % AnT) and a reminder when the next test is due.
- **New cyclist in GPX Ride**: a skinned rider (blue jersey, black bibs, red helmet) posed on the bike – feet on the pedals at your cadence, hands on the hoods, a gentle side-to-side rock that grows on climbs over 6 %.
- **3D**: the road runs on 50 m past the FINISH arch; contact shadows under wheels and feet; clean lines between clothes and skin.
- **Runs in History** and **FIT export** as running / treadmill activities.
- **Database upgrade**: on first start the database is upgraded automatically to schema 7; all rides, workouts, profiles, plans and settings are kept. The data folder, the macOS Bluetooth permission, the Windows installation and the Linux package name (`artilody-watts-trainer`) are unchanged – install over 1.4.1 (or older).

## Also included from 1.4.1

**GPX Ride**: start and finish arches, and a rider who pedals like you.

- **START and FINISH arches**: a green inflatable START arch stands 25 m ahead of where you start – also when you start part-way along the route – and a red FINISH arch at the end of the route, so you finish under it. The START arch is shown until you have ridden through it, then the FINISH arch: on a loop, where both stand at the same place, you start under green and finish under red.
- **The rider follows your real cadence**: the rider's legs turn at the cadence shown in the Cadence tile (smoothed over about half a second, so they don't jerk at each new value). They stand still when you stop pedaling, pause or have no cadence, while the wheels roll on with your speed – coasting downhill looks like coasting.
- No database change – install over 1.4.0 (or older) and keep all your data.

## Also included from 1.4.0

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

Earlier releases (1.1.4 PM5 stroke-rate fix, 1.1.3 PM5 on Raspberry Pi / Linux, 1.1.2 Builder reordering, 1.1.1 music fix, 1.1.0 calendar, rowing, FIT export and workout sharing) are described on the [Releases page](https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer/releases).

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
- **macOS**: the app is ad-hoc signed, not notarized. The first time, **right-click (Control-click) the app → Open → Open** (or run `xattr -cr "/Applications/Artilody Multisport Endurance Trainer.app"`).
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
| **Windows 10/11 (64-bit)**, installer | `ArtilodyMultisportEnduranceTrainer-{{VERSION}}-windows-x64-setup.exe` |
| **Windows 10/11 (64-bit)**, portable ZIP | `ArtilodyMultisportEnduranceTrainer-{{VERSION}}-windows-x64.zip` |
| **macOS 12 Monterey or newer** (Apple Silicon and Intel) | `ArtilodyMultisportEnduranceTrainer-{{VERSION}}-macOS-universal.dmg` |
| **Ubuntu 24.04 LTS** (and derivatives, amd64) | `artilody-watts-trainer_{{VERSION}}_ubuntu24.04_amd64.deb` |
| **Debian 12 Bookworm** (amd64) | `artilody-watts-trainer_{{VERSION}}_debian12_amd64.deb` |
| **Raspberry Pi OS 64-bit (Bookworm)**: Raspberry Pi 3, 4, 5, 400 | `artilody-watts-trainer_{{VERSION}}_raspberrypi-os-bookworm_arm64.deb` |
| User manual (PDF) | `Artilody-Multisport-Endurance-Trainer-Manual.pdf` |
| Checksums | `SHA256SUMS.txt` (verify with `sha256sum -c SHA256SUMS.txt --ignore-missing`) |

## Installation

### Windows

1. Download `ArtilodyMultisportEnduranceTrainer-{{VERSION}}-windows-x64-setup.exe` and run it. It creates a Start menu entry and, if you choose, a desktop shortcut.
   If you prefer not to install anything, extract the ZIP and start `Artilody Multisport Endurance Trainer.exe` from the extracted folder.
2. **SmartScreen:** the app is not code-signed yet, so Windows may show "Windows protected your PC". Click **More info → Run anyway**.
3. Bluetooth must be turned on (Settings → Bluetooth & devices). Bluetooth LE requires Windows 10 or newer.

### macOS

1. Open the DMG and drag **Artilody Multisport Endurance Trainer** onto the **Applications** folder.
2. **Gatekeeper:** the app is ad-hoc signed but not notarized by Apple. The first time you open it, **right-click (or Control-click) the app → Open → Open**.
   If macOS says the app is "damaged" or still won't open it, run this once in Terminal:
   ```sh
   xattr -cr "/Applications/Artilody Multisport Endurance Trainer.app"
   ```
3. **Bluetooth permission:** when macOS asks, allow Bluetooth access. If you denied it by mistake, turn it on again under System Settings → Privacy & Security → Bluetooth.

### Ubuntu 24.04 / Debian 12

Download the matching `.deb` and install it with apt. Apt also installs all required Qt 6 libraries:

```sh
sudo apt install ./artilody-watts-trainer_{{VERSION}}_ubuntu24.04_amd64.deb    # Ubuntu 24.04
sudo apt install ./artilody-watts-trainer_{{VERSION}}_debian12_amd64.deb        # Debian 12
```

Start **Artilody Multisport Endurance Trainer** from the application menu, or run `artilody-watts-trainer` in a terminal.
Bluetooth needs the BlueZ service to be running (`sudo systemctl enable --now bluetooth`). Ubuntu 22.04 and older are **not** supported, because they ship Qt 6.2 and the app needs Qt 6.4 or newer.

### Raspberry Pi

You need **Raspberry Pi OS (64-bit) Bookworm** on a Raspberry Pi 3, 4, 5 or 400. The 32-bit edition is not supported. A Pi 4 or 5 is recommended for a smooth UI.

```sh
sudo apt update
sudo apt install ./artilody-watts-trainer_{{VERSION}}_raspberrypi-os-bookworm_arm64.deb
```

The built-in Bluetooth of the Pi 3/4/5 works with smart trainers and heart-rate sensors. Make sure Bluetooth is turned on in the desktop panel.

### Uninstall

- Windows: Settings → Apps → Artilody Multisport Endurance Trainer → Uninstall
- macOS: move the app from Applications to the Trash
- Linux: `sudo apt remove artilody-watts-trainer`

Your rides and settings are stored separately in your user profile and are kept when you uninstall.

## Feedback

Found a bug or have an idea? Please open an issue: https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer/issues
