"""Text of the Artilody Watts Trainer user manual.

Written against the v1.3.4 source code (incl. Zwift Click virtual shifting, calendar with Free Ride/Video Ride planning, rowing, Free Ride, virtual gears and Real Life Video with Workout ERG) (qml/, src/core, src/ble, src/media,
src/app/main.cpp, packaging/release-notes.md). Markup is ReportLab paragraph
markup (<b>, <i>, <font>, <br/>, &amp; ...). Used by make_manual.py.
"""


def K(key):
    """Keyboard key."""
    return f'<font name="Courier-Bold" backColor="#e3e8f5" color="#14204f">&nbsp;{key}&nbsp;</font>'


def UI(label):
    """A label as shown in the user interface."""
    return f'<b>{label}</b>'


def C(text):
    """Inline code / file name."""
    return f'<font name="Courier" color="#14204f">{text}</font>'


def L(url, text=None):
    return f'<link href="{url}" color="#2178ff"><u>{text or url}</u></link>'


def build(s, env):
    SHOTS, MOCKUPS, REPO, V = env["SHOTS"], env["MOCKUPS"], env["REPO"], env["VERSION"]
    Z = s.zone_hex
    RELEASES = REPO + "/releases/latest"

    # =====================================================================
    s.chapter("Introduction", new_page=True, lead=
              "Artilody Watts Trainer is a free, open-source desktop app for structured, power-based indoor "
              "cycling and rowing. It connects to your Bluetooth smart trainer, heart-rate sensor and Concept2 "
              "PM5 rower, controls the trainer's resistance in ERG mode and records every ride and row locally "
              "on your computer.")

    s.section("What the app does")
    s.bullets([
        f"{UI('Rides structured workouts')} – eight built-in workouts plus any number of your own, created in "
        "the visual Workout Builder.",
        f"{UI('Controls your smart trainer')} – in ERG mode the app sends the target power of every interval to "
        "the trainer, which sets the resistance so that you produce exactly that power.",
        f"{UI('Shows live data')} – power, cadence, heart rate, elapsed time, the workout profile and a "
        "timeline of all intervals.",
        f"{UI('Records and analyses')} – every second of every ride is stored. History shows totals, charts, "
        "time in zones, Normalized Power, Intensity Factor and Training Stress Score. Rides can be exported as FIT "
        "files (for Strava, Garmin Connect, TrainingPeaks, intervals.icu …) or as CSV files.",
        f"{UI('Rides filmed routes')} – Real Life Videos play at your speed while the trainer simulates the "
        "gradient of the road (Video Ride page).",
        f"{UI('Plans your training')} – a month calendar for planned rides and rows; planned workouts are "
        "started with a double-click and ticked off automatically when you complete them.",
        f"{UI('Rows on a Concept2')} – connects to a Concept2 PM5 for free rows over a distance, watt-plan "
        "rowing workouts and ghost races against your own personal best.",
        f"{UI('Shares workouts')} – export your workouts as Artilody {C('.awt')} files or Zwift {C('.zwo')} "
        "files, and import workouts that others have shared with you.",
        f"{UI('Estimates your FTP')} – the built-in Ramp FTP Test (or your own 20-minute test) calculates your "
        "Functional Threshold Power and can update your profile and all training zones in one click.",
        f"{UI('Plays music')} – a built-in player for your own audio files with playlists and favourites.",
        f"{UI('Works offline')} – no account, no cloud, no tracking. All data is kept in a local SQLite "
        "database.",
    ])

    s.section("Key concepts")
    s.sub("Watts and FTP")
    s.p("Power, measured in <b>watts (W)</b>, is the most direct measure of how hard you are working on the "
        "bike. Unlike heart rate it responds instantly and is not affected by heat, fatigue or caffeine. "
        "<b>FTP (Functional Threshold Power)</b> is the highest power you can sustain for roughly one hour. "
        "All workouts in Artilody Watts Trainer are stored as a percentage of FTP, so when your FTP changes, "
        "every workout target and every training zone is scaled automatically.")
    s.sub("ERG mode and Free Ride")
    s.p(f"In {UI('ERG mode')} the app tells the trainer which power to hold. If you pedal faster, the trainer "
        "lowers the resistance; if you pedal slower, it increases the resistance – your power stays on target. "
        f"In {UI('Free Ride')} the trainer does not follow the targets: it simulates riding on a flat road, "
        "and you control the effort with your gears and cadence. Targets are still shown, so you can follow "
        "them by feel. A ride without any workout is also called a Free Ride.")
    s.sub("Power zones and RPE")
    s.p("Training intensity is divided into six power zones, each with a colour that is used consistently in "
        "charts, the timeline and the Watt Training page. Each zone also corresponds to a range on the RPE scale "
        "(Rating of Perceived Exertion, 1–10), which tells you how hard an effort should feel:")
    s.table(["Zone", "Name", "Default range", "RPE", "Feels"], [
        ["Z1", "Recovery", "below 55 % FTP", "1", "Very easy"],
        ["Z2", "Endurance", "55 – 75 % FTP", "2 – 3", "Easy"],
        ["Z3", "Tempo", "75 – 87 % FTP", "4 – 6", "Moderate"],
        ["Z4", "Sweet Spot", "87 – 94 % FTP", "7 – 8", "Vigorous"],
        ["Z5", "Threshold", "94 – 105 % FTP", "9", "Very hard"],
        ["Z6", "VO<sub>2</sub> Max", "above 105 % FTP", "10", "Max effort"],
    ], [0.7, 1.4, 1.6, 0.8, 1.2], swatches=list(Z), keep=True)
    s.p("The zone boundaries can be adjusted per profile (see chapter 4). Heart-rate zones are calculated "
        "separately with the Karvonen method.")

    s.section("System requirements")
    s.table(["Platform", "Requirement"], [
        ["Windows", "Windows 10 or 11, 64-bit (x64). Bluetooth LE requires Windows 10 or newer."],
        ["macOS", "macOS 12 Monterey or newer, Apple Silicon or Intel (universal app)."],
        ["Linux", "Ubuntu 24.04 LTS (amd64) or Debian 12 Bookworm (amd64). The app requires Qt 6.4 or newer, "
                  "so Ubuntu 22.04 and older are not supported. Bluetooth requires the BlueZ service."],
        ["Raspberry Pi", "Raspberry Pi OS (64-bit) Bookworm on a Raspberry Pi 3, 4, 5 or 400. The 32-bit "
                         "edition is not supported. A Pi 4 or 5 is recommended for a smooth UI."],
        ["Display", "Designed for 1584 × 992 pixels; the whole interface scales down uniformly on smaller "
                    "windows. Minimum window size 800 × 480 (e.g. the official 7\" Raspberry Pi display)."],
        ["Bluetooth", "A Bluetooth Low Energy (BLE, Bluetooth 4.0 or newer) adapter – built-in or USB."],
    ], [1.1, 4.6])
    s.sub("Supported devices")
    s.bullets([
        f"{UI('Smart trainers')} that implement the standard Bluetooth <b>Fitness Machine Service (FTMS)</b>. "
        "Most current trainers do, for example Tacx NEO, Wahoo KICKR, Saris H3, Elite (Suito, Direto, …) and "
        "Zwift Hub. ERG control requires a trainer that supports the FTMS <i>Set Target Power</i> command.",
        f"{UI('Heart-rate sensors')} that use the standard Bluetooth <b>Heart Rate Service (HRS)</b>, for "
        "example Polar H10, Garmin HRM-Dual/HRM-Pro or Wahoo TICKR straps and armbands.",
        f"{UI('Rowers')}: Concept2 rowing machines with a <b>PM5</b> monitor (Bluetooth LE). The PM5 has no "
        "ERG mode – watt targets are followed by feel (chapter 11).",
    ])
    s.warning("<b>ANT+ is not supported</b> – only Bluetooth LE. Older trainers that offer only a proprietary "
              "Bluetooth protocol (no FTMS) cannot be controlled.")

    # =====================================================================
    s.chapter("Installation",
              f"Download the file for your platform from the releases page: {L(RELEASES)}. "
              f"All packages contain the same version {V}.")
    s.table(["Platform", "File"], [
        ["Windows 10/11 (64-bit), installer", C(f"ArtilodyWattsTrainer-{V}-windows-x64-setup.exe")],
        ["Windows 10/11 (64-bit), portable ZIP", C(f"ArtilodyWattsTrainer-{V}-windows-x64.zip")],
        ["macOS 12 or newer (Apple Silicon and Intel)", C(f"ArtilodyWattsTrainer-{V}-macOS-universal.dmg")],
        ["Ubuntu 24.04 LTS (amd64)", C(f"artilody-watts-trainer_{V}_ubuntu24.04_amd64.deb")],
        ["Debian 12 Bookworm (amd64)", C(f"artilody-watts-trainer_{V}_debian12_amd64.deb")],
        ["Raspberry Pi OS 64-bit Bookworm (Pi 3/4/5/400)",
         C(f"artilody-watts-trainer_{V}_raspberrypi-os-bookworm_arm64.deb")],
        ["User manual (this document)", C("Artilody-Watts-Trainer-Manual.pdf")],
        ["Checksums", C("SHA256SUMS.txt")],
    ], [2.3, 3.4])
    s.tip(f"To check a download, compare its SHA-256 checksum with {C('SHA256SUMS.txt')}: on Linux run "
          f"{C('sha256sum -c SHA256SUMS.txt --ignore-missing')} in the download folder, on macOS "
          f"{C('shasum -a 256 &lt;file&gt;')}, on Windows {C('Get-FileHash &lt;file&gt;')} in PowerShell.")
    s.p("<b>Upgrading from an earlier version:</b> install the new version over the old one. Rides, workouts, "
        "profiles and settings are kept; the database is upgraded automatically on first start.")

    s.section("Windows")
    s.steps([
        f"Download {C(f'ArtilodyWattsTrainer-{V}-windows-x64-setup.exe')} and run it. The installer creates a "
        "Start menu entry and, if you choose, a desktop shortcut.",
        f"Prefer not to install anything? Extract the ZIP file instead and start "
        f"{C('Artilody Watts Trainer.exe')} from the extracted folder.",
        f"<b>SmartScreen:</b> the app is not code-signed yet, so Windows may show <i>“Windows protected your "
        f"PC”</i>. Click {UI('More info')} → {UI('Run anyway')}.",
        f"Turn on Bluetooth under {UI('Settings → Bluetooth &amp; devices')}.",
    ])

    s.section("macOS")
    s.steps([
        f"Open the DMG and drag {UI('Artilody Watts Trainer')} onto the {UI('Applications')} folder.",
        f"<b>Gatekeeper:</b> the app is ad-hoc signed but not notarized by Apple. The first time you open it, "
        f"right-click (or Control-click) the app and choose {UI('Open')}, then confirm with {UI('Open')}.",
        "If macOS reports that the app is “damaged” or still refuses to open it, run this command once in "
        "Terminal:",
    ])
    s.code('xattr -cr "/Applications/Artilody Watts Trainer.app"')
    s.p(f"<b>Bluetooth permission:</b> when macOS asks whether the app may use Bluetooth, click "
        f"{UI('Allow')}. If you denied access by mistake, enable it again under "
        f"{UI('System Settings → Privacy &amp; Security → Bluetooth')}. Without this permission the app can "
        "only use the virtual devices; the connection log shows <i>“Bluetooth permission denied”</i>.")

    s.section("Ubuntu 24.04 and Debian 12")
    s.p("Download the matching .deb package and install it with apt, which also installs all required Qt 6 "
        "libraries:")
    s.code(f"sudo apt install ./artilody-watts-trainer_{V}_ubuntu24.04_amd64.deb   # Ubuntu 24.04\n"
           f"sudo apt install ./artilody-watts-trainer_{V}_debian12_amd64.deb      # Debian 12")
    s.p(f"Start {UI('Artilody Watts Trainer')} from the application menu or run {C('artilody-watts-trainer')} "
        "in a terminal. Bluetooth needs the BlueZ service to be running:")
    s.code("sudo systemctl enable --now bluetooth")

    s.section("Raspberry Pi OS")
    s.p("You need <b>Raspberry Pi OS (64-bit) Bookworm</b> on a Raspberry Pi 3, 4, 5 or 400. Install the arm64 "
        "package:")
    s.code("sudo apt update\n"
           f"sudo apt install ./artilody-watts-trainer_{V}_raspberrypi-os-bookworm_arm64.deb")
    s.p("The built-in Bluetooth of the Pi 3/4/5 works with smart trainers and heart-rate sensors. Make sure "
        "Bluetooth is switched on in the desktop panel.")
    s.tip("On small displays such as the official 7\" touch screen (800 × 480), maximise the window. The "
          "interface is laid out for 1584 × 992 pixels and scales down uniformly, so every element stays in "
          "place – only smaller. A Pi 4 or 5 gives the smoothest charts and animations.")

    s.section("Uninstalling")
    s.bullets([
        f"Windows: {UI('Settings → Apps → Artilody Watts Trainer → Uninstall')}.",
        "macOS: move the app from Applications to the Trash.",
        f"Linux and Raspberry Pi: {C('sudo apt remove artilody-watts-trainer')}.",
    ])
    s.note("Your rides, profiles, workouts and settings are stored separately in your user folder (see "
           "section 15.3) and are kept when you uninstall the app.")

    s.section("First start")
    s.p("On the very first start the app creates its database and fills it with:")
    s.bullets([
        "one rider profile (weight 68 kg, FTP 250 W, max HR 175 BPM, resting HR 58 BPM, preferred cadence "
        "85 RPM, default mode ERG) – open the profile and enter your own name and values;",
        "the eight built-in workouts (see Appendix A);",
        "a music playlist called <i>Workout Mix</i> with the sample tracks shipped with the app;",
        "a virtual trainer, a virtual heart-rate sensor and a virtual rower, so you can try everything "
        "without hardware.",
    ])
    s.p("A good order for getting started:")
    s.steps([
        f"{UI('Connect your devices')} under {UI('Devices → Scan for Devices')} (chapter 5). Without hardware, "
        f"connect {UI('Virtual Trainer')} and {UI('Virtual HR Sensor')}.",
        f"{UI('Set up your profile')} – weight, FTP and heart-rate values (chapter 4). If you don't know your "
        "FTP, ride the Ramp FTP Test first (chapter 8).",
        f"{UI('Choose a workout')} under {UI('Workouts')}, or build your own in {UI('Builder')}.",
        f"Choose {UI('ERG Mode')} or {UI('Free Ride')} and press {UI('Load Workout')}. The workout opens on "
        f"{UI('Watt Training')}; clip in and press {UI('Start')} (or {K('Space')}). Nothing starts by pedaling.",
        f"When you are done, press {UI('Stop')} and save the ride. Open {UI('History')} for the analysis.",
    ])

    # =====================================================================
    s.chapter("The main window",
              "The window consists of a navigation sidebar on the left, the current page in the middle and a "
              "small top bar in the upper-right corner.")
    s.figure(SHOTS / "dashboard.png",
             "The main window with the Watt Training page during a ride: sidebar (left), live metrics and charts "
             "(centre), devices, ERG switch, controls and music player (right).")
    s.section("Sidebar")
    s.table(["Item", "Purpose"], [
        [UI("Watt Training"), "(Bicycle icon; formerly Dashboard.) The cycling workout page – live metrics, charts and controls; press Start to begin."],
        [UI("Free Ride"), "Ride without a workout: a road with a grade (your gears or virtual gears) or ERG (chapter 12)."],
        [UI("Video Ride"), "Ride a filmed route (Real Life Video): the video follows your speed (chapter 13)."],
        [UI("Rowing"), "Rowing with a Concept2 PM5: Free Row, Watt Plan and Ghost Race (chapter 11)."],
        [UI("Workouts"), "The workout library: browse, search, preview and start cycling and rowing workouts."],
        [UI("Calendar"), "The training plan: plan, move and start workouts, Free Rides and Video Rides by day "
         "(chapter 10)."],
        [UI("Builder"), "The Workout Builder for creating and editing workouts."],
        [UI("History"), "Recorded rides and rows, analysis, time in zones, FIT and CSV export."],
        [UI("Devices"), "Bluetooth scanning, connections, diagnostics and trainer test."],
        [UI("Profile"), "Opens the User Profile dialog for the current rider."],
    ], [1.2, 4.5])
    s.p("The four training pages – Watt Training (cycling workouts), Free Ride, Video Ride and Rowing – are at the top, "
        "followed by planning (Workouts, Calendar, Builder), History and Devices.")
    s.p("While a workout is running or paused, a status box appears at the bottom of the sidebar. It shows "
        "<i>Riding</i> or <i>Paused</i> together with the elapsed time and the "
        "current power (a workout that is only loaded, not started, is not shown). Click it to jump back to the Watt Training page from any page. A free ride shows "
        "<i>Free Ride 12:34 • 50×17 • 210 W</i> (time, gear or ERG, power) and leads to the Free Ride page; a video "
        "ride shows <i>Video 12:34 • 4.3 km • 210 W</i> (with a workout: <i>Video 12:34</i> / <i>Sweet Spot "
        "2/6 • 230 W</i>) and leads to the Video Ride page; a row leads to the Rowing page.")
    s.section("Top bar")
    s.bullets([
        f"{UI('Sun/moon icon')} – switches between the dark and the light theme. The app always starts in the "
        "dark theme; the choice applies until you close the app.",
        f"{UI('Gear icon')} – opens the Settings dialog (section 15.1).",
        f"{UI('Avatar and name')} – opens the profile menu: a list of all profiles (name and FTP) to switch "
        f"between, {UI('Edit profile…')} and {UI('New profile…')}. Switching and creating profiles is disabled "
        "while a ride is running or paused.",
    ])
    s.section("Window size and scaling")
    s.p("The interface is designed for 1584 × 992 pixels. The window opens at that size (or the available "
        "screen size, whichever is smaller). If you make it smaller, the entire interface is scaled down "
        "uniformly so the layout is always preserved; it is never scaled above 100 %. The minimum window "
        "size is 800 × 480 pixels.")
    s.p("Short messages (for example <i>“Ride saved – see History for the analysis”</i>) appear briefly as a "
        "notification at the bottom of the window.")

    # =====================================================================
    s.chapter("Setting up your profile",
              "Your profile tells the app how strong you are. FTP drives all power targets and power zones; "
              "maximum and resting heart rate drive the heart-rate zones.")
    s.section("Opening the profile")
    s.p(f"Click {UI('Profile')} in the sidebar, or click your avatar in the top bar and choose "
        f"{UI('Edit profile…')}. The {UI('User Profile')} dialog opens.")
    s.figure(MOCKUPS / "muckup brugerprofil.png",
             "The User Profile dialog (design mockup – the dialog in the app looks very similar). "
             "In the app, a “Training zones ›” link below the fields expands the zone editor.",
             width_frac=0.5, crop=(509, 227, 1030, 806))
    s.section("Profile fields")
    s.table(["Field", "Range in the dialog", "Notes"], [
        ["Name", "–", "Must not be empty."],
        ["Weight", "30 – 200 kg, steps of 0.5", "Used for W/kg (shown under the FTP field) and the road "
         "model of virtual gears (mass and auto air drag)."],
        ["Height", "0 or 100 – 230 cm", "Optional (0 = not set). Refines the auto air drag (CdA) of virtual gears."],
        ["FTP", "50 – 600 W, steps of 5", "Used to calculate power zones and all cycling workout targets."],
        ["Rowing FTP", "50 – 600 W, steps of 5", "Used for rowing workouts and rows (default 150 W, "
         "section 11.7)."],
        ["Max heart rate", "100 – 230 BPM", "Must be more than 20 BPM above the resting heart rate."],
        ["Resting heart rate", "30 – 110 BPM", "Used together with max HR for the Karvonen zones."],
        ["Preferred cadence", "50 – 130 RPM",
         "Default cadence target on the Watt Training page when an interval has no cadence of its own."],
        ["Default mode", "ERG or Free Ride", "Pre-selected ride mode in the workout preview."],
    ], [1.4, 1.8, 3.0])
    s.p(f"Click {UI('Save Profile')} to store the changes. If a value is invalid, the dialog stays open and "
        "shows the reason in red (for example <i>“Max heart rate must be above resting heart rate”</i>). "
        f"{UI('Cancel')} closes the dialog without saving.")
    s.p("Changes to FTP and zones take effect immediately for the workout library, the Builder and the "
        "Watt Training page. A ride that is already running keeps the profile values it started with.")
    s.sub("Avatar")
    s.p("Click the round picture (camera icon) to choose a profile picture (PNG, JPG, JPEG, BMP or WebP). "
        "Without a picture the first letter of your name is shown. The app remembers the location of the "
        "image file rather than copying it, so keep the file where it is.")

    s.section("Training zones")
    s.p(f"Click {UI('Training zones ›')} to expand the zone editor on the right side of the dialog.")
    s.sub("Power zones")
    s.p("Each of the zones Z1–Z5 has an editable upper boundary in percent of FTP (20–200 %); the next zone "
        "starts where the previous one ends, and Z6 covers everything above the Z5 boundary. Next to each zone "
        "the resulting watt range for the FTP in the dialog is shown. The boundaries must increase from zone "
        "to zone. "
        f"{UI('Reset')} restores the default boundaries 55 / 75 / 87 / 94 / 105 %.")
    s.table(["Zone", "% of FTP (default)", "Watts at FTP 250 W", "RPE"], [
        ["Z1 Recovery", "0 – 55 %", "0 – 137 W", "1 Very Easy"],
        ["Z2 Endurance", "55 – 75 %", "138 – 187 W", "2 – 3 Easy"],
        ["Z3 Tempo", "75 – 87 %", "188 – 217 W", "4 – 6 Moderate"],
        ["Z4 Sweet Spot", "87 – 94 %", "218 – 234 W", "7 – 8 Vigorous"],
        ["Z5 Threshold", "94 – 105 %", "235 – 262 W", "9 Very Hard"],
        ["Z6 VO<sub>2</sub> Max", "above 105 %", "263 W and more", "10 Max Effort"],
    ], [1.5, 1.4, 1.5, 1.4], swatches=list(Z), keep=True)
    s.sub("Heart-rate zones (Karvonen)")
    s.p("Heart-rate zones are calculated from your heart-rate reserve (max HR − resting HR) with the Karvonen "
        "formula: <i>zone limit = resting HR + percentage × (max HR − resting HR)</i>. They update live while you "
        "edit the heart-rate fields and cannot be edited directly.")
    s.table(["Zone", "% of heart-rate reserve", "Example (max 175, rest 58)"], [
        ["Z1 Very Light", "50 – 60 %", "117 – 128 BPM"],
        ["Z2 Light", "60 – 70 %", "128 – 140 BPM"],
        ["Z3 Moderate", "70 – 80 %", "140 – 152 BPM"],
        ["Z4 Hard", "80 – 90 %", "152 – 163 BPM"],
        ["Z5 Maximum", "90 – 100 %", "163 – 175 BPM"],
    ], [1.6, 1.9, 2.2], swatches=["#3d86fb", "#45d659", "#ffd21f", "#ff8a26", "#ff3b30"], keep=True)
    s.p("A heart rate below 50 % of the reserve is shown as <i>“Below zone 1”</i> on the Watt Training page.")

    s.section("Multiple profiles")
    s.p("Several riders can share one installation. Every profile has its own values, zones and ride history; "
        "History always shows the rides of the current profile only.")
    s.bullets([
        f"<b>Create</b> – avatar menu → {UI('New profile…')}. The dialog starts with an empty name and the "
        f"values of the current profile; click {UI('Create Profile')}. The new profile becomes the current one.",
        "<b>Switch</b> – avatar menu → click a profile name.",
        "<b>Delete</b> – open the profile and click the trash button next to Save Profile. It is only shown when "
        "there is more than one profile and no ride is active. Deleting a profile also deletes all of its rides.",
    ])
    s.note("You cannot switch, create or delete profiles while a ride is running or paused. Stop the ride "
           "first. Rides are always stored for the profile that was active when the ride started.")

    # =====================================================================
    s.chapter("Connecting devices",
              "The Devices page finds and connects Bluetooth LE smart trainers, heart-rate sensors and "
              "Concept2 PM5 rowers, and "
              "helps you diagnose connection problems.")
    s.figure(SHOTS / "devices.png",
             "The Devices page with the virtual trainer and virtual heart-rate sensor connected.")
    s.section("Scanning")
    s.p(f"Press {UI('Scan for Devices')}. The app searches for Bluetooth LE devices for up to 12 seconds; press "
        f"the button again ({UI('Scanning…')}) to stop early. The round arrow button next to it "
        f"({UI('Refresh adapter')}) and {UI('Rescan')} start a new scan. The two status cards at the top show "
        "the Bluetooth state (<i>Ready</i>, <i>Off</i> or <i>Unavailable</i>) and how many devices are connected.")
    s.p("Only relevant devices are listed. A device is shown as a <b>smart trainer</b> if it advertises the "
        "FTMS service, and as a <b>heart-rate sensor</b> if it advertises the Heart Rate Service. Devices that "
        "do not advertise these services are recognised by typical names (for example KICKR, NEO, Tacx, Saris, "
        "Elite, Direto, Suito, Zwift, Wahoo, Magene – or HRM, Polar, H10, TICKR, Coospo, Verity).")
    s.tip("Wake your devices before scanning: pedal the trainer for a few turns and moisten/put on the "
          "heart-rate strap. Most sensors stop advertising when idle.")

    s.section("Available devices and connecting")
    s.p(f"Found devices appear under {UI('Smart Trainers')}, {UI('Heart Rate Sensors')} and "
        f"{UI('Rowers (Concept2 PM5)')} (the sections can be collapsed; connecting a PM5 is described in "
        "section 11.1). Each row shows the name, connection state and protocol. Click "
        f"{UI('Connect')} to connect; while connecting the button reads {UI('Cancel')}, and once connected "
        f"{UI('Disconnect')}. Connected devices show live values: power and cadence for a trainer, heart rate "
        "for a sensor.")
    s.p("The gear button opens the device info: identifier (Bluetooth address), protocol, signal strength "
        "(<i>Strong</i> ≥ −60 dBm, <i>Good</i> ≥ −75 dBm, otherwise <i>Weak</i>), status and type.")
    s.bullets([
        "A connected device is remembered as your <b>known</b> trainer or heart-rate sensor, so it can be "
        "connected automatically next time.",
        f"Pressing {UI('Disconnect')} forgets the device again, so it will not auto-connect until you connect "
        "it manually.",
        "Only one trainer, one heart-rate sensor and one rower can be connected at a time. Connecting another trainer "
        "replaces the current one.",
        "If no heart-rate sensor is connected but your trainer reports heart rate (some trainers pass on a "
        "paired strap), the trainer's heart rate is used.",
    ])
    s.p("The Watt Training page's <b>Devices</b> card summarises both connections (<i>Connected</i>, <i>Connecting</i>, "
        "<i>Offline</i> or <i>Not paired</i>); click it to open the Devices page.")

    s.section("Selected trainer and capabilities")
    s.p(f"The {UI('Selected Trainer')} card shows the connected trainer, its state, protocol, signal strength "
        "and the capabilities detected over FTMS: <i>Power</i>, <i>Cadence</i>, <i>ERG Control</i>, "
        "<i>Free Ride</i> and <i>Heart Rate</i>. If <i>ERG Control</i> is missing, the trainer only reports data "
        "and cannot be controlled by the app.")

    s.section("Controllers (Zwift Click)")
    s.p(f"A <b>Zwift Click</b> (v1 or v2) is listed under {UI('Controllers (Zwift Click)')} and shifts the "
        f"virtual gears in {UI('Free Ride')} and {UI('Video Ride')} (section 5.5). Press any button on the Click to wake it, then "
        f"scan and press {UI('Connect')}. A <b>Click v2</b> consists of two pods – connect both (the section "
        "header then shows <i>2 Zwift Click pods</i>). A connected Click shows its <b>battery</b> level and a live "
        f"<b>button test</b>: the {UI('−')} / {UI('+')} indicators light up while a button is pressed.")
    s.note("The Click connects without the Zwift app, in its unencrypted mode. Firmware that insists on Zwift's "
           "encrypted mode is not supported – the connection log says so; update the Click in Zwift Companion. "
           "The left pod of a Click v2 only sends buttons after Zwift has unlocked it (once per 24 hours); the "
           "right pod always works. Zwift Play and Zwift Ride controllers are not supported.")

    s.section("Virtual shifting (Zwift Click)")
    s.p("Virtual shifting is tied to the <b>Zwift Click</b>, not to the trainer, and there is nothing to set up. "
        "Shifting changes the gear, never the trainer difficulty.")
    s.table(["", "Trainer with the Zwift trainer service", "Other FTMS trainer"], [
        ["Zwift Click connected", "Zwift's 24 virtual gears via the <b>Zwift protocol</b> (the trainer does the "
                                  "physics) – <i>Virtual shifting: Zwift protocol</i>",
         "Zwift's 24 virtual gears via a gear-adjusted <b>FTMS gradient</b> – <i>Virtual shifting: FTMS gradient</i>"],
        ["No Zwift Click", "Your bike's real gears, plain FTMS gradient simulation (route grade × difficulty) – "
                           "<i>Shifting: your bike's gears</i>",
         "Your bike's real gears, plain FTMS gradient simulation – <i>Shifting: your bike's gears</i>"],
    ], [1.3, 2.6, 2.6])
    s.p(f"The {UI('Virtual shifting')} card in the {UI('Controllers (Zwift Click)')} section shows this status "
        "line and why (e.g. <i>The trainer does the physics (incline + gear ratio)</i> or <i>Zwift protocol "
        "failed (no answer to the handshake) – using FTMS gradient</i>). While a Click is connected it also shows "
        f"the current gear, e.g. <i>Gear 12/24 (2.40)</i>, with {UI('−')} / {UI('+')} to try the Click without "
        "riding. The same status line appears on the Selected Trainer card and on the Free Ride and Video Ride "
        "pages; on-screen shift buttons are only shown while a Click is connected. A trainer that supports the "
        "Zwift protocol is <b>not</b> switched to it without a Click – it may carry a normal cassette.")
    s.table(["Zwift Click", "Free Ride: Ride / Video Ride: Simulation", "ERG", "Other pages"], [
        [UI("+"), "Shift harder", "Target +10 W", "Shift harder"],
        [UI("−"), "Shift easier", "Target −10 W", "Shift easier"],
    ], [1.0, 2.4, 1.2, 1.2])
    s.p("<b>Zwift's 24 gears.</b> The Click always shifts Zwift's fixed gear table – the same ratios Zwift uses "
        "(also used by Incyclist and qdomyos-zwift): 0.75, 0.87, 0.99, 1.11, 1.23, 1.38, 1.53, 1.68, 1.86, 2.04, "
        "2.22, <b>2.40</b> (gear 12, the start gear), 2.61, 2.82, 3.03, 3.24, 3.49, 3.74, 3.99, 4.24, 4.54, 4.84, "
        "5.14, 5.49. A gear setup of earlier versions (chainrings, cassette) stays stored but is no longer used.")
    s.p("<b>Zwift protocol (automatic).</b> When the <i>connected</i> trainer exposes the Zwift trainer service "
        "(0xFC82 or 00000001-19CA-4651-86E5-FA29DCDD09D1 with the characteristics 0002, 0003 and 0004 – e.g. "
        "Zwift Hub, Jetblack Victory, Wahoo KICKR Core with Zwift Cog) and a Click is connected, the app performs "
        "the Zwift handshake (<i>RideOn</i>), starts the trainer's data stream and then sends the incline (route "
        "grade × difficulty, or the Free Ride grade), Zwift's fixed air drag (CWa 0.51) and rolling resistance "
        "(Crr 0.004), your weight, the bike weight and the ratio of the current gear – in Zwift's order: after "
        "every incline the gear ratio again, then a gear-ratio request (dataId 520). A shift goes out at once, "
        "the incline when it changes by 0.1 % or more and every 2.5 s. The trainer computes the resistance like "
        "in Zwift, so light gears are really light even on a single 14T cog. FTMS control is requested (as "
        "Incyclist does) and FTMS delivers the riding data, but while the Zwift protocol is in use (also during "
        "its handshake, at a pause and as the start state) no FTMS start, gradient, neutral or ERG command is "
        "sent – Zwift sends none either. Speed and video follow your measured power on the route (road "
        "physics with inertia). The gear starts at 12 when the app starts and only the Click changes it – "
        "never a stop or a pause.")
    s.p("<b>FTMS gradient.</b> On other trainers the app sends a gear-adjusted grade: with the bike in the "
        "reference gear the trainer's flywheel turns at v<sub>ref</sub> = cadence × R<sub>reference</sub> × wheel "
        "circumference, the virtual bike rides at v<sub>v</sub> = cadence × R<sub>virtual</sub> × wheel "
        "circumference, and the grade is chosen so that the brake delivers the road power for v<sub>v</sub> on "
        "the route grade at v<sub>ref</sub> (limited to −10 … +20 %). The reference gear defaults to 50 × 17; "
        "when the trainer's speed and cadence show a stable, different real gear, the card and the ride page ask "
        "<i>“Your trainer reports 44×14 (3.14) but the setting is 50×17 (2.94) — use the detected gear?”</i> "
        f"with {UI('Use detected gear')} and {UI('Keep 50×17')}. Tip: a small real gear such as 34 × 17 makes "
        "the easy gears lighter. Some trainers clamp negative grades to 0 % – the Gradient test shows it.")
    s.p("<b>Fallback.</b> If the trainer does not answer the Zwift handshake (2 attempts of 3 s, then 4 s for the "
        "data stream) or stops sending, the app switches to FTMS gradient and shows it on the ride page. It never "
        "replaces the simulation with ERG; only a trainer without any gradient simulation is driven with ERG at "
        "the gear's watts.")
    s.p(f"<b>Advanced overrides</b> (Bluetooth Diagnostics → {UI('Advanced…')}, for troubleshooting only): "
        f"{UI('Zwift trainer protocol')} <i>Automatic (default)</i> / <i>Off – FTMS gradient only</i>; "
        f"{UI('FTMS virtual shifting')} <i>Gradient (default)</i> or the experimental <i>Auto</i> / <i>ERG</i> "
        "(only for the session, every start is Gradient again); and the bike (section 5.6).")
    s.p("<b>The grade you feel.</b> Video Ride uses the grade at your position looking slightly ahead "
        "(the smoothed elevation from 10 m behind to 20 m ahead), so a descent is felt within about 20 m of its "
        "start. If gears feel wrong, send the diagnostic log (see “Troubleshooting & FAQ”): its <i>VSHIFT</i> "
        "lines show the shifting decisions, the <i>ZHUB</i> lines every Zwift-protocol step in hex and decoded.")

    s.section("Bike (advanced)")
    s.figure(SHOTS / "gear-setup.png",
             "The advanced Bike dialog: wheel size, bike weight, air drag and Crr, with Zwift's 24 gears and "
             "their speed at 90 rpm.", width_frac=0.9)
    s.p(f"Bluetooth Diagnostics → {UI('Advanced…')} → {UI('Bike: wheel, weight, air drag…')} refines the road "
        "physics behind the virtual gears; nothing here is needed for shifting. Saved per profile.")
    s.table(["Field", "Meaning"], [
        [UI("Wheel size"), "A tyre preset (700×23c … 29×2.2) or the circumference in millimetres (1000–3000): "
                           "the virtual speed of each gear."],
        [UI("Bike and air drag"), "Bike weight (0–60 kg), air drag CdA – <i>CdA auto (from weight)</i> (default) "
                                  "or your own value (0.10–1.00 m²; hoods ≈ 0.32, drops ≈ 0.28) – and Crr (rolling "
                                  "resistance, 0.001–0.050; road tyres ≈ 0.004). Your own weight (and height) come "
                                  "from the profile."],
    ], [1.2, 4.6])
    s.p("<b>Road model – your weight counts.</b> Every virtual speed v needs the road power "
        "P = (Crr·m·g·cos θ + m·g·sin θ + ½·ρ·CdA·v²)·v / η, with m = your profile weight + the bike weight "
        "(default 9 kg), θ the grade, Crr 0.004, air density ρ = 1.225 kg/m³ and drivetrain efficiency "
        "η = 0.97. Example: 40 km/h on the flat with 77 + 9 kg and CdA 0.32 needs about 310 W, in line with "
        "online power calculators. On a climb the mass dominates, on the flat the air drag – so the CdA follows "
        "your size too. <i>Auto</i> CdA = 0.32 m² × (weight / 75 kg)<super>2/3</super> on the hoods; with a height "
        "in your profile 0.32 m² × BSA(weight, height) / BSA(75 kg, 175 cm), where BSA is the body surface area "
        "after Du Bois (0.007184 · kg<super>0.425</super> · cm<super>0.725</super>) – the frontal area of a "
        "cyclist scales with it (Heil 2001). A lighter rider therefore needs fewer watts in the same gear at the "
        "same cadence: at 90 rpm in 50×14 (40.6 km/h) about 280 W on the flat and 670 W at 5 % for 60 kg, "
        "against 370 W and 940 W for 90 kg.")
    s.table(["Gear at 90 rpm", "Speed", "60 kg flat", "90 kg flat", "60 kg 5 %", "90 kg 5 %"], [
        ["34×34", "11.4 km/h", "20 W", "20 W", "124 W", "178 W"],
        ["34×24", "16.1 km/h", "28 W", "38 W", "184 W", "262 W"],
        ["50×21", "27.1 km/h", "95 W", "127 W", "357 W", "503 W"],
        ["50×17", "33.4 km/h", "165 W", "220 W", "489 W", "684 W"],
        ["50×14", "40.6 km/h", "281 W", "372 W", "674 W", "936 W"],
        ["50×11", "51.7 km/h", "555 W", "732 W", "1055 W", "1449 W"],
    ], [1.3, 1.0, 0.9, 0.9, 0.9, 0.9])
    s.p(f"The {UI('Virtual shifting')} card shows the values in use, e.g. <i>Rider + bike: 77 kg · CdA 0.30 "
        f"(auto)</i>, with a link to {UI('Edit weight')} (the profile). "
        "The same constants are used everywhere: Free Ride, Video Ride, the gear-adjusted grade, the "
        "simulation parameters sent to the trainer (Crr and Cw = ρ·CdA, e.g. 0.37 kg/m for 68 kg; with the Zwift "
        "protocol also your weight and the bike weight) and the Virtual Trainer.")
    s.note("FTMS has no weight field: a real trainer that simulates an FTMS gradient uses its own rider-weight "
           "setting (in the manufacturer's app). With the Zwift protocol the app sends your weight + bike itself.")

    s.section("Virtual devices")
    s.p(f"{UI('Virtual Trainer')} (Simulated FTMS) and {UI('Virtual HR Sensor')} (Simulated HRS) are built-in "
        "simulators marked with a <i>VIRTUAL</i> badge. They let you try every feature – workouts, ERG, "
        "History, the FTP dialog – without any hardware (developers: <i>WATT_SIM_TRAINER_ZWIFT=1</i> gives it "
        "the Zwift trainer protocol). With virtual gears on, the virtual rider leaves the "
        "bike in the reference gear and pedals ~88 rpm – no shifting of his own – so the watts follow your "
        "virtual gear and the road. He has your profile's FTP as his power (bursts up to 1.5 × FTP for about a "
        "minute): in a gear that is too hard his cadence drops (down to 40 rpm), in an easy gear he spins up "
        "to ~105 rpm. The virtual trainer simulates with your weight + bike; with virtual gears off he "
        "shifts his own bike towards the effort he wants. The virtual heart rate reacts to the simulated load "
        "and uses the resting and maximum heart rate of your profile. Hide them with "
        f"{UI('Show virtual devices')} (Connection Preferences or Settings). Rides with virtual devices are "
        "recorded like any other ride.")

    s.section("Connection preferences")
    s.table(["Option (default: on)", "Effect"], [
        [UI("Auto-connect known devices"),
         "At start-up the app connects the remembered trainer and heart-rate sensor. For real devices it "
         "starts a scan automatically and connects them as soon as they are found."],
        [UI("Reconnect when signal is lost"),
         "If a connection drops, the app retries automatically – after 2 s, then 4 s, 6 s … up to every 30 s. "
         "After reconnecting, the last ERG target is sent again."],
        [UI("Allow multiple sensors"),
         "Trainer and heart-rate sensor can be connected at the same time. When switched off, only one device "
         "can be connected; connecting another device disconnects the first."],
        [UI("Show virtual devices"), "Lists the virtual trainer and heart-rate sensor."],
        [UI("Bluetooth Adapter"),
         "Chooses the adapter used for scanning and connecting: <i>Built-in</i> (system default) or a specific "
         "adapter if your computer has several (for example a USB dongle)."],
    ], [1.9, 4.0])
    s.p("All preferences are saved in the database and kept between sessions.")

    s.section("Trainer status and rejected commands")
    s.p("The app shows what the trainer <i>actually</i> does, taken from its answers on the FTMS Control Point and "
        f"its Fitness Machine Status – in {UI('Selected Trainer')}, in the row {UI('Trainer control')} of "
        "Bluetooth Diagnostics and, when something is wrong, as a yellow note on Video Ride and Free Ride:")
    s.table(["Text", "Meaning"], [
        ["Simulation – trainer accepted −2.00 %", "the last gradient command was confirmed"],
        ["ERG – trainer accepted 200 W", "the trainer holds the watts"],
        ["Trainer rejected the gradient command (Invalid parameter)",
         "result code 0x03; also <i>(Operation failed)</i> 0x04, <i>(Control not permitted)</i> 0x05, "
         "<i>(Op Code not supported)</i> 0x02"],
        ["Trainer did not answer the gradient command (no response within 2 s)", "no indication within 2 s"],
        ["Trainer stopped / paused", "the trainer reported stop/pause – the app starts it again and re-sends the command"],
        ["Trainer control lost – requesting it again", "control permission lost (another app, a reset) – the app asks again and re-sends"],
        ["Trainer doesn't report its gradient range", "no Supported Inclination Range (0x2AD5), e.g. "
         "Jetblack Victory – grades are sent unclamped (−10 … 20 %)"],
    ], [2.6, 3.0])
    s.p("<b>Defined start state.</b> When the trainer grants control and no ride has sent a command yet, the "
        "app sets a neutral state – a flat road (0 %, simulation with your rolling and air resistance), or a "
        "low resistance level on a trainer without gradient simulation – so an ERG target left over from "
        "another app does not linger. The first command of a ride replaces it at once. The log shows "
        "<i>neutral state sent</i>.")
    s.p("Rejected and unanswered commands are retried after 1, 2 and 4 s (at most three times); the next "
        "regular command is sent as usual. The app never shows “simulation” when the last gradient command was "
        "not accepted. Trainers without the optional Training Status (0x2AD3) or inclination range work "
        "without waiting for them.")

    s.section("Gradient test")
    s.p(f"Devices → {UI('Bluetooth Diagnostics')} → {UI('Details…')} → {UI('Gradient test…')} checks whether the "
        "trainer really simulates descents. Put the bike in any real gear and pedal steadily at about 90 rpm, "
        f"then press {UI('Start')}. The app sends +2 %, 0 %, −2 % and −5 % as FTMS gradient simulation (never "
        "ERG), 20 s each, and measures power, speed and cadence after an 8 s settling time. The table shows the "
        "averages per step and whether the trainer confirmed each command; the verdict reads e.g. "
        "<i>“Power keeps falling on descents → the trainer simulates descents”</i> or <i>“Power stops falling "
        "below 0 % → the trainer clamps descents”</i>. "
        f"{UI('Stop')} ends the test at any time; afterwards the previous trainer command is restored. The test "
        "also ends by itself when you leave the Devices page, a ride starts or a ride page sends a resistance "
        "command. Every step is written to the diagnostic log (lines <i>GTEST</i>). Not available during a "
        "workout.")

    s.section("Bluetooth diagnostics and connection log")
    s.p(f"{UI('Bluetooth Diagnostics')} shows the adapter name, the connection mode (Bluetooth Low Energy), the "
        "trainer signal (<i>Stable</i> at −75 dBm or better, otherwise <i>Weak</i>), the time of the last "
        f"scan and the trainer control status. {UI('Refresh')} re-reads the Bluetooth adapter. {UI('Details…')} opens a full report – adapter, "
        "Qt version, every device seen with signal, service UUIDs and how the app classified it, all GATT services "
        "of the connected trainer with their characteristics and properties (FTMS, Zwift 0xFC82, vendor "
        "services), the trainer's actual mode and last rejection, scan errors, "
        "the live rower data (which source is active – Concept2 rowing service, the 0x0080 multiplexed channel or "
        "FTMS Rower Data –, packet counters and the last raw bytes of every subscribed characteristic, the last "
        "decoded stroke rate and whether the stroke rate comes from the monitor or is computed from the stroke "
        f"timing) and the log – that you can {UI('Copy')} into a bug report. The row {UI('Rower data')} shows the "
        "active rower data source and stroke rate at a glance. With {UI('Show all devices')} (top of "
        f"{UI('Available Devices')}) the list gets an extra section {UI('Other Bluetooth devices')} with every "
        f"unrecognised Bluetooth LE device and a {UI('Connect as rower')} button (section 11.2). Scan problems "
        "(Bluetooth off, adapter error, missing permission) appear in red above the device list.")
    s.p(f"The {UI('Connection Log')} lists time-stamped events with a green (OK) or red (problem) dot – for "
        "example <i>Bluetooth ready</i>, <i>Found …</i>, <i>FTMS capabilities detected</i>, <i>Trainer control "
        "granted</i>, <i>Reconnecting to … in 4 s</i> or <i>Bluetooth permission denied</i>. "
        f"{UI('Clear')} empties the log. The log is the first place to look when something does not work.")

    s.section("Trainer control (ERG) test")
    s.p(f"Use {UI('Trainer Control Test')} to check that the app can control your trainer before you start a "
        f"workout. Set an {UI('ERG Target')} with − / + (steps of 10 W, 25–1500 W, default 150 W) and press "
        f"{UI('Test ERG')}. While pedaling you should feel the resistance change; the button shows the measured "
        f"power. Changing the target during the test sends the new value immediately. {UI('Stop Test')} ends the "
        "test and returns the trainer to the current workout target, or to free ride if nothing is riding. "
        "While the test runs the card shows <i>ERG test running – 150 W</i> in yellow.")
    s.p("<b>The test never sticks.</b> If you forget to press Stop Test, the ERG test ends by itself – and the "
        "trainer gets the ride's own command back – when you leave the Devices page, when a workout, Free Ride, "
        "Video Ride or row starts, when a ride page sends a resistance command, and at the latest after "
        "<b>5 minutes</b> (every new target restarts the 5 minutes). The connection log says why, e.g. "
        "<i>ERG test stopped automatically: left the Devices page</i>.")
    s.note("The test is only available when a trainer is connected and supports control. If the log shows "
           "<i>Trainer control refused</i>, another app (Zwift, a phone app, a watch) is probably connected "
           "to the trainer – close it and try again.")

    s.section("Connection troubleshooting")
    s.bullets([
        "Trainer or sensor not listed: wake it up, move it closer, make sure it is not connected to another app "
        "or device (most BLE devices accept only one controlling connection), then scan again.",
        "Check that Bluetooth is turned on and, on macOS, that the app has Bluetooth permission.",
        "On Linux and Raspberry Pi check that BlueZ is running: " + C("systemctl status bluetooth") + ".",
        "Values drop to 0 or “–”: power and cadence are reset after 3 seconds without new data, heart rate "
        "after 6 seconds. This usually means a weak signal – reduce the distance or use a USB adapter with an "
        "extension cable.",
        "See chapter 16 for more solutions.",
    ])

    # =====================================================================
    s.chapter("Choosing and riding a workout",
              "Pick a workout in the library, choose ERG or Free Ride, load it and press Start – the Watt Training "
              "page guides you through every interval.")
    s.figure(SHOTS / "workouts.png", "The Workouts library with the preview panel on the right.")
    s.section("The workout library")
    s.bullets([
        f"{UI('Search')} – filters by name, focus and category as you type.",
        f"{UI('Sport')} – <i>All sports</i>, <i>Cycling</i> or <i>Rowing</i> (section 11.9).",
        f"{UI('Category')} – <i>All</i>, <i>Endurance</i>, <i>Intervals</i>, <i>Recovery</i>, <i>FTP Tests</i> or "
        "<i>Custom</i>. Custom shows all your own workouts, whatever their category.",
        f"{UI('Sort')} – <i>Most Popular</i> (most often started first; default), <i>Name A–Z</i>, "
        "<i>Shortest</i>, <i>Longest</i>, <i>Favorites</i> (favourites first) or <i>Newest</i>.",
        f"{UI('Import')} – imports shared workouts from {C('.awt')} or Zwift {C('.zwo')} files (section 6.2).",
        f"{UI('New Workout')} – choose {UI('New cycling workout')} or {UI('New rowing workout')}; it opens "
        "in the Builder.",
    ])
    s.p("Each card shows the category (your own workouts are marked <i>CUSTOM</i>), name, duration, a mini "
        "profile chart and a summary such as <i>3 × 12 min @ 225 W • Intervals</i>. Watt values are always "
        "calculated from your current FTP. Click the heart to mark a favourite. Click a card to select it, "
        "double-click to open it in the Builder.")
    s.p("Selecting a workout while nothing is riding also loads it on the Watt Training page, so you can look at it "
        "there before starting.")
    s.sub("Workout preview")
    s.p("The preview panel shows the name, category, duration and focus, a larger profile chart, and the "
        f"tiles {UI('Duration')}, {UI('Intensity')} (average % of FTP) and {UI('Intervals')} (e.g. 3 × 12 min), "
        f"followed by the description. Below it are {UI('Load Workout')}, {UI('Edit')} (opens the workout in "
        f"the Builder) and a share button that exports the selected workout (section 6.2). The {UI('•••')} "
        "(more) menu offers:")
    s.bullets([
        f"{UI('Export selected workout…')} – saves the selected workout as an Artilody {C('.awt')} file or a Zwift {C('.zwo')} "
        "file to share it (section 6.2).",
        f"{UI('Export all my workouts…')} – saves all your own (custom) workouts into one {C('.awt')} file.",
        f"{UI('Add to calendar…')} – plans the workout on a day of your choice (chapter 10).",
        f"{UI('Duplicate')} – creates your own copy named “… (copy)” that you can edit freely.",
        f"{UI('Delete')} – removes one of your own workouts after confirmation. Rides already recorded with it "
        "stay in History. Built-in workouts cannot be deleted; for the Ramp FTP Test the menu reads "
        "<i>Delete (required for FTP tests)</i>.",
    ])

    s.section("Sharing workouts: export and import")
    s.p("Workouts can be shared with other riders – also with riders who use other training apps. Because "
        "every target is stored as a <b>percentage of FTP</b>, a shared workout automatically scales to the "
        "FTP of the person who imports it: a block at 90 % FTP is 225 W for a rider with 250 W FTP and 180 W "
        "for a rider with 200 W FTP.")
    s.table(["Format", "Use it for", "Contains"], [
        [C(".awt") + "<br/>(Artilody workout)", "Sharing with other Artilody Watts Trainer users, and "
         "backing up your own workouts. Recommended.",
         "Everything: name, sport, category, focus, description and every block with type, name, duration, start/end "
         "intensity, cadence and RPE. One file can hold many workouts (Appendix C)."],
        [C(".zwo") + "<br/>(Zwift workout)", "Exchanging workouts with Zwift, TrainerRoad, Golden Cheetah, "
         "MyWhoosh and other apps that read Zwift workout files.",
         "Name, description, category (as a tag) and the blocks with duration, intensity, cadence and block "
         "name. One workout per file."],
    ], [1.3, 2.1, 2.4])
    s.sub("Exporting a workout")
    s.steps([
        "Select the workout in the library (built-in workouts can be exported too).",
        f"Click {UI('Export')} in the filter bar of the Workouts page (next to {UI('Import')}) and choose "
        f"{UI('Export selected workout…')}. You can also click the share button next to {UI('Edit')} at the "
        f"bottom of the Workout Preview, or use the {UI('•••')} menu of the preview.",
        f"Choose the file type in the save dialog – {UI('Artilody workout (*.awt)')} or "
        f"{UI('Zwift workout (*.zwo)')} – and save. The file extension decides the format; the suggested "
        "file name is the workout name in your Documents folder.",
        "Send the file to a friend (e-mail, chat, USB stick …) or keep it as a backup.",
    ])
    s.p(f"{UI('Export')} → {UI('Export all my workouts…')} writes all your own workouts (not the built-in ones) into a single "
        f"{C('.awt')} file, suggested name {C('My workouts.awt')}. The entry is disabled "
        "until you have created or imported at least one workout of your own.")
    s.sub("Importing workouts")
    s.steps([
        f"Click {UI('Import')} in the filter bar of the Workouts page.",
        f"Select one or more {C('.awt')}, {C('.json')} or {C('.zwo')} files (hold Ctrl/Cmd or Shift to select "
        "several) and click Open.",
        "A message reports how many workouts were imported, how many duplicates were skipped and any "
        "warnings. The first imported workout is selected in the library.",
    ])
    s.bullets([
        "Imported workouts are <b>your own (custom) workouts</b>: they appear under <i>Custom</i>, can be edited "
        "in the Builder and can be deleted.",
        "<b>Duplicates are skipped:</b> a workout with the same name and exactly the same blocks as one already "
        "in your library is not imported again – so importing the same file twice does no harm.",
        "<b>Name clashes:</b> if a different workout with the same name already exists, the imported one is "
        "renamed to <i>Name (imported)</i>, <i>Name (imported 2)</i> and so on. Your existing workouts are "
        "never overwritten.",
        "Every file is <b>checked and cleaned</b> before import: durations are limited to 5 seconds – 6 hours "
        "per block and 12 hours per workout, intensities to 400 % FTP and cadence to 200 RPM; unknown "
        "categories are replaced by Endurance, Intervals or Recovery depending on the intensity. Adjustments "
        "are listed as warnings. Files larger than 2 MB, with more than 500 workouts or more than 2000 blocks "
        "per workout are rejected.",
    ])
    s.sub("Zwift .zwo files – what is converted")
    s.table(["In the .zwo file", "In Artilody Watts Trainer"], [
        [C("Warmup") + ", " + C("Cooldown") + ", " + C("Ramp"), "Warm Up, Cool Down and Ramp blocks (start "
         "and end power)."],
        [C("SteadyState"), "A steady block; depending on its intensity it becomes a Recovery, Steady or "
         "Interval block."],
        [C("IntervalsT"), "<b>Expanded</b> into individual blocks: e.g. 5 × (3 min on / 3 min off) becomes ten "
         "blocks (Interval, Recovery, Interval, …). Exporting it again writes the individual blocks."],
        [C("FreeRide"), "A steady block at 60 % FTP (ERG cannot follow a free-ride section)."],
        [C("MaxEffort"), "An interval block at 150 % FTP."],
        [C("Cadence") + " attribute", "Cadence target of the block."],
        [C("textevent") + " at the start of a block", "Used as the block name (other on-screen messages are "
         "ignored)."],
    ], [2.2, 3.6])
    s.note("A .zwo file cannot store everything: <b>RPE settings</b> of a block are not exported (the RPE "
           "zone is derived from the intensity again after import), and Steady, Interval and Recovery blocks "
           "are all written as <i>SteadyState</i>. Use .awt when sharing with other Artilody Watts Trainer "
           "users to keep every detail.")

    s.section("Ride mode: ERG or Free Ride")
    s.p(f"Under {UI('Ride Mode')} choose {UI('ERG Mode')} (<i>automatically match target power</i>) or "
        f"{UI('Free Ride')} (<i>follow the workout at your own pace</i>). The default comes from your profile. "
        "You can also switch ERG on or off during the ride on the Watt Training page.")

    s.section("Starting: you press Start")
    s.steps([
        f"Press {UI('Load Workout')}. The app switches to the Watt Training page with the workout loaded; a "
        f"<i>“Ready – press Start”</i> label is shown in the header. The trainer stays free, so you can clip "
        "in and spin your legs without anything starting.",
        f"Press {UI('Start')} in the Controls card (or {K('Space')}). The clock, the recording and – in ERG mode "
        "– the target power start now.",
    ])
    s.note("A session never starts by itself when you start pedaling (or rowing): pedaling a little while you "
           "clip in does not start the clock. This applies to the Watt Training, Free Ride, Video Ride and "
           "Rowing pages alike. There is no automatic pause either, and pedaling never resumes a paused "
           f"session: press {UI('Resume')} (or {K('Space')}) – the header then reads <i>“Paused – press Resume”</i>.")
    s.p(f"Until you press Start you can choose a different workout and press {UI('Load Workout')} again – it "
        "replaces the loaded one. A loaded workout never blocks the other training pages.")
    s.p(f"{UI('Ride without workout ›')} (below the Load button) loads a Free Ride without any intervals in "
        "the same way.")

    s.section("The Watt Training page in detail")
    s.sub("Header")
    s.p("The header shows a breadcrumb (<i>Watt Training › focus of the workout</i>), the workout name and a "
        "summary line such as <i>3 × 12 min @ 225 W • Sweet Spot • 56 min total</i>.")
    s.sub("Metric cards")
    s.table(["Card", "Shows"], [
        [UI("Power"), "Current power in watts. The bar is coloured by your current power zone. Footer: the "
                      "target power and your deviation from it (e.g. <i>+2 W</i>). The small <b>3 s avg</b> badge "
                      "shows that the value is the rolling 3-second average (section 15.2); click it to switch "
                      "to the raw trainer value (<i>Instant</i>) and back."],
        [UI("Cadence"), "Current cadence in RPM. Footer: the target range ±5 RPM around the interval's cadence, "
                        "or around your preferred cadence if the interval has none."],
        [UI("Heart Rate"), "Current heart rate in BPM, or “–” without data. The bar is coloured by heart-rate "
                           "zone; the footer shows the zone and its range, <i>Below zone 1</i> or <i>No sensor "
                           "data</i>."],
        [UI("Elapsed Time"), "Workout time and the total duration (<i>of 56:00</i>)."],
    ], [1.2, 4.6])
    s.sub("Workout Profile chart")
    s.p("The chart shows the target power of every interval as bars coloured by RPE zone (legend: "
        "<i>RPE zones 1, 2–3, 4–6, 7–8, 9, 10</i>), your actual power as it is recorded, and your heart-rate "
        "curve in magenta on the right-hand axis. A vertical cursor marks your position, with a label showing "
        "your current power. In a Free Ride the time axis grows in 10-minute steps.")
    s.sub("Workout Timeline")
    s.p("Below the chart, every interval is shown as a coloured block with its duration, name and target "
        "watts (a range for ramps); the current interval is highlighted. The header of the card contains:")
    s.bullets([
        f"{UI('− 100 % +')} – the <b>intensity</b> control. Each click makes all targets 5 % easier or harder, "
        "from 50 % to 150 %. The adjustment applies immediately (also to the trainer in ERG mode), is recorded "
        "in the ride's target power and is reset when you start a new ride.",
        f"{UI('Skip')} – jumps to the start of the next interval (only during a ride). The skipped time is not "
        "recorded. Skipping the last interval finishes and saves the workout.",
        f"{UI('Edit Workout')} – opens the workout in the Builder.",
        f"{UI('Add Interval')} – opens the Builder to add blocks (disabled during a ride).",
    ])
    s.p("A progress bar with elapsed and total time is shown at the bottom.")
    s.sub("Right column")
    s.bullets([
        f"{UI('Workout Details')} – opens a dialog with the description and a list of all intervals with their "
        "start time, duration and target.",
        f"{UI('Devices')} – connection status of trainer and heart-rate sensor.",
        f"{UI('ERG Mode')} switch – turns trainer control on or off at any time during a workout (disabled in a "
        "Free Ride). The text below explains the current mode.",
        f"{UI('Controls')} – {UI('Start')} / {UI('Pause')} / {UI('Resume')} and "
        f"{UI('Stop')}.",
        f"{UI('Music Player')} – see chapter 14.",
    ])

    s.section("Pause, resume and stop")
    s.p(f"{UI('Pause')} stops the clock and the recording. <b>While paused, ERG is released</b> – the trainer "
        "returns to free ride so that it does not hold a high resistance while you stand still. "
        f"{UI('Resume')} continues the workout and sends the current target to the trainer again.")
    s.p(f"{UI('Stop')} opens the <i>Finish workout?</i> dialog:")
    s.bullets([
        f"{UI('Save &amp; Finish')} – ends the ride and saves it to History.",
        f"{UI('Discard')} – ends the ride without saving.",
        f"{UI('Cancel')} – continue riding.",
    ])
    s.p("When you reach the end of a workout it is finished and <b>saved automatically</b> (<i>“Workout "
        "complete – ride saved to History”</i>). After a ride, the trainer is always returned to free ride.")
    s.warning("Rides shorter than <b>30 seconds</b> of recorded time are not saved (<i>“Ride too short to "
              "save”</i>).")
    s.sub("Keyboard shortcut")
    s.p(f"On the Watt Training page, {K('Space')} works like the Start / Pause / Resume button: it starts the "
        "loaded workout, then pauses and resumes it. The shortcut is inactive on other pages and while the profile dialog or the "
        "FTP result dialog is open.")

    s.section("Free ride without a workout")
    s.tip(f"For riding without a workout on a road with a grade (your gears or virtual gears) or a fixed ERG wattage, use the "
          f"{UI('Free Ride')} page (chapter 12).")
    s.p(f"Choose {UI('Ride without workout ›')} on the Workouts page. The Watt Training page shows <i>Free Ride – ride at "
        "your own pace without structured intervals</i>; there are no targets, ERG is unavailable and the "
        "trainer simulates a flat road. Power, cadence and heart rate are still recorded, and the ride is saved "
        "under the name <i>Free Ride</i>. A free ride runs until you press Stop.")

    s.section("What is recorded")
    s.p("Once per second the app records your <b>power</b>, <b>cadence</b>, <b>heart rate</b> and the <b>target "
        "power</b> actually requested (including any intensity adjustment; 0 in a free ride). With the ride "
        "it stores the profile, workout name, start time, number of blocks, mode (ERG or Free Ride, as set "
        "at the end of the ride) and the FTP at the start. Statistics are calculated with the FTP and zones "
        "your profile had when the ride started.")

    # =====================================================================
    s.chapter("Workout Builder",
              "Create your own workouts from blocks, or adapt an existing one. Targets are stored as a "
              "percentage of FTP, so your workouts grow with you.")
    s.figure(SHOTS / "builder.png",
             "The Workout Builder: timeline chart and block list (left), interval settings and summary (right).")
    s.section("Creating and opening workouts")
    s.bullets([
        f"<b>New workout:</b> {UI('Workouts → New Workout')}. It starts as <i>New Workout</i> (category "
        "Intervals) with three blocks: a 10-minute warm-up, a 12-minute sweet-spot interval and a 5-minute "
        "cool-down.",
        f"<b>Edit a workout:</b> double-click it in the library, choose {UI('Edit')} in the preview, or "
        f"{UI('Edit Workout')} on the Watt Training page.",
    ])
    s.p("At the top of the page you enter the <b>workout name</b> and choose the <b>category</b> (Endurance, "
        "Intervals, Recovery or FTP Tests). The <b>Cycling / Rowing</b> selector sets the sport; rowing "
        "workouts use your rowing FTP and stroke rate (SPM) instead of cadence (section 11.9).")
    s.note("A workout in the <i>FTP Tests</i> category is treated as an FTP test: when you finish it, the app "
           "estimates your FTP (chapter 8).")

    s.section("Adding blocks")
    s.p(f"The {UI('Add Block')} panel offers six presets. {UI('Add Interval')} below the block list adds a "
        "Steady block.")
    s.table(["Preset", "Inserted", "Default"], [
        ["Warm Up", "always at the start", "10:00, ramp 40 → 60 % FTP"],
        ["Steady", "after the selected block", "10:00 at 70 % FTP, cadence = your preferred cadence"],
        ["Intervals", "after the selected block",
         "a set of 3 × (5:00 <i>Interval</i> at 105 % FTP, cadence 95 + 3:00 <i>Recovery</i> at 55 %)"],
        ["Recovery", "after the selected block", "5:00 at 55 % FTP"],
        ["Cool Down", "always at the end", "5:00, ramp 55 → 35 % FTP"],
        ["Ramp", "after the selected block", "10:00, ramp 50 → 100 % FTP"],
    ], [1.0, 1.6, 3.2], swatches=[Z[0], Z[1], Z[3], Z[2], Z[0], Z[5]])
    s.p("If no block is selected, new blocks are added at the end.")

    s.section("Interval settings")
    s.p(f"Select a block in the list or click it in the timeline chart. {UI('Interval Settings')} shows its "
        "name (click to rename) and these fields, each with − / + buttons and direct input:")
    s.table(["Field", "Description"], [
        [UI("Duration"), "Type <i>mm:ss</i> (e.g. 12:00) or <i>h:mm:ss</i>; a plain number means minutes. "
                         "− / + change it by 15 s (up to 2 minutes) or 30 s. Range 5 seconds to 6 hours."],
        [UI("Target Power (W)"), "Target in watts (steps of 5 W). It is converted to % of your current FTP."],
        [UI("Start / End Power (W)"),
         "For ramps, warm-ups and cool-downs: the power at the start and at the end of the block. The target "
         "changes linearly between the two."],
        [UI("% FTP"), "The same target as a percentage of FTP (steps of 1 %, up to 400 %). Watts and % FTP are "
                      "two views of one value."],
        [UI("Cadence (RPM)"), "Optional cadence target for the block (“–” = none; the Watt Training page then uses your "
                              "preferred cadence)."],
        [UI("RPE"), "<i>Auto (from power)</i> colours the block by the power zone of its target. Choose a "
                    "fixed RPE (1 Very Easy … 10 Max Effort) to override the colour and zone shown."],
    ], [1.5, 4.3])
    s.tip("A Steady, Recovery or Interval block becomes a ramp only if it is created as one. To build a "
          "progressive block, add a Ramp preset and set its start and end power.")

    s.section("Timeline chart and block list")
    s.p(f"The {UI('Workout Timeline')} chart shows the whole workout with total duration and number of blocks. "
        f"Use {UI('View:')} to switch the vertical axis between {UI('Power (Watts)')} and {UI('% FTP')}. Click "
        "anywhere in the chart to select the block at that time.")
    s.p("Below the chart, the block list shows one card per block in workout order (left = start). Click a "
        "card to select and edit it. A hint line above the list summarises how to reorder and delete blocks.")

    s.section("Reordering and deleting blocks")
    s.p("Each block card has a <b>grip strip</b> at the top (the six-dot grip icon next to the coloured zone bar) and "
        "four small buttons at the bottom: ‹ and › on the left, copy and trash on the right. Hover over any of "
        "them to see a tooltip. There are four ways to change the order:")
    s.table(["Method", "How"], [
        ["Drag", "Point at the grip strip (the cursor becomes an open hand), press and drag the card left or "
                 "right. The card being moved is dimmed, a copy follows the pointer and a blue line with dots "
                 "shows where it will land. Release to drop it there. Near the edge of the list it scrolls "
                 "automatically."],
        [UI("‹") + " / " + UI("›"), "Moves the block one place earlier (left) or later (right). The buttons are "
                 "greyed out on the first and last block."],
        ["Right-click menu", f"Right-click a card for {UI('Move earlier')}, {UI('Move later')}, "
                 f"{UI('Move to start')}, {UI('Move to end')}, {UI('Duplicate')} and {UI('Delete interval')}."],
        ["Keyboard", "<b>Alt+←</b> / <b>Alt+→</b> (on a Mac <b>Option+←</b> / <b>Option+→</b>; the up/down arrows work too) "
                 "move the selected block earlier or later. This works when no text field has the keyboard focus."],
    ], [1.4, 4.4])
    s.p("A moved block stays selected, so you can keep pressing the arrows. To <b>delete</b> a block, click the "
        f"red trash button on its card (or choose {UI('Delete interval')} in the right-click menu). The copy "
        "button duplicates a block and inserts the copy right after it.")
    s.tip("Deleting has no undo. If you delete a block by mistake in a workout you saved earlier, open the "
          "workout again from the library without saving to get the saved version back; otherwise add the "
          "block again from the Add Block panel.")

    s.section("Summary, preview and saving")
    s.p(f"{UI('Workout Summary')} shows the average intensity (% FTP), the estimated TSS, the total duration "
        f"and the number of blocks. {UI('Preview')} opens a large chart of the workout with a "
        f"{UI('Save Workout')} button.")
    s.p(f"Click {UI('Save Workout')} to store the workout (a dot – <i>Save Workout •</i> – indicates unsaved "
        "changes). A workout needs a name and at least one block. After saving, the workout is selected in "
        "the library.")
    s.note("Built-in workouts are never overwritten. When you save changes to a built-in workout, the app "
           "saves them as a <b>new personal copy</b> and leaves the original untouched.")

    # =====================================================================
    s.chapter("FTP test",
              "The built-in Ramp FTP Test estimates your FTP in about 15–25 minutes. Repeat it every 4–8 "
              "weeks to keep your zones accurate.")
    s.section("The Ramp FTP Test protocol")
    s.table(["Block", "Duration", "Target"], [
        ["Warm Up", "5:00", "40 → 45 % FTP"],
        ["Ramp", "20:00", "50 → 150 % FTP, rising continuously by 5 % of FTP per minute"],
    ], [1.2, 1.0, 3.6])
    s.p("With an FTP of 250 W, the ramp starts at 125 W and would end at 375 W. Almost nobody reaches the end – "
        "the test is over when you can no longer hold the target.")

    s.section("How to perform the test")
    s.steps([
        "Be rested. Set up a fan, have water ready and make sure the trainer is calibrated if it requires it.",
        f"Select {UI('Ramp FTP Test')} (category <i>FTP Tests</i>). <b>ERG mode</b> is recommended: the trainer "
        "then raises the power for you.",
        f"Press {UI('Load Workout')}, clip in and press {UI('Start')} on the Watt Training page.",
        "Ride the warm-up easily, then follow the ramp. Keep a steady cadence.",
        f"When you cannot keep the cadence or the target any longer, press {UI('Stop')} and choose "
        f"{UI('Save &amp; Finish')}. If you ride to the end, the ride is saved automatically.",
    ])
    s.warning("An FTP test is a maximal effort. Do not perform it if you are ill or have health concerns, and "
              "consult a doctor if in doubt.")

    s.section("How the FTP is estimated")
    s.p("For a <b>ramp test</b> (the built-in Ramp FTP Test, any <i>FTP Tests</i> workout with “ramp” in its "
        "name, or one whose longest block is a rising ramp) the app finds your <b>best 1-minute average "
        "power</b> during the ride (rolling 60-second window) and calculates:")
    s.p('<para alignment="center"><font size="12"><b>FTP = 0.75 × best 1-minute power</b></font>'
        '<br/><font color="#5b6485">rounded to whole watts</font></para>')
    s.p("For a <b>20-minute test</b> (an <i>FTP Tests</i> workout of your own with a steady or interval block "
        "of at least 20 minutes) it uses your <b>best 20-minute average power</b>:")
    s.p('<para alignment="center"><font size="12"><b>FTP = 0.95 × best 20-minute power</b></font>'
        '<br/><font color="#5b6485">rounded to whole watts</font></para>')
    s.p("An <i>FTP Tests</i> workout that is neither a ramp nor contains a 20-minute block gives no FTP result.")
    s.p("The result is only offered when it is plausible:")
    s.bullets([
        "the ride was saved (at least 30 seconds) and contains at least <b>5 minutes</b> of recorded data;",
        "the estimate lies within the valid FTP range of <b>50–600 W</b>;",
        "the ride belongs to a workout in the <i>FTP Tests</i> category;",
        "the current profile is still the one that rode the test.",
    ])

    s.section("The “FTP Test Complete” dialog")
    s.p("After a successful test the <i>FTP Test Complete</i> dialog opens. It shows the <b>Estimated FTP</b> "
        "(“Based on 75 % of your best 1-minute power (… W)”, or 95 % of the best 20-minute power for a "
        "20-minute test) and three tiles: <b>Current FTP</b>, "
        "<b>Change</b> (in watts and percent) and <b>New W/kg</b>.")
    s.bullets([
        f"{UI('Update FTP')} – saves the new FTP to your profile. Power zones and all workout targets are "
        "recalculated immediately (<i>“FTP updated to … W – zones and workouts adjusted”</i>).",
        f"{UI('Keep current')} – closes the dialog without changes.",
    ])
    s.p("Because zones are stored as percentages of FTP, you do not need to edit them after an update.")

    s.section("Using an earlier test from History")
    s.p(f"In {UI('History')}, select an FTP-test ride and click {UI('View Details')}. For FTP tests the "
        f"details additionally show {UI('Best 1 min')} and {UI('Estimated FTP')}. Click {UI('Use as FTP')} to "
        "open the same dialog and apply that result to your profile.")

    # =====================================================================
    s.chapter("History & Analysis",
              "Every saved ride and row appears in History with a full analysis. Only rides of the current "
              "profile are shown.")
    s.figure(SHOTS / "history.png", "The History &amp; Analysis page.")
    s.section("Period and totals")
    s.p("Choose the period in the selector at the top: <i>Last 7 days</i>, <i>Last 30 days</i> (default), "
        "<i>Last 90 days</i>, <i>Last 12 months</i> or <i>All time</i>. Periods count whole days including today. "
        f"The tiles show {UI('Total Rides')}, {UI('Training Time')}, {UI('Avg Power')} and "
        f"{UI('Total Energy')} (kJ) for that period. The {UI('All')} / {UI('Cycling')} / {UI('Rowing')} "
        "selector limits the page to one sport (rows are described in section 11.8).")
    s.section("Selected ride, ride list and charts")
    s.bullets([
        f"{UI('Selected Workout')} – a chart of the selected ride with <i>Target Power</i>, <i>Actual Power</i> and "
        "<i>Heart Rate</i>, and a line with date, duration, number of blocks and mode. After a ride, the new "
        "ride is selected automatically.",
        f"{UI('Recent Workouts')} – date, workout, duration and average power. Click a row to select the ride; "
        f"{UI('View All')} / {UI('Show Less')} toggles between recent and all rides in the period.",
        f"{UI('Weekly Training Time')} – hours ridden per weekday (Monday–Sunday) within the period.",
        f"{UI('Power Zones')} – time and share of time spent in each power zone during the period.",
        f"{UI('Selected Workout Details')} – average and maximum heart rate, Normalized Power and average "
        "cadence of the selected ride.",
    ])
    s.section("Ride details")
    s.p(f"{UI('View Details')} opens a dialog with date, start time and duration, and these values:")
    s.table(["Value", "Meaning"], [
        ["Avg Power / Max Power", "Mean and highest one-second power. Seconds without pedaling count as 0 W."],
        ["Normalized (NP)", "Normalized Power – see below."],
        ["Energy", "Mechanical work in kilojoules (sum of watts per second ÷ 1000). Roughly equal to the food "
                   "energy burned in kcal."],
        ["Intensity (IF)", "Intensity Factor = NP ÷ FTP."],
        ["SPM", "Strokes per minute – the stroke rate when rowing."],
        ["TSS", "Training Stress Score – see below."],
        ["Avg / Max HR", "Mean and highest heart rate (seconds without data are ignored)."],
        ["Avg Cadence", "Mean cadence while pedaling (zero cadence is ignored)."],
        ["Best 1 min, Estimated FTP", "Only for FTP-test rides (chapter 8)."],
    ], [1.6, 4.2])
    s.p("A <b>Time in zones</b> bar shows how the ride was distributed across the six power zones. Buttons: "
        f"{UI('Delete ride')}, {UI('Use as FTP')} (FTP tests only), {UI('Export FIT')}, {UI('Export CSV')} and "
        f"{UI('Close')}.")
    s.sub("NP, IF and TSS explained")
    s.bullets([
        "<b>Normalized Power (NP)</b> estimates the power you could have held at a constant output for the same "
        "physiological cost. It is calculated from 30-second rolling averages of power, raised to the 4th power, "
        "averaged, and the 4th root taken. Rides shorter than 30 seconds use simple average power.",
        "<b>Intensity Factor (IF)</b> = NP ÷ FTP. 0.75 is an endurance ride, about 0.9 a hard interval session, "
        "1.0 equals a one-hour all-out effort.",
        "<b>Training Stress Score (TSS)</b> = duration (s) × NP × IF ÷ (FTP × 3600) × 100. One hour at FTP "
        "gives 100 TSS.",
    ])
    s.p("All three use the FTP stored with the ride (your FTP when the ride started). The Builder and the "
        "library show an <i>estimated</i> TSS calculated from the workout targets.")

    s.section("Exporting rides")
    s.p(f"Click {UI('Export')} at the top of the History page and choose:")
    s.table(["Menu item", "Result"], [
        [UI("Selected ride – FIT (Garmin, Strava, TrainingPeaks…)"),
         f"One FIT activity file, suggested name {C('ride-YYYYMMDD-HHMM-&lt;workout&gt;.fit')}"],
        [UI("Selected ride – CSV (per second)"), f"{C('ride-YYYYMMDD-HHMM-&lt;workout&gt;.csv')} (section 9.6)"],
        [UI("All rides in range – CSV summary"), f"{C('watt-history-YYYY-MM-DD.csv')}, one row per ride"],
        [UI("All rides in range – FIT files…"), "One FIT file per ride of the selected period, in a folder "
                                                "you choose"],
    ], [2.9, 2.9])
    s.p(f"The ride details dialog also has {UI('Export FIT')} and {UI('Export CSV')} buttons for the ride shown. "
        "The save dialogs open in your Documents folder.")

    s.section("FIT export: Strava, Garmin Connect, TrainingPeaks, intervals.icu")
    s.p("<b>FIT</b> is the activity file format used by Garmin devices and read by practically every training "
        "platform. Export a ride as FIT to keep your training log in one place or to share it with a coach.")
    s.sub("Uploading the file")
    s.p("Upload the .fit file through the website of your platform (menu names may change over time):")
    s.table(["Platform", "How to upload"], [
        ["Strava", f"On strava.com click the {UI('+')} button at the top right → {UI('Upload activity')} → "
                   f"{UI('File')} and select one or more .fit files."],
        ["Garmin Connect", f"On connect.garmin.com click {UI('+')} (or the upload icon) → {UI('Import Data')} "
                           "and drop the file(s) into the window."],
        ["TrainingPeaks", "Drag the file onto the day in your calendar, or click the day → Upload."],
        ["intervals.icu", "Drag the file(s) onto the calendar or the activities list, or use the upload "
                          "button."],
    ], [1.4, 4.4])
    s.p("A ride appears as an <b>indoor cycling</b> activity, a row as an <b>indoor rowing</b> activity, "
        "with its original start time, duration and "
        "per-second power, heart-rate and cadence curves. Most platforms recognise a ride that was already "
        "uploaded and reject the second upload as a duplicate.")
    s.sub("What the file contains")
    s.bullets([
        "Sport <i>cycling</i>, sub-sport <i>indoor cycling</i> (rows: sport <i>rowing</i>, sub-sport "
        "<i>indoor rowing</i>), start time and duration.",
        "For rows: the total distance and average speed, and a per-second distance estimated from the total "
        "(chapter 11).",
        "One record per second with <b>power</b>, <b>heart rate</b> and <b>cadence</b> (heart rate is "
        "left out for seconds without data).",
        "One lap covering the whole ride and a session summary: average and maximum power and heart rate, "
        "average cadence, Normalized Power, TSS, IF, the FTP (threshold power) used for the ride, total work "
        "and calories (estimated as 1 kcal per kJ of work).",
    ])
    s.sub("Limitations")
    s.bullets([
        "<b>No GPS, distance or speed</b> for rides – an indoor ride has no speed model. Platforms show a "
        "distance of 0 and no map. Rows contain distance, but no GPS or map.",
        "The workout's intervals are not written as separate laps; the whole ride is one lap.",
        "Pauses are not part of the file: it contains the recorded riding time only.",
        "Platforms often calculate NP, TSS and IF again with the FTP stored in <i>your account there</i>, so "
        "their values can differ from the ones in the app.",
        "The target power of each second is not included (use the CSV export if you need it).",
    ])
    s.sub("Exporting many rides at once")
    s.p(f"Choose {UI('All rides in range – FIT files…')}, then pick a folder. The app writes one file per ride "
        f"of the period selected at the top of the page ({UI('Last 30 days')}, {UI('All time')} …), named "
        f"{C('ride-YYYYMMDD-HHMM-&lt;workout&gt;.fit')}; files with the same name in that folder are replaced. "
        "A message reports how many files were written. Most platforms accept several files in one upload.")
    s.note("The FIT files are written by an independent encoder that is part of this app. FIT is a file format "
           "by Garmin; Artilody Watts Trainer is not affiliated with or endorsed by Garmin, Strava, "
           "TrainingPeaks or intervals.icu.")

    s.section("Exporting CSV")
    s.p("CSV files can be opened in any spreadsheet program (Excel, Numbers, LibreOffice). They are UTF-8, "
        "comma-separated, with a header row.")
    s.sub("Per-second file")
    s.p(f"Three comment lines ({C('# workout,…')}, {C('# started,…')} with ISO date/time, {C('# ftp,…')}) are "
        "followed by one row per second:")
    s.table(["Column", "Content"], [
        [C("seconds"), "Seconds since start (0, 1, 2, …)"],
        [C("elapsed"), "Elapsed time as hh:mm:ss"],
        [C("power_w"), "Power in watts"],
        [C("target_w"), "Requested target power in watts (0 in a free ride)"],
        [C("cadence_rpm"), "Cadence in RPM"],
        [C("heart_rate_bpm"), "Heart rate in BPM (0 = no data)"],
    ], [1.6, 4.2])
    s.sub("Summary file")
    s.p("One row per ride in the selected period:")
    s.table(["Column", "Content"], [
        [C("date"), "Start date and time (ISO 8601)"],
        [C("workout"), "Workout name"],
        [C("duration_s"), "Recorded duration in seconds"],
        [C("mode"), "ERG or Free Ride"],
        [C("avg_power_w") + ", " + C("max_power_w") + ", " + C("normalized_power_w"), "Average, maximum and "
                                                                                       "normalized power"],
        [C("avg_hr_bpm") + ", " + C("max_hr_bpm"), "Average and maximum heart rate"],
        [C("avg_cadence_rpm"), "Average cadence"],
        [C("energy_kj"), "Energy in kJ"],
        [C("intensity_factor"), "IF (3 decimals)"],
        [C("tss"), "Training Stress Score"],
        [C("ftp_w"), "FTP used for the ride"],
    ], [2.6, 3.2])
    s.section("Deleting rides")
    s.p(f"In the ride details, click {UI('Delete ride')} and confirm. The ride and all its recorded data are "
        "removed permanently. Deleting a workout from the library does <i>not</i> delete its rides.")

    # =====================================================================
    s.chapter("Workout calendar",
              "Plan your training weeks ahead: put cycling and rowing workouts, Free Rides and Video Rides on "
              "the days you want to do them, move them around when life happens, and start them straight from "
              "the calendar.")
    s.figure(SHOTS / "calendar.png",
             "The Calendar page: month view with planned and completed workouts (left), the selected day "
             "and the upcoming workouts (right).")
    s.section("The month view")
    s.p(f"Click {UI('Calendar')} in the sidebar. The calendar shows one month as a grid of weeks (Monday to "
        f"Sunday). Use the arrow buttons to go to the previous or next month and {UI('Today')} to jump back to "
        "the current month. Next to the month name a summary shows how many workouts are planned in the month, "
        "how many are done and the total planned time, for example <i>22 planned • 12 done • 17 h 48</i>.")
    s.bullets([
        "Every planned entry is shown as a small chip with an icon, its name and duration. Each kind has its "
        "own colour (see the legend at the top right): cycling workouts blue, rowing cyan, <b>Free Ride</b> "
        "green, <b>Video Ride</b> purple. A Video Ride whose route is no longer in the video library shows a "
        "warning sign and <i>Route not found</i>.",
        "Completed workouts are dimmed, crossed out and marked with a tick and <i>done</i>.",
        "Today has a filled circle around the date. If a day holds more workouts than fit, the last line "
        "reads <i>+N more</i> – select the day to see all of them in the side panel.",
        "Hold the pointer over a chip to see the details (duration; route distance and climbing, the workout "
        "and the resistance mode of a Video Ride) and what clicking does.",
    ])
    s.section("Adding a workout")
    s.steps([
        f"Double-click a day, or click the {UI('+')} button that appears in the corner of a day when you point "
        f"at it. {UI('Add')} at the top right and {UI('+')} in the side panel add to the selected day. "
        "A single click only selects the day.",
        f"The {UI('Add to Calendar')} dialog opens for that date on the {UI('Workout')} tab (the arrows next to "
        f"the date change the day). Type in {UI('Search workouts...')} and use the "
        f"{UI('Cycling')} / {UI('Rowing')} filter to find the workout.",
        f"Optionally enter {UI('Notes')} (for example <i>“Outdoor if dry”</i>).",
        f"Click {UI('Add to Calendar')}. Double-clicking a workout in the list adds it right away.",
    ])
    s.p(f"You can also plan from the Workouts page: choose {UI('Add to calendar…')} in the "
        f"{UI('•••')} menu of the Workout Preview (or the calendar button), then pick the date. The same workout "
        "can be planned on several days, and a day can hold several workouts.")
    s.section("Planning a Free Ride or a Video Ride")
    s.p(f"The tabs at the top of the {UI('Add to Calendar')} dialog switch between {UI('Workout')}, "
        f"{UI('Free Ride')} and {UI('Video Ride')}:")
    s.bullets([
        f"<b>{UI('Free Ride')}</b> – the planned <b>duration</b> (a value in minutes or one of the buttons "
        f"30 / 45 / 60 / 90 / 120 min; {UI('Open')} = no planned time) and the <b>resistance</b>: "
        f"{UI('Ride')} (the road: virtual gears or a gradient, chapter 12) or {UI('ERG')} with the target "
        "watts. The duration is a target, not a limit – the Free Ride page shows <i>Planned 60 min • 23:10 "
        "left</i> in its Time tile and nothing stops when the time is up.",
        f"<b>{UI('Video Ride')}</b> – a <b>route</b> from your video library (name, distance, climbing ↑, video "
        f"length and country; if the list is empty, put route folders into the library folder shown there or add "
        f"folders on the Video Ride page, then press {UI('Rescan')}), the resistance {UI('Simulation')} with a "
        f"<b>difficulty</b> or {UI('ERG')} with the watts, and optionally "
        "<b>" + UI("Ride a workout during the video") + "</b>: "
        f"one of your cycling workouts, with or without {UI('Workout ERG')} (chapter 13). "
        "Double-clicking a route adds it right away.",
    ])
    s.p(f"To change a planned Free Ride or Video Ride later, press the pencil in the side panel or choose "
        f"{UI('Edit…')} in its menu: the same dialog opens with the entry's settings, notes and date "
        f"({UI('Save')}). Choosing another route replaces the entry by a new one on the same day.")
    s.section("Starting a planned workout")
    s.p("A single click on a chip selects it and its day. To start it, <b>double-click</b> the chip, press "
        f"{UI('Ride')} or {UI('Row')} for the entry in the side panel (or the play button in "
        f"{UI('Upcoming')}), or choose {UI('Ride this workout')} / {UI('Row this workout')} in its menu:")
    s.bullets([
        f"a <b>cycling</b> workout is loaded on the {UI('Watt Training')} page – choose ERG or Free Ride and "
        f"press {UI('Start')} as usual (chapter 6);",
        f"a <b>rowing</b> workout is set up as a <b>Watt Plan</b> on the {UI('Rowing')} page (chapter 11) – "
        f"press {UI('Start')} to begin;",
        f"a <b>Free Ride</b> opens the {UI('Free Ride')} page with the planned mode, ERG watts and duration "
        "(chapter 12);",
        f"a <b>Video Ride</b> opens the {UI('Video Ride')} page with the route, the resistance mode and the "
        "workout loaded (chapter 13). If the route is missing, a message says <i>Route not found</i>.",
    ])
    s.p("If a ride or a row is already running, the app shows a message and takes you to it instead. Finish "
        "it first.")
    s.note("When you <b>complete</b> a workout you started from the calendar, exactly that plan entry is "
           "<b>marked as completed automatically</b> and linked to the ride – even if it was planned for another "
           "day. A completed workout started elsewhere (Workouts page, Watt Training) ticks off the first open entry "
           "of that workout on the same day. Rides you stop before the end of the workout, aborted rows, free "
           "rides and free rows do not change the calendar. If you delete the ride in History, its plan entry "
           "is opened again. A <b>Free Ride</b> entry started from the calendar is ticked off by a saved Free "
           "Ride of at least half the planned time (10 minutes without a planned time); a <b>Video Ride</b> "
           "entry by a saved ride on that route that reached the finish or 90 % of the route distance.")
    s.section("Moving, completing and removing")
    s.p("Right-click a chip (on touch screens: press and hold it) to open its menu:")
    s.table(["Menu item", "Effect"], [
        [UI("Ride this workout") + " / " + UI("Row this workout") + " / " + UI("Start Free Ride") + " / "
         + UI("Start Video Ride"), "Starts the entry, like double-clicking the chip."],
        [UI("Edit…"), "Free Ride / Video Ride: changes duration, mode, watts, route, workout and notes."],
        [UI("Mark as completed") + " / " + UI("Mark as not completed"), "Ticks the entry off by hand – for "
         "example after an outdoor ride – or opens it again."],
        [UI("Move to…"), "Opens a date picker; the entry moves to the chosen day."],
        [UI("Move to next day"), "Moves the entry one day forward."],
        [UI("Show day"), "Selects the day in the side panel."],
        [UI("Remove from calendar"), "Asks for confirmation, then deletes the plan entry. The workout itself "
         "and any recorded ride are not affected."],
    ], [2.3, 3.5])
    s.p("<b>Drag and drop:</b> press a chip and drag it to another day of the visible month – the target day "
        "is highlighted – and release it there.")
    s.section("Side panel: selected day and Upcoming")
    s.bullets([
        f"The upper card shows the selected day (<i>Today</i> or the date) with every entry planned for it: "
        f"name and details (sport, category and duration; for a Free Ride the planned time and mode; for a "
        f"Video Ride distance, climbing, duration and workout), and buttons for {UI('Ride')}/{UI('Row')}, mark "
        f"as completed, edit (Free Ride / Video Ride), move and remove. {UI('+')} adds another entry to the day.",
        f"{UI('Upcoming')} lists the next open workouts from today onwards (<i>Today</i>, <i>Tomorrow</i>, "
        "then the date). Click a row to select its day, double-click it or press the play button to start it.",
    ])
    s.tip("The plan belongs to the current profile, like the ride history. Each rider in a shared installation "
          "has a separate calendar.")

    # =====================================================================
    s.chapter("Rowing with a Concept2 PM5",
              "Connect a Concept2 rowing machine with a PM5 monitor over Bluetooth and row a distance, follow a "
              "watt plan, or race a ghost of your personal best. Rows are saved to History like rides.")
    s.figure(SHOTS / "rowing-ghost.png",
             "The Rowing page during a 1000 m Ghost Race: live metrics, the power/pace/heart-rate chart over "
             "distance, the race course with your boat and the ghost, and the Session panel with your personal "
             "bests.")
    s.section("Connecting the PM5")
    s.steps([
        "Sit on the rower and wake the PM5 by pressing any button.",
        f"On the PM5 choose {UI('More Options')} → {UI('Turn Wireless ON')} → {UI('Connect Device')}. The "
        "monitor now advertises itself over Bluetooth LE (it shows a waiting screen).",
        f"In the app open {UI('Devices')} and press {UI('Scan for Devices')}. The PM5 appears in the section "
        f"{UI('Rowers (Concept2 PM5)')} with a name such as <i>PM5 430012345 Row</i>.",
        f"Click {UI('Connect')}. Once connected the row shows live power, pace /500 m and stroke rate, and the "
        "PM5 display switches to the connected state.",
    ])
    s.p("The rower is remembered like a trainer and reconnected automatically next time (if "
        f"{UI('Auto-connect known devices')} is on) – just turn wireless on at the PM5 again. A trainer, a "
        "heart-rate sensor and a rower can be connected at the same time. Heart rate comes from the connected "
        "heart-rate sensor (Devices → Heart Rate Sensors) if there is one – a dedicated strap always wins – "
        "otherwise from a strap paired with the PM5.")
    s.p(f"{UI('Virtual Rower')} (Simulated Concept2 PM5) in the same section lets you try every rowing feature "
        "without a rower. It rows by itself at around 175 W and follows the target in a Watt Plan.")
    s.note("Only the <b>PM5</b> on a <b>rower</b> (RowErg and other Concept2 rowing machines) is supported. "
           "BikeErg and SkiErg are <b>not supported</b> – they advertise the same way, but the app only handles "
           "rowing data. PM3/PM4 monitors have no Bluetooth. The PM5 accepts one "
           "Bluetooth app at a time – disconnect ErgData or other apps first.")

    s.section("Rowing machine not found")
    s.bullets([
        f"The PM5 only advertises while it shows the {UI('Connect Device')} screen. Open it "
        f"({UI('More Options')} → {UI('Turn Wireless ON')} → {UI('Connect Device')}), then press "
        f"{UI('Scan for Devices')}. A scan runs for 60 seconds.",
        "The PM5 can be connected to <b>one app at a time</b>. Disconnect ErgData, the Concept2 app or any phone "
        "or watch that is connected to it (turning off Bluetooth on the phone is the quickest way).",
        "Current PM5 firmware advertises the standard Bluetooth fitness-machine service (FTMS, "
        + C("0x1826") + ") with the machine type <i>Rower</i>. The app reads this advertisement and lists the PM5 "
        f"under {UI('Rowers')}, even before its name arrives. Older app versions could list it under "
        f"{UI('Smart Trainers')}. If a rowing machine is still listed there, press the rowing button next to it "
        f"({UI('Connect as rower')}). A device connected as trainer that turns out to offer rowing data is moved "
        "to the rowers automatically and remembered.",
        "If the PM5 does not offer the Concept2 rowing service, the app uses the standard FTMS rower data "
        "instead. Other FTMS rowing machines work the same way.",
        f"Turn on {UI('Show all devices')} in {UI('Available Devices')}. The section "
        f"{UI('Other Bluetooth devices')} lists every Bluetooth LE device the app did not recognise, with name, "
        f"address and signal. If your PM5 is there, press {UI('Connect as rower')}. If the device has no Concept2 "
        "rowing service or FTMS rower data, a red message says so.",
        f"Press {UI('Details…')} under {UI('Bluetooth Diagnostics')}. The report lists the adapter, the Qt "
        f"version, every device seen (name, address, signal, service UUIDs, classification) and any scan errors. "
        "After a connection attempt it also lists the GATT services and characteristics the device offered. "
        f"{UI('Copy')} puts it on the clipboard for a bug report. A PM5 shows the service "
        + C("CE060000-43E5-11E4-916C-0800200C9A66") + " or an <i>FTMS advertisement</i> with type <i>Rower</i>.",
        "Scan errors (Bluetooth off, adapter error, missing permission) are shown in red at the top of the device "
        "list. If Bluetooth is off, the app tries to switch it on.",
        "Update the PM5 firmware with Concept2 Utility if the monitor is old.",
        "Stroke rate missing while power, pace and distance work: the stroke rate comes from the PM5's "
        "<i>Additional Status 1</i> (0x0032). Since 1.1.4 the app re-enables that channel or moves it to the "
        "multiplexed channel when it stays silent, and otherwise computes the stroke rate from the time between "
        "strokes (shown with a ≈ sign on the Devices page). The <i>[Rower data]</i> section of the diagnostics "
        "report shows the packet counters and raw bytes per characteristic – please include it in a bug report.",
    ])
    s.p("<b>Raspberry Pi / Linux:</b> check the Bluetooth stack in a terminal:")
    s.code("bluetoothctl show            # Powered: yes\n"
           "bluetoothctl power on        # if it says no\n"
           "sudo rfkill unblock bluetooth\n"
           "bluetoothctl scan le         # lists \"PM5 430...\" (Ctrl+C stops)\n"
           "groups                       # should include \"bluetooth\"")
    s.p("If " + C("bluetoothctl scan le") + " does not list the PM5 either, the PM5 is not advertising (not on the "
        "Connect screen, or connected to another device). If it lists the PM5 but the app does not, start the app "
        "from a terminal and send the output together with the diagnostics report:")
    s.code("QT_LOGGING_RULES=\"qt.bluetooth*=true\" artilody-watts-trainer 2>&1 | tee ~/artilody-bt.log")
    s.p("Lines starting with " + C("artilody.ble:") + " show every device the app saw and how it was classified.")

    s.section("The Rowing page")
    s.p(f"Open {UI('Rowing')} in the sidebar. If no rower is connected, the Session panel shows "
        f"<i>No rower connected</i> and an {UI('Open Devices')} button.")
    s.table(["Card", "Shows"], [
        [UI("Power"), "Current power in watts; below the average power (in a Watt Plan: the target)."],
        [UI("Distance"), "Metres rowed – of the target distance with a progress bar, or with strokes and calories."],
        [UI("Pace"), "Current pace per 500 m; below the average pace (in a Ghost Race: the ghost's pace)."],
        [UI("Stroke Rate"), "Strokes per minute (SPM) and the average."],
        [UI("Heart Rate"), "Current and average heart rate."],
        [UI("Elapsed"), "Rowing time; for a distance the estimated finish time at the current average pace, in a "
                         "Watt Plan the time left."],
    ], [1.3, 4.5])
    s.p(f"The {UI('Power, Pace &amp; Heart Rate')} chart plots your power as bars and pace and heart rate as "
        "curves – over distance in Free Row and Ghost Race, over time in a Watt Plan. The bottom card depends "
        "on the mode (below). The sidebar shows a small <i>Rowing</i> status box with time and heart rate "
        "while you row on other pages.")

    s.section("Modes")
    s.p(f"Choose the mode in the {UI('Session')} panel. The mode and its settings can be changed while no row "
        "is running.")
    s.sub("Free Row")
    s.p(f"Row a distance at your own pace, without targets. Pick the {UI('Workout Distance')}: "
        f"{UI('Open (no distance)')}, 500 m, 1000 m, 2000 m, 5000 m, 6000 m, 10 000 m or {UI('Custom…')} "
        "(100 – 42 195 m). The bottom card shows a course with your boat moving towards the finish line, the "
        "metres rowed, the metres to go and the number of strokes. A distance row ends automatically at the "
        f"finish line; an open row runs until you press {UI('Finish')}.")
    s.figure(SHOTS / "rowing-solo.png", "Free Row over 1000 m: your boat on the course.", width_frac=0.82)
    s.sub("Watt Plan")
    s.p(f"Row a structured <b>rowing workout</b> from your library (choose it under {UI('Rowing Workout')}; "
        f"{UI('New Rowing Workout')} opens the Builder). Targets are a percentage of your <b>rowing FTP</b>. "
        "The bottom card shows the interval blocks of the workout coloured by zone, with a rower marker moving "
        "along a line beneath them, and these values:")
    s.table(["Value", "Meaning"], [
        [UI("Target"), "Target power of the current second."],
        [UI("Current"), "Your power – green within ±10 % of the target, amber below, red above."],
        [UI("Interval left"), "Time left in the current interval."],
        [UI("Next target"), "Target of the next interval (<i>Finish</i> in the last one)."],
        [UI("Interval avg"), "Your average power in the current interval."],
        [UI("Compliance"), "Share of the seconds with a target in which you were within ±10 % of it "
                           "(green from 80 %, amber from 50 %)."],
    ], [1.4, 4.4])
    s.figure(SHOTS / "rowing-plan.png", "A rowing Watt Plan: interval blocks and target vs. actual power.",
             width_frac=0.82)
    s.warning("The PM5 has <b>no ERG mode</b>: the rower cannot set the resistance for you. You follow the "
              "target yourself by changing how hard and how fast you pull. The workout runs on time and "
              "finishes (and is saved) automatically at its end.")
    s.sub("Ghost Race")
    s.p(f"Race against your own <b>personal best</b>. Choose the {UI('Race Distance')} – 500 m, 1000 m, 2000 m, "
        f"5000 m, 6000 m, 10 000 m or {UI('Custom…')}; the list shows your current best for each distance. A "
        "pink <b>ghost boat</b> replays your record row metre by metre next to your own boat. The header shows "
        "both distances and the gap, for example <i>+7 m ahead (+2.0 s)</i> in green or <i>−4 m behind</i> in "
        "red; the gap in seconds is how much earlier or later you reached your current distance than the "
        "ghost did.")
    s.p("Without a record for the distance there is no ghost yet (<i>No record yet – set your first time</i>) – "
        "row the distance once and that row becomes your ghost.")

    s.section("Personal bests")
    s.bullets([
        "A personal best is saved when you <b>complete</b> a distance in a <b>Ghost Race</b> or a <b>Free "
        "Row</b> with a set distance, and your time is faster than the previous best. The whole row (distance "
        "over time) is stored, so the next ghost rows exactly like you did.",
        f"After the finish the summary shows your time and, for a new record, <i>New personal best!</i> with "
        "the improvement over the old record.",
        f"The {UI('Personal bests')} table in the Ghost Race panel lists time and pace /500 m for the standard "
        "distances and every custom distance you have a record for.",
        "Open rows, Watt Plans and rows you finish early never change your records. A time is also not "
        "saved as a record if you <b>paused</b> during the piece or the time is <b>implausible</b> (faster "
        "than is physically possible, e.g. after a monitor glitch). Records belong to the current profile.",
    ])

    s.section("Starting, pausing and finishing")
    s.bullets([
        "Opening the Rowing page sets the row up (<i>Ready – press Start</i>). Strokes on the rower never "
        "start it – take a few warm-up strokes or get settled first.",
        f"{UI('Start')} (or {K('Space')} once a rower is connected) starts the row; without any setup it starts "
        "an open Free Row. Metres and strokes rowed before Start do not count.",
        f"{UI('Pause Row')} / {UI('Resume')} (or {K('Space')}) stops and continues the clock; distance rowed "
        "during a pause is not counted, and rowing on never resumes the row by itself "
        "(<i>Paused – press Resume</i>).",
        f"{UI('Finish')} asks <i>Finish row?</i>: {UI('Save &amp; Finish')} saves the row, {UI('Discard')} "
        "throws it away.",
        f"{UI('Reset')} (when paused or finished) discards an unfinished row and prepares the same session "
        f"again. After a row, {UI('Row Again')} starts the same session and {UI('View in History')} opens the "
        "saved row.",
    ])
    s.p("As with rides, rows shorter than 30 seconds are not saved. Values from the monitor are taken relative "
        "to the start of the session, so it does not matter what the PM5 showed before.")

    s.section("Rowing FTP")
    s.p(f"Every profile has a separate {UI('Rowing FTP')} (50 – 600 W, default 150 W) in the {UI('User Profile')} "
        "dialog, next to the cycling FTP. Rowing workouts, the Builder in rowing mode and the zones and TSS of "
        "rows use it; cycling uses the normal FTP. Rowing power is usually much lower than cycling power for "
        "the same effort, which is why the two are kept apart.")
    s.tip("No rowing FTP yet? A common estimate is 95 % of the average watts of an all-out 20-minute row, or "
          "the average watts of a hard but steady 30-minute row. Update it every few weeks.")

    s.section("Rows in History and export")
    s.p(f"Saved rows appear in {UI('History')} with a rowing icon, the sport <i>Rowing</i> and the distance "
        "rowed (e.g. <i>2,002 m rowed</i>); the cadence tile shows the stroke rate in SPM. Use the "
        f"{UI('All')} / {UI('Cycling')} / {UI('Rowing')} filter at the top to show one sport only – the totals, "
        "charts and zones follow the filter. Free rows are named <i>Free Row</i> or <i>Row 2000 m</i>, ghost "
        "races <i>Ghost Race 1000 m</i>, Watt Plans after the workout.")
    s.p("Rows can be exported as FIT and CSV like rides (section 9.4).")
    s.note("The FIT file of a row is marked as an <b>indoor rowing</b> activity (sport <i>rowing</i>, "
           "sub-sport <i>indoor rowing</i>) and contains the distance rowed, the average speed, power, heart "
           "rate and the stroke rate (as cadence, in strokes per minute). The PM5 reports only the total "
           "distance, so the distance curve inside the file is estimated: the total is spread over the seconds "
           "in proportion to the cube root of the power (on an air-braked rower the speed grows with the cube "
           "root of the power), and it ends exactly at the distance rowed."),

    s.section("Sport type: cycling and rowing workouts")
    s.p("Every workout has a sport – <b>Cycling</b> or <b>Rowing</b>:")
    s.bullets([
        f"<b>Workouts page:</b> cards show a Cycling or Rowing badge, and the filter {UI('All sports')} / "
        f"{UI('Cycling')} / {UI('Rowing')} at the top limits the library to one sport. For a rowing workout "
        f"the preview shows {UI('Row Workout')} instead of Load Workout; it opens the workout as a Watt Plan "
        f"on the Rowing page. {UI('New Workout')} asks for {UI('New cycling workout')} or "
        f"{UI('New rowing workout')}. Watt values on rowing cards use your rowing FTP.",
        f"<b>Builder:</b> the {UI('Cycling')} / {UI('Rowing')} selector at the top sets the sport. In rowing "
        f"mode, watts and {UI('% Rowing FTP')} are based on your rowing FTP, and the cadence field becomes "
        f"{UI('Stroke Rate (SPM)')}. Switching the sport keeps the blocks (all targets are percentages).",
        "<b>History:</b> the sport filter and the distance of rows (above).",
        f"<b>Sharing:</b> {C('.awt')} files keep the sport (Appendix C). Zwift {C('.zwo')} files have no "
        "rowing type: workouts are always exported and imported as cycling workouts.",
    ])
    s.p("All rides and workouts from earlier versions are cycling. The database is upgraded automatically "
        "the first time version 1.1.0 starts; nothing is lost.")

    # =====================================================================
    s.chapter("Free Ride & virtual shifting",
              "Ride without a workout: a road with a grade that you climb like outdoors – shifting your bike or "
              "virtual gears – or a wattage that ERG holds for you.")
    s.figure(SHOTS / "freeride.png",
             "The Free Ride page in Ride mode with a Zwift Click: live metrics, the current gear (12 / 24, "
             "ratio 2.40) with the shifting status line, small Easier / Harder buttons, the grade control and "
             "the power, speed and heart-rate chart.")
    s.section("Modes")
    s.table(["Mode", "What happens"], [
        [UI("Ride"), "A simulated road with the grade you set: climbs get harder like outdoors. Without a Zwift "
                     "Click you shift <b>your bike's gears</b>: the trainer gets the grade (gradient simulation) and "
                     "your speed follows your power. With a <b>Zwift Click</b> connected you shift Zwift's 24 "
                     "virtual gears (Click, on-screen buttons or keys) – through the Zwift protocol or an FTMS "
                     "gradient, automatically (section 5.5); a harder gear means more speed and more watts at the "
                     "same cadence."],
        [UI("ERG"), "The trainer holds the target wattage regardless of cadence, as long as you like."],
    ], [1.0, 4.8])
    s.p("Choose the mode with the selector at the top right of the page – it is the only ERG switch: selecting "
        "ERG sends the target to the trainer at once – also before the ride is started, as long as the Free Ride "
        "page is open and a trainer is connected (the status pill then shows <i>ERG on</i>) – and selecting Ride "
        "releases it (with virtual gears, Ride gives the trainer the resistance of your virtual gear right away; "
        "the pill shows the gear, e.g. <i>Gear 12/24</i>). While paused the trainer stays free and ERG returns with Resume. You can change the mode "
        "at any time; switching to ERG continues at the power you are riding at that moment (3 s average), "
        "otherwise at the last target (the first time 60 % of your FTP).")
    s.note("A trainer that cannot simulate a gradient cannot make a climb harder by itself. In Ride mode the "
           "app then uses virtual gears automatically (via ERG) and says so on the page.")

    s.section("The page")
    s.bullets([
        "<b>Metric tiles</b> – power (with the ERG target or the power the current gear needs; the <b>3 s</b> "
        "badge shows the rolling average and toggles it, section 15.2), speed (average), "
        "cadence (current gear), heart rate (% of max), time (average power; for a Free Ride started from the "
        "calendar with a planned duration: <i>Planned 60 min • 23:10 left (61 %)</i>, a target only) and distance "
        "(energy in kJ).",
        f"<b>Road panel</b> (Ride) – the grade control on the right; on the left either the current virtual gear "
        f"with {UI('Easier')} / {UI('Harder')} or, with your bike's gears, <i>Shift on your bike</i> with the "
        f"grade sent to the trainer and {UI('Virtual gears: connect a Zwift Click')}. The header shows the "
        "status line (<i>Virtual shifting: Zwift protocol</i>, <i>Virtual shifting: FTMS gradient</i> or "
        "<i>Shifting: your bike's gears</i>). In ERG the panel holds the target (below).",
        f"<b>Chart</b> – power (yellow area), speed (cyan) and heart rate (pink) over time; in ERG a dashed "
        "line marks the target.",
        f"<b>Right column</b> – {UI('Start')}, {UI('Pause Ride')} / "
        f"{UI('Resume')}, {UI('Finish')} and {UI('Reset')}; the {UI('Shifting')} card with the Zwift Click "
        f"status, its buttons as you press them and a link to the Devices page. Without a trainer a "
        f"<i>No trainer connected</i> card with {UI('Open Devices')} appears.",
    ])

    s.section("Shifting and the grade")
    s.p(f"With a Zwift Click connected the current gear is shown large – for example <b>12 / 24</b> with "
        "<i>Gear 12/24 (2.40)</i> – with a position bar and the shifting status line. "
        f"{UI('Easier')} (−) and {UI('Harder')} (+), {K('↓')} / {K('↑')} or the Zwift Click shift one gear; "
        "hold a button to shift repeatedly; Shift + ↑ / ↓ jumps three gears. There is nothing to set up.")
    s.p(f"The <b>grade</b> control (−15 % to +20 %, {UI('−')} / {UI('+')} in 0.5 % steps, a slider and "
        f"{UI('Flat')}) simulates a climb or descent: uphill you need more power for the same speed, so you "
        "shift easier – just like outdoors. With virtual gears the app shows the power the current gear needs "
        "at your cadence.")
    s.p("<b>Soft start:</b> when the ride starts (and after more than 5 s without cadence) the trainer gets "
        "0 % until the flywheel turns – 40 rpm or 10 km/h for 2 s – and then the grade rises to the set value "
        "over 4 s. While you stand or coast with virtual gears on the FTMS gradient path the trainer gets 0 %, "
        "not the road grade. ERG is never affected.")
    s.tip("Ride at your usual cadence and use the gears like outdoors: shift harder to go faster, easier when "
          "the road gets steeper. Shifting never changes the grade or the difficulty.")

    s.section("ERG mode")
    s.figure(SHOTS / "freeride-erg.png",
             "ERG mode: target 200 W with step buttons, exact entry and the target line in the chart.",
             width_frac=0.9)
    s.bullets([
        f"The <b>target</b> is shown large; below it the actual power and the difference (green within ±5 %), "
        "and a bar with the marker of your current power.",
        f"Step buttons {UI('−25')} {UI('−10')} {UI('−5')} {UI('+5')} {UI('+10')} {UI('+25')} (hold to repeat), "
        f"or type a value under {UI('Exact target')} and press {UI('Set')} or Enter (25–1500 W).",
        "The Zwift Click changes the target by 10 W; the keyboard by 5 W (↑ / ↓) or 10 W (Page Up / Page Down).",
        f"There is no separate ERG switch: ERG is on exactly while {UI('ERG')} is selected at the top of the "
        f"page. Choose {UI('Ride')} to release the target, or {UI('Pause Ride')} to free-wheel for a moment.",
    ])

    s.section("Starting, pausing and finishing")
    s.bullets([
        f"{UI('Start')} (or {K('Space')}) starts the ride – the only way it starts: pedaling never starts it, "
        "so you can clip in and spin up calmly (the page says <i>Ready – press Start</i>). The resistance of the "
        "selected mode (ERG target, grade, virtual gear) already applies while the page is open.",
        f"{UI('Pause Ride')} (or {K('Space')}) stops the clock and releases the trainer; {UI('Resume')} "
        "(or Space) continues – pedaling on never resumes it.",
        f"{UI('Finish')} asks <i>Finish free ride?</i> – {UI('Save &amp; Finish')} saves the ride to History "
        f"under the name <i>Free Ride</i>, {UI('Discard')} throws it away. A summary (time, distance, average and "
        f"maximum power, average speed, energy) and {UI('View in History')} appear; {UI('Ride Again')} starts a "
        f"new ride and {UI('Reset')} clears the page.",
        "Only one session runs at a time: a free ride cannot start while a workout, a video ride or a row is "
        "in progress (running, or paused) on another page. Start is then greyed out and a yellow card says "
        f"why, with {UI('Go to …')}, {UI('End &amp; Save')} and {UI('Discard')} (click twice to confirm). "
        "Prepared (not started) and finished sessions never block.",
    ])
    s.warning("Rides shorter than <b>30 seconds</b> are not saved.")

    s.section("Zwift Click and keyboard")
    s.table(["Input", "Ride", "ERG"], [
        ["Zwift Click +", "Shift harder", "Target +10 W"],
        ["Zwift Click −", "Shift easier", "Target −10 W"],
        [K("↑") + " or " + K("+") + " / " + K("↓") + " or " + K("−"), "Shift harder / easier (virtual gears)",
         "Target ±5 W"],
        [K("Shift") + "+" + K("↑") + " / " + K("↓"), "Jump 3 gears harder / easier", "–"],
        [K("Page Up") + " / " + K("Page Down"), "Grade +0.5 % / −0.5 %", "Target +10 W / −10 W"],
        [K("Space"), "Start / pause / resume", "Start / pause / resume"],
    ], [1.6, 2.6, 1.6])
    s.p("Virtual gears are on exactly while a Zwift Click is connected. The keys work only on the Free Ride page and not while you type in a field or a dialog or "
        f"menu is open; {K('Enter')}, {K('Esc')} or a click on the page background leaves a text field. "
        "Without virtual gears and outside ERG, the arrow keys do nothing.")

    s.chapter("Real Life Video",
              "Ride filmed routes: the video plays at the speed you ride, the trainer makes the climbs harder "
              "like on the road, and the route's elevation profile shows where you are.")
    s.figure(SHOTS / "video-ride.png",
             "The Video Ride page with a workout (Sweet Spot 3 × 12, Workout ERG): live metrics at the top, the "
             "route video with the overlays in the middle, the workout's blocks with the elapsed-time cursor and "
             "below them the elevation profile with your position; the Workout card on the right. "
             "Video: cycling.vangestel.online, CC BY-NC-SA 4.0.")
    s.section("Route folders")
    s.p("A Real Life Video (RLV) route is a folder with three files that share a name:")
    s.table(["File", "Content"], [
        [C(".mp4"), "The video, filmed from the bike. H.264 plays everywhere; see <i>Video formats</i> below."],
        [C(".gpx"), "The GPS track with elevation – distance, gradient and the profile are computed from it."],
        [C(".xml"), "Optional route file (common RLV format): title, country, the video's start and end frame "
                    "and the on-screen texts at their distances (e.g. <i>Mount Glandasse in the background</i>)."],
    ], [1.0, 4.8])
    s.p(f"Put route folders into your videos folder – by default {C('Documents/Artilody Videos')} in your home "
        f"folder – for example {C('Artilody Videos/FR_Le_petit_Menglonnais/FR_Le_petit_Menglonnais.mp4')} "
        f"(plus the {C('.gpx')} and {C('.xml')} next to it). The folder button next to the route list opens a "
        f"menu: {UI('Add Folder…')} adds another folder (e.g. on an external drive; a folder can hold many "
        f"route folders or be a route folder itself), {UI('Choose Videos Folder…')} replaces the videos "
        f"folder, {UI('Open Videos Folder')} shows it in Finder / Explorer and {UI('Rescan Library')} looks for "
        "new routes. With an empty library the page explains the layout and offers the same buttons.")

    s.section("Choosing a route")
    s.p("The drop-down at the top of the page lists every route with its name in bold and below it the "
        "distance, the climbing, the length of the video and the country, e.g. <i>12.0 km · 142 m ↑ · 29:43 · "
        "FR</i>. Routes that cannot be loaded are shown greyed out with the reason. Choosing a route shows its "
        "first frame and its profile; the app remembers the last route. The route cannot be changed during a "
        "ride.")

    s.section("The page")
    s.bullets([
        "<b>Metric tiles</b> – power (3 s average; footer: the ERG target or the power the current virtual gear "
        "needs), cadence (gear or mode), "
        "heart rate (% of max), speed (average) and elapsed time (distance ridden and energy).",
        "<b>Video</b> – plays at your speed: the playback rate follows your riding speed relative to the speed "
        "of the camera (about 0.25× to 2.5×); it pauses when you stop or coast to a standstill, and a video that "
        "drifts from your position jumps back in sync. Overlays show the ride status, the gradient you ride "
        "(coloured; with the route's gradient below it when the difficulty is below 100 %), the resistance mode "
        "(gear or ERG target), the difficulty, the "
        "playback rate and the route's on-screen texts. The speaker button turns the sound on – RLV videos are "
        "usually silent, so it starts muted.",
        "<b>Distance line</b> – the elevation profile of the whole route, coloured by gradient (green "
        "descents, blue flat, yellow/orange climbs, red steep), the ridden part strong, your position with "
        "<i>4.3 / 12.0 km</i>, km marks and small ticks where the route has texts. Above it: distance, "
        "remaining distance, elevation, climbing done and gradient. Before the start, click the profile to "
        "start part-way along the route (hovering shows the distance and elevation).",
        f"<b>Right column</b> – {UI('Start')}, {UI('Pause Ride')} / "
        f"{UI('Resume')}, {UI('Finish')} and {UI('Reset')}; the {UI('Resistance')} card; the Zwift Click status. "
        f"Without a trainer a <i>No trainer connected</i> card with {UI('Open Devices')} appears.",
    ])

    s.section("Resistance and difficulty")
    s.p(f"The {UI('Resistance')} card offers two modes; the choice and the ERG target are remembered.")
    s.table(["Mode", "What happens"], [
        [UI("Simulation"), "<b>Default – the classic RLV feel</b> (as in Tacx, Rouvy or Kinomap): <i>climbs get "
                           "harder like on the road.</i> The status line shows how you shift (the "
                           "link opens the Devices page, section 5.5):<br/>"
                           "• <i>Shifting: your bike's gears</i> (no Zwift Click) – the app sends the route's gradient (× difficulty) to the "
                           "trainer, which sets the resistance itself; the card shows the "
                           f"{UI('Grade sent to trainer')}. Your speed comes from your power on the gradient "
                           "including the bike's momentum, so it rises and falls smoothly.<br/>"
                           "• <i>Virtual shifting: Zwift protocol</i> / <i>FTMS gradient</i> (a Zwift Click is "
                           f"connected) – Zwift's 24 gears, {UI('−')} / {UI('+')} on the card (<i>Gear 12/24</i>), "
                           "the keys or the Click. With the Zwift protocol the trainer does the physics and the "
                           "video follows your power; with the FTMS gradient speed and video follow your gear and "
                           "cadence."],
        [UI("ERG"), "<i>The trainer holds your watts. Climbs make the video slower, not the pedals heavier.</i> "
                    f"Set a target wattage on the card (big number, {UI('−25')} … {UI('+25')} steps, or an exact "
                    f"value with {UI('Set')}; 25–1500 W). Switching to ERG during a ride starts from your current "
                    "3 s power; otherwise the last target (first time: 60 % of your FTP)."],
    ], [1.2, 4.6])
    s.p("<b>How the gradient reaches the trainer.</b> Simulation uses the FTMS command <i>Set Indoor Bike "
        "Simulation Parameters</i> with wind 0 m/s, the grade (0.01 % steps), rolling resistance Crr 0.004 and "
        "wind resistance Cw = ρ·CdA of your rider model (e.g. 0.37 kg/m for 68 kg; section 5.6) A new grade is sent when it changes by "
        "0.1 % or more (at most once a second; otherwise refreshed every 3 seconds). The grade sent is limited "
        "to −10 … +20 %. With virtual gears see section 5.5 for how the gear reaches the trainer.")
    s.p("<b>Soft start.</b> At the start of a ride the trainer does not get the route's gradient at once: it "
        "gets 0 % until the flywheel turns (cadence of at least 40 rpm, or a trainer speed of at least 10 km/h, "
        "for 2 seconds; a trainer that reports neither: 50 W), then the gradient rises in a straight line to "
        "the real value over 4 seconds. After you stop (no cadence for more than 5 seconds) the soft start "
        "applies again when you ride on. Pausing and resuming also starts it again. This applies to the "
        "gradient (Simulation, virtual gears on the FTMS gradient path and the Zwift protocol's incline) – "
        "never to ERG.")
    s.note("If the trainer cannot simulate a gradient (it does not report the feature, or it rejects the "
           "command), Simulation automatically uses virtual gears and the card shows <i>Your trainer can't "
           "simulate climbs – using virtual gears instead.</i> A trainer without cadence is left in its "
           "free-ride mode; the speed then follows your power.")
    s.p("<b>Difficulty</b> (0–100 %, Simulation) scales the gradients: at 100 % you ride the real road, at 50 % "
        "a 6 % climb feels like 3 %, at 0 % the route is flat (descents are scaled the same way). Change it with "
        f"the slider on the {UI('Resistance')} card or {K('Page Up')} / {K('Page Down')} in 10 % steps – "
        "shifting never changes it. The setting is remembered. In ERG the difficulty does not apply.")

    s.section("Riding a workout during the video")
    s.p(f"The {UI('Workout')} card above {UI('Resistance')} lists your cycling workouts with their duration "
        f"({UI('None')} = just ride the route). Choose one before you start – during a ride the list is locked "
        f"(a tooltip says so); a Video Ride planned with a workout in the calendar loads it for you. The "
        "workout's intervals run on the ride's clock: they start with the ride and pause when you pause.")
    s.bullets([
        "<b>Workout chart</b> – between the video and the elevation profile: one block per interval, its "
        "height in % of your FTP (dashed FTP line), coloured by your power zones, ramps as slopes, finished "
        "blocks faded, the current block outlined and a cursor at the elapsed time. Above it: the current "
        "interval (<i>Sweet Spot 2/7</i>), the target, the time left in the interval, the next target, the "
        "workout time and <i>In target</i> – the share of seconds within ±10 % of the target. In small windows "
        f"(for example 800 × 480) the chart and the profile share the bottom card: {UI('Route')} | "
        f"{UI('Workout')}.",
        f"<b>{UI('Workout ERG')}</b> (on by default) – the trainer holds the workout's target watts, as on the "
        "Watt Training; the route only sets the speed of the video, so a climb makes it slower but not heavier. The "
        "Resistance card shows <i>Workout ERG holds the trainer. This mode applies after the workout.</i> Switch "
        "it off to use the targets as a guide: then Simulation or ERG from the Resistance card applies as usual.",
        f"<b>Status and controls</b> – the card shows the interval, the big target (green within ±5 %, yellow "
        f"within ±10 %, orange beyond), the time left, the next target and the time to go. {UI('−')} / "
        f"{UI('+')} change the <b>intensity</b> by 5 % (50–150 %; all targets scale), {UI('Skip Interval')} jumps "
        f"to the next interval and {UI('End Workout')} stops the workout while the ride goes on. The Power tile "
        "shows <i>Target 230 W</i> in the same colours, the sidebar <i>Video 12:34</i> / <i>Sweet Spot 2/6 • "
        "230 W</i>.",
        "<b>Completion</b> – when the workout ends before the route, a message says <i>Workout complete – "
        "continuing the route in Simulation</i> (or ERG, whatever the Resistance card is set to) and you ride "
        "on to the finish. When the route ends first, the ride finishes and is saved as always. The saved ride "
        "records the workout's targets (History chart, FIT/CSV), and a calendar entry is ticked off by the "
        "route (finish or 90 % of the distance), not by the workout.",
    ])

    s.section("Starting, pausing and finishing")
    s.bullets([
        f"{UI('Start')} (or {K('Space')}) starts at the chosen start point. Pedaling never starts the ride; "
        "before Start the status pill reads <i>Ready – press Start</i>.",
        f"{UI('Pause Ride')} (or {K('Space')}) stops the clock and the video; {UI('Resume')} (or Space) "
        "continues – pedaling on never resumes it.",
        f"{UI('Finish')} asks <i>Finish video ride?</i> – {UI('Save &amp; Finish')} or {UI('Discard')}. "
        "Reaching the end of the route finishes and saves the ride by itself. Video rides appear in History "
        "with the route name and can be exported as FIT or CSV.",
        "Only one session runs at a time – a video ride cannot start while a workout, a free ride or a row is "
        "in progress (running or paused) on another page. Start is then greyed out and a yellow card under it "
        f"says why – e.g. <i>A workout is paused on Watt Training</i> – with {UI('Go to Watt Training')}, "
        f"{UI('End &amp; Save')} and {UI('Discard')}. The route, workout, gears and difficulty stay usable. "
        "The ride never depends on the video: it can start even when the video cannot be played.",
    ])

    s.section("Zwift Click and keyboard")
    s.table(["Input", "Simulation", "ERG", "Workout ERG"], [
        ["Zwift Click + / −", "Shift harder / easier", "Target ±10 W", "Intensity ±5 %"],
        [K("↑") + " / " + K("↓"), "Shift harder / easier (virtual gears)", "Target ±5 W", "Intensity ±5 %"],
        [K("Shift") + "+" + K("↑") + " / " + K("↓"), "Jump 3 gears (virtual gears)", "–", "–"],
        [K("Page Up") + " / " + K("Page Down"), "Difficulty ±10 %", "Target ±10 W", "Intensity ±5 %"],
        [K("Space"), "Start / pause / resume", "Start / pause / resume", "Start / pause / resume"],
    ], [1.3, 2.3, 1.1, 1.1])
    s.p("Virtual gears are on exactly while a Zwift Click is connected. The keys work only on the Video Ride "
        "page and not while you type in a field or a dialog or menu is open; "
        f"{K('Enter')}, {K('Esc')} or a click on the page background leaves a text field. The arrow keys never "
        "change the route in the route list.")

    s.section("Video formats and Raspberry Pi")
    s.bullets([
        "<b>H.264 MP4</b> (720p or 1080p) plays on every platform.",
        "<b>AV1</b> videos need AV1 hardware decoding (recent Apple Silicon Macs, recent PC graphics). "
        "Elsewhere the page shows <i>No picture</i> or <i>This video cannot be played</i> together with the "
        "conversion steps below (with buttons to open the HandBrake website, copy the steps and rescan the "
        "library); the ride still works without the picture.",
        f"<b>Raspberry Pi</b>: prefer 720p H.264 and install the GStreamer decoders: "
        f"{C('sudo apt install gstreamer1.0-libav gstreamer1.0-plugins-good')} (recommended by the .deb "
        "package). 1080p and AV1 are too heavy for a Pi.",
    ])
    s.p(f"<b>Convert a video to H.264 with HandBrake</b> (free, {L('https://handbrake.fr', 'handbrake.fr')}):")
    s.steps([
        f"Open the video in HandBrake and choose the preset {UI('General → Fast 720p30')} "
        f"(or {UI('HQ 720p30 Surround')}).",
        f"On the {UI('Video')} tab set {UI('Video Encoder')} to {UI('H.264 (x264)')}, {UI('Framerate')} to "
        f"{UI('Same as source')} and select {UI('Constant Framerate')} – important, so the video stays in sync "
        "with the GPS route.",
        "Save it as <b>.mp4</b> in the route folder with the <b>same file name</b> as the original, replacing it "
        "(keep the original somewhere else).",
        f"Click {UI('Rescan Library')} (on the page, or in the folder menu next to the route drop-down).",
    ])
    s.p(f"Or with ffmpeg: {C('ffmpeg -i video.mp4 -c:v libx264 -crf 21 -an video-h264.mp4')}, then give the "
        "new file the original name.")
    s.tip("Free route videos with GPX tracks are available from several RLV communities; respect the licence "
          "of each video (many are non-commercial only).")

    # =====================================================================
    s.chapter("Music player",
              "Play your own music while you ride – from the Watt Training page, without leaving the app.")
    s.section("The player card")
    s.p(f"The {UI('Music Player')} card on the Watt Training page shows the album art (when the file contains it), "
        "title and artist, a heart button, previous / play-pause / next, a position bar, a mute button and a "
        f"volume slider (default 70 %, remembered). {UI('Up Next')} lists the next up to three songs; click one "
        "to play it. When the card is small, the list stays inside the card and scrolls. The playlist icon in the card header opens the music library.")
    s.p("Title and artist are read from the file's tags. Without tags, a file name of the form "
        f"{C('Artist - Title.mp3')} is split into artist and title; otherwise the file name is used.")
    s.section("Music library and playlists")
    s.bullets([
        "The left column lists your playlists with the number of songs; click one to open it. Long lists get "
        "a scrollbar, and long names are shortened with “…”.",
        f"Type a name in {UI('New playlist name')} and click {UI('Create')}. The trash button deletes the "
        "current playlist.",
        f"{UI('Add Files')} adds selected audio files; {UI('Add Folder')} adds all supported files in a folder "
        "and its sub-folders (sorted by name).",
        f"{UI('Shuffle')} plays the playlist in random order; {UI('Repeat playlist')} (default on) starts again "
        "after the last song.",
        "For each song: play, move up, move down and remove from the playlist.",
    ])
    s.p("<b>Favourites:</b> the heart button on the player card adds the current song to a playlist called "
        "<i>Favorites</i> (created automatically) or removes it again.")
    s.p("<b>Supported formats:</b> MP3, M4A, AAC, FLAC, WAV, OGG, Opus and WMA. Playback uses the operating "
        "system's media framework, so whether a format actually plays can depend on the platform (WMA, for "
        "example, is typically only available on Windows).")
    s.section("Default playlist")
    s.p("On first start the app creates the playlist <i>Workout Mix</i> and fills it with up to 200 supported "
        "files from the first folder that contains music: the sample music shipped with the app (two tracks) "
        "or, if none is found, your system Music folder. If you delete the last playlist, an empty "
        "<i>Workout Mix</i> is created again.")
    s.note("Playlists store links to your files – the files are not copied. If you move or delete a file, the "
           "player reports <i>“File not found”</i>; remove it and add it again from its new location.")

    # =====================================================================
    s.chapter("Settings & data")
    s.section("Settings dialog")
    s.p("Click the gear icon in the top bar. The Settings dialog contains:")
    s.bullets([
        f"{UI('Dark theme')} – same as the sun/moon button.",
        f"{UI('Show virtual devices')} and {UI('Auto-connect known devices')} – same as in Connection "
        "Preferences on the Devices page.",
        f"{UI('Smooth power')} – shows cycling watts as a rolling average (on by default, 3 s) and the length "
        "of the average: 1, 3, 5 or 10 s (section 15.2).",
        f"{UI('Diagnostic log')} (on by default) – writes a trace log file automatically: the logs folder "
        "(selectable text), an scp command to fetch the logs from a Raspberry Pi, "
        f"{UI('Open logs folder')} and {UI('Export logs…')} (copies all log files to a folder you choose, "
        "e.g. a USB stick). See “Diagnostic log” in chapter “Troubleshooting & FAQ”.",
        "The full path of the database file (<i>“All data is stored locally (offline) in SQLite”</i>).",
        f"The app version. {UI('Done')} closes the dialog.",
    ])
    s.section("Power smoothing (3 s average)")
    s.p("Nobody pedals exactly evenly with both legs, so the power a trainer reports changes with every pedal "
        "stroke and the raw number jumps up and down. With "
        f"{UI('Smooth power')} on (the default) the app shows the <b>average over the last 3 seconds</b> "
        "instead – like most training apps and bike computers. The average is time-based: every reading counts "
        "for as long as it was current, so it works the same with trainers that send 1, 2 or 4 readings a "
        "second. When the trainer stops sending, the value drops to 0 after 3 seconds.")
    s.bullets([
        "Used for the displayed watts on the Watt Training page power card (including the deviation from the target and "
        "the zone colour of its bar), the Free Ride power tile and ERG panel (<i>Actual … W</i>), the sidebar "
        "status box and the live trainer values on the Devices page (<i>Power 3s</i>).",
        "<b>Not</b> used for recording: rides keep the raw per-second power, so averages, NP, IF, TSS, zones, "
        "FIT/CSV export, the FTP test (best 1-minute power), ERG control and virtual gearing are unchanged.",
        "Rowing is not affected – the PM5 already reports power per stroke.",
        f"Change it in the Settings dialog ({UI('Smooth power')}: switch plus 1 / 3 / 5 / 10 s) or click the "
        f"{UI('3 s avg')} badge on the power card; the setting is stored in the database and kept after a restart.",
    ])
    s.section("Where your data is stored")
    s.p(f"Everything – profiles, workouts, rides and rows with per-second data, the training plan, rowing "
        f"records, playlists and settings – is stored in "
        f"a single SQLite file named {C('watt-trainer.sqlite')} in the standard application-data folder of "
        "your user account (organisation <i>Artilody</i>, application <i>Watt Trainer</i>):")
    s.table(["System", "Default location"], [
        ["Windows", C("%APPDATA%\\Artilody\\Watt Trainer\\watt-trainer.sqlite") + "<br/>(usually "
         + C("C:\\Users\\&lt;name&gt;\\AppData\\Roaming\\…") + ")"],
        ["macOS", C("~/Library/Application Support/Artilody/Watt Trainer/watt-trainer.sqlite")],
        ["Linux / Raspberry Pi", C("~/.local/share/Artilody/Watt Trainer/watt-trainer.sqlite")],
    ], [1.4, 4.4])
    s.p("The exact path on your computer is always shown in the Settings dialog.")
    s.sub("Using a different database file")
    s.p(f"Set the environment variable {C('WATT_DB_PATH')} to the full path of a database file before starting "
        "the app; it will then use (or create) that file instead. Example for macOS/Linux:")
    s.code('WATT_DB_PATH="$HOME/watt-test.sqlite" artilody-watts-trainer')
    s.section("Backup and restore")
    s.steps([
        "Close the app.",
        f"Copy {C('watt-trainer.sqlite')} from the folder shown in Settings to a safe place (USB drive, cloud "
        "folder, another computer).",
        "To restore, close the app and copy the backup back to the same folder under the same name.",
    ])
    s.tip(f"To back up only your own workouts, use {UI('Export')} → {UI('Export all my workouts…')} on the Workouts page "
          f"(section 6.2).")
    s.tip("You can move your complete training history to a new computer the same way. Music files and "
          "avatar pictures are referenced by path and must be copied separately.")
    s.section("Offline and privacy")
    s.p("Artilody Watts Trainer runs fully offline: no account, no cloud, no telemetry or analytics. "
        "The app only talks to your Bluetooth devices. Your data leaves your computer only when you export a "
        "ride (FIT or CSV) or a workout, or copy the database yourself – the app never uploads anything.")

    # =====================================================================
    s.chapter("Troubleshooting & FAQ")
    s.section("Diagnostic log")
    s.p("The app writes a plain-text trace log by itself – no terminal needed. It is on by default "
        f"(Settings → {UI('Diagnostic log')}). One file per app start, named "
        + C("artilody-YYYYMMDD-HHMMSS.log") + "; the newest 20 files (at most 50 MB) are kept. Lines are written "
        "to disk every half second, so the log survives switching a Raspberry Pi off.")
    s.table(["System", "Logs folder"], [
        ["Raspberry Pi / Linux", C("~/.local/share/Artilody/Watt Trainer/logs")],
        ["macOS", C("~/Library/Application Support/Artilody/Watt Trainer/logs")],
        ["Windows", C("%APPDATA%\\Artilody\\Watt Trainer\\logs")],
    ], [1, 2.6])
    s.p("<b>Fetch the logs from a Raspberry Pi</b> – in Terminal on the Mac, with your Pi's user and host name "
        "(the Settings dialog shows the exact command):")
    s.code("scp 'pi@raspberrypi.local:.local/share/Artilody/Watt?Trainer/logs/*.log' .")
    s.p(f"<b>Without a network:</b> Settings → {UI('Export logs…')} copies all log files to a folder you "
        f"choose, e.g. a USB stick. {UI('Open logs folder')} opens the folder; the Bluetooth diagnostics dialog "
        "on the Devices page also shows the current log file with an Open folder button.")
    s.p("<b>What to send:</b> the log file of the ride (newest file, or the one with the ride's date) and a "
        "short note with the time and what you felt, e.g. <i>“20:14, start of the descent, lightest gear, "
        "about 200 W”</i>. The log contains:")
    s.bullets([
        "app, Qt and OS version and the Raspberry Pi model;",
        "the shifting settings: Zwift Click, shifting mode (Zwift protocol / FTMS gradient / your bike's "
        "gears), Zwift-protocol override, gear, reference and detected real gear, difficulty, rider + bike "
        "mass, CdA, Crr;",
        "the virtual shifting decisions (lines <i>VSHIFT</i>: Click connected or gone, which path and why) "
        "and the Zwift trainer protocol (lines <i>ZHUB</i>): detection on the connected trainer, every "
        "handshake step, every write and notification in hex and decoded (incline, gear ratio, weights, "
        "riding data), the trainer's settings echo, keep-alive, timeouts and the fallback to FTMS gradient;",
        "every command to the trainer (FTMS Control Point, hex and decoded: grade, Crr, Cw, wind, ERG watts) "
        "and the trainer's answer (result code), its status notifications (Fitness Machine Status, Training "
        "Status), its features and supported inclination / resistance / power ranges;",
        "the trainer's speed, cadence and power once per second;",
        "per second during a ride: mode, resistance path, gear and ratio, cadence, virtual speed, route and "
        "effective grade, grade or ERG target sent, assumed trainer speed, demanded and measured power, "
        "coasting;",
        "Zwift Click presses with the resulting gear, connections, ride start / pause / finish and warnings.",
    ])
    s.p("Only your profile name and weight are personal data in the log. Turning the switch off stops "
        "writing; delete old files in the logs folder at any time.")
    s.section("Frequently asked questions")
    faq = [
        ("“Start” is greyed out.",
         "Another session is still in progress on another page – usually a workout started or paused on the "
         "Watt Training page, or a paused free ride (the sidebar chip shows <i>Paused</i>). The yellow card under Start "
         "says which one and offers <b>Go to …</b>, <b>End &amp; Save</b> and <b>Discard</b> (click twice). A "
         "workout, route or row that is only prepared (not started) and finished rides never block. If Start stays grey without a card, start the app from a "
         "terminal with <i>WATT_DEBUG_RLVUI=1</i> and send the <i>RLVUI</i> lines with your report."),
        ("My trainer is not found.",
         "Make sure it is an FTMS (Bluetooth LE) trainer – ANT+-only and proprietary-protocol trainers are not "
         "supported. Wake it by pedaling, disconnect it from other apps and devices (Zwift, phone apps, bike "
         "computers), check that Bluetooth is on (and, on macOS, that the app has Bluetooth permission), then "
         "press Scan for Devices. Look at the Connection Log for errors."),
        ("ERG does not change the resistance.",
         "Check that the ERG Mode switch on the Watt Training page is on and that the ride is running (ERG is released "
         "before Start, while paused and in Free Ride). Check that the trainer lists <i>ERG Control</i> under "
         "Capabilities, and try the Trainer Control Test. A log entry <i>Trainer control refused</i> means "
         "another app holds control of the trainer. Some trainers react with a delay of a few seconds."),
        ("Heart rate shows “–”.",
         "No heart-rate data has arrived for 6 seconds. Moisten the strap electrodes, make sure the strap is "
         "connected on the Devices page, and that it is not paired exclusively to a watch or phone. Replace "
         "the battery if the sensor connects but drops out."),
        ("Power or cadence drop to 0 for a moment.",
         "Values are reset after 3 seconds without data, typically because of a weak Bluetooth signal. Reduce "
         "the distance, avoid metal obstacles and 2.4 GHz interference, or use a USB Bluetooth adapter with an "
         "extension cable and select it under Bluetooth Adapter."),
        ("The workout does not start when I pedal.",
         "That is intended: sessions only start when you press Start (or Space) on the Watt Training, Free "
         "Ride, Video Ride or Rowing page, so pedaling while you clip in never starts the clock. Load the "
         "workout, get on the bike, then press Start. Pedaling (or rowing) also never resumes a paused "
         "session – press Resume."),
        ("My ride was not saved.",
         "Rides with less than 30 seconds of recorded time are not saved, and Discard deletes the ride. Rides "
         "are saved for the profile that was active when the ride started."),
        ("The FTP dialog did not appear after my test.",
         "The estimate is only offered for workouts in the FTP Tests category, with at least 5 minutes of data "
         "and a result between 50 and 600 W. You can still open the ride in History → View Details → Use as "
         "FTP."),
        ("macOS says the app cannot be opened or is damaged.",
         "Right-click the app → Open → Open. If that does not help, run "
         + C('xattr -cr "/Applications/Artilody Watts Trainer.app"') + " in Terminal (section 2.2)."),
        ("Windows shows “Windows protected your PC”.",
         "The app is not code-signed yet. Click More info → Run anyway."),
        ("Raspberry Pi: the UI is slow or too small.",
         "Use a Pi 4 or 5 and the 64-bit OS. Maximise the window – the UI scales down to fit and needs at least "
         "800 × 480 pixels. Close other programs while riding."),
        ("Music does not play.",
         "Check the volume and mute button on the player card. If a song reports <i>File not found</i>, it "
         "was moved or deleted; <i>Cannot play this file</i> means the format is not supported by your "
         "system's media framework – convert it to MP3, M4A or FLAC."),
        ("How do I reset all data?",
         "Close the app and delete (or rename, to keep a backup) " + C("watt-trainer.sqlite") + " in the folder "
         "shown in Settings. The next start creates a fresh database with the default profile and the built-in "
         "workouts."),
        ("How do I get my rides into Strava, Garmin Connect or TrainingPeaks?",
         "Export them as FIT files in History (Export → Selected ride – FIT, or All rides in range – FIT "
         "files…) and upload the files on the platform's website (section 9.5). The app does not upload "
         "anything by itself."),
        ("An imported workout is called “… (imported)”.",
         "Your library already contained a different workout with the same name. Rename it in the Builder if "
         "you like. If nothing was imported at all, the workout was already in your library."),
        ("My Concept2 PM5 is not found.",
         "On the PM5 choose More Options → Turn Wireless ON → Connect Device and scan again while the monitor "
         "shows the Connect screen. Close ErgData and other apps that may be connected to the PM5 (it accepts "
         "one app at a time), and keep the computer within a few metres. Turn on Show all devices and use "
         "Connect as rower if the PM5 is listed under Other Bluetooth devices or under Smart Trainers, and check Bluetooth Diagnostics → "
         "Details… (see “Rowing machine not found” in chapter 11, including the Raspberry Pi commands)."),
        ("The rower does not follow the watt plan targets.",
         "That is expected: the PM5 has no ERG mode. Follow the target power by pulling harder or easier; "
         "the Current value turns green within ±10 % of the target."),
        ("My ghost race did not save a new record.",
         "Records are only saved when the full distance is completed in a Ghost Race or a distance Free Row, "
         "and the time is faster than the existing best. Rows finished early do not count."),
        ("A planned workout was not ticked off.",
         "Entries are completed automatically only by a saved ride or row of the same workout on the same "
         "day. Use Mark as completed in the chip menu for anything else."),
        ("Do I need an internet connection?",
         "No. The app works fully offline."),
    ]
    for q, a in faq:
        s.sub(q)
        s.p(a)

    # =====================================================================
    s.chapter("Glossary & keyboard shortcuts")
    s.section("Glossary")
    s.table(["Term", "Explanation"], [
        [".awt", "Artilody workout file – the app's own JSON format for sharing workouts (Appendix C)."],
        ["BLE", "Bluetooth Low Energy – the wireless standard used for trainers and sensors."],
        ["Cadence", "Pedaling rate in revolutions per minute (RPM)."],
        ["Compliance", "In a rowing Watt Plan: share of seconds within ±10 % of the target power."],
        ["ERG mode", "The trainer automatically sets the resistance so you produce the target power, "
                     "regardless of cadence."],
        ["Free Ride", "The trainer does not follow power targets; it simulates a flat road and you control the "
                      "effort. Also a ride without a workout."],
        ["FIT", "Flexible and Interoperable Data Transfer – the binary activity file format by Garmin, read by "
                "Strava, Garmin Connect, TrainingPeaks, intervals.icu and most other platforms."],
        ["FTMS", "Fitness Machine Service – the standard Bluetooth profile for smart trainers (data and "
                 "control)."],
        ["FTP", "Functional Threshold Power – the highest power you can sustain for about one hour. Kept "
                "separately for cycling and rowing (Rowing FTP)."],
        ["Gear ratio", "Chainring teeth ÷ cog teeth; a higher ratio means more distance per pedal stroke."],
        ["Ghost", "A replay of your personal best row that you race against."],
        ["HRS", "Heart Rate Service – the standard Bluetooth profile for heart-rate sensors."],
        ["IF", "Intensity Factor = Normalized Power ÷ FTP."],
        ["Karvonen", "Method for heart-rate zones based on the heart-rate reserve (max HR − resting HR)."],
        ["kJ", "Kilojoules of mechanical work; roughly equal to kcal of food energy burned."],
        ["Pace", "Rowing speed as time per 500 m (e.g. 2:04 /500m)."],
        ["PM5", "Performance Monitor 5 – the Bluetooth display of Concept2 rowing machines."],
        ["NP", "Normalized Power – a weighted average power that reflects the physiological cost of a "
               "variable ride."],
        ["RLV", "Real Life Video – a filmed route (video + GPS track) that plays at your riding speed "
                "(Video Ride page)."],
        ["RPE", "Rating of Perceived Exertion – how hard an effort feels on a scale of 1 to 10."],
        ["TSS", "Training Stress Score – training load from duration and intensity; one hour at FTP = 100."],
        ["Virtual gears", "Gears simulated by the app: speed from cadence and gear, trainer power from the speed "
                          "on the virtual road (Free Ride and Video Ride pages)."],
        ["W/kg", "Watts per kilogram of body weight – power relative to weight."],
        ["Zone", "An intensity range defined as a percentage of FTP (power) or heart-rate reserve (HR)."],
        ["Zwift Click", "A small Bluetooth handlebar controller with + / − buttons, used for virtual shifting."],
        ["ZWO", "Zwift workout file (.zwo) – an XML format for structured workouts, also read by TrainerRoad, "
                "Golden Cheetah, MyWhoosh and others."],
    ], [1.1, 4.7])
    s.section("Keyboard shortcuts")
    s.table(["Key", "Where", "Action"], [
        [K("Space"), "Watt Training", "Start the loaded workout / pause / resume (like the Start button)."],
        [K("Space"), "Rowing", "Start a set-up row (rower connected) / pause / resume."],
        [K("Space"), "Free Ride", "Start / pause / resume the free ride."],
        [K("↑") + " " + K("+") + " / " + K("↓") + " " + K("−"), "Free Ride",
         "Shift harder / easier (Ride with virtual gears); target ±5 W (ERG). Inactive otherwise."],
        [K("Shift") + "+" + K("↑") + " / " + K("↓"), "Free Ride / Video Ride", "Bigger / smaller chainring; sequential: jump 3 gears."],
        [K("Page Up") + " / " + K("Page Down"), "Free Ride", "Grade ±0.5 % (Ride); target ±10 W (ERG)."],
        [K("Space"), "Video Ride", "Start / pause / resume the video ride."],
        [K("↑") + " " + K("+") + " / " + K("↓") + " " + K("−"), "Video Ride",
         "Shift harder / easier (Simulation with virtual gears); target ±5 W (ERG)."],
        [K("Page Up") + " / " + K("Page Down"), "Video Ride", "Difficulty ±10 %; target ±10 W (ERG); intensity ±5 % "
         "(Workout ERG)."],
        [K("Alt") + "+" + K("←") + " / " + K("→") + " or " + K("↑") + " / " + K("↓"), "Builder",
         "Move the selected block earlier / later (Option on a Mac)."],
        [K("Enter") + " / " + K("Esc"), "Text fields on the ride pages",
         "Leave the field (Enter also applies an exact ERG target), so the ride keys work again. A click on "
         "the page background does the same."],
        [K("Esc"), "Dialogs", "Close the dialog (where it can be closed)."],
        [K("Enter"), "New playlist name", "Create the playlist."],
    ], [1.0, 1.5, 3.3])

    # =====================================================================
    s.chapter("Built-in workouts", appendix=True, new_page=True,
              lead="These workouts are installed on first start. Watt values depend on your FTP; the examples "
                   "assume 250 W. Built-in workouts cannot be deleted; edits are saved as a personal copy.")
    s.table(["Workout", "Category", "Duration", "Focus / structure"], [
        ["Sweet Spot 3 × 12", "Intervals", "56 min",
         "Sweet Spot. 6 min warm-up, 3 × 12 min at 90 % FTP (cadence 90) with 5 min recovery at 55 %, "
         "4 min cool-down."],
        ["Endurance Base", "Endurance", "60 min",
         "Build your base. 10 min warm-up, 20 min at 68 % and 20 min at 72 % FTP, 10 min cool-down."],
        ["VO<sub>2</sub> Max 5 × 3", "Intervals", "45 min",
         "VO<sub>2</sub> Max. 10 min warm-up, 5 × 3 min at 120 % FTP (cadence 100) with 3 min recovery, 5 min "
         "cool-down."],
        ["Recovery Spin", "Recovery", "30 min",
         "Active recovery. 3 min easy start, 24 min at 55 % FTP, 3 min cool-down."],
        ["Threshold 2 × 20", "Intervals", "65 min",
         "Threshold. 10 min warm-up, 2 × 20 min at 95 % FTP with 5 min recovery, 10 min cool-down."],
        ["Ramp FTP Test", "FTP Tests", "25 min",
         "Progressive ramp to test your FTP. 5 min warm-up, 20 min ramp from 50 to 150 % FTP. Protected – "
         "always present."],
        ["Tempo 3 × 15", "Endurance", "67 min",
         "Muscular endurance. 8 min warm-up, 3 × 15 min at 80 % FTP with 4 min recovery, 6 min cool-down."],
        ["Over-Unders 4 × 8", "Intervals", "62 min",
         "Lactate clearance. 10 min warm-up, 4 sets of 8 min alternating 2 min at 92 % and 1 min at 108 % FTP, "
         "4 min recovery between sets, 8 min cool-down."],
    ], [1.5, 0.95, 0.75, 3.6])

    s.chapter("Power zone reference", appendix=True)
    s.table(["Zone", "Default % FTP", "RPE", "Typical use"], [
        ["Z1 Recovery", "&lt; 55 %", "1", "Warm-up, cool-down, recovery between intervals and recovery rides."],
        ["Z2 Endurance", "55 – 75 %", "2 – 3", "Long aerobic rides; builds the aerobic base."],
        ["Z3 Tempo", "75 – 87 %", "4 – 6", "Steady, moderately hard efforts; muscular endurance."],
        ["Z4 Sweet Spot", "87 – 94 %", "7 – 8", "Efficient threshold development with manageable fatigue."],
        ["Z5 Threshold", "94 – 105 %", "9", "Efforts around FTP, 8–20+ minutes."],
        ["Z6 VO<sub>2</sub> Max", "&gt; 105 %", "10", "Short, very hard intervals of 1–5 minutes."],
    ], [1.4, 1.1, 0.7, 3.4], swatches=list(Z), keep=True)
    s.p("The zone colours are used everywhere in the app: Watt Training power bar, workout charts, timeline blocks, "
        "Builder and History. Zone boundaries can be changed per profile (section 4.3).")

    s.chapter("The .awt file format", appendix=True,
              lead="Artilody workout files (.awt) are plain UTF-8 JSON text, so they can be read, written or "
                   "generated by other programs and scripts. This appendix describes version 1 of the format.")
    s.code('''{
  "format": "artilody-watts-trainer/workout",
  "version": 1,
  "exportedAt": "2026-10-23T12:00:00Z",
  "app": "Artilody Watts Trainer 1.3.4",
  "workouts": [
    {
      "name": "Sweet Spot 3 x 12",
      "sport": "cycling",
      "category": "Intervals",
      "focus": "Sweet Spot",
      "description": "Three 12-minute sweet spot efforts ...",
      "intervals": [
        { "name": "Warm Up", "type": "warmup", "duration": 360,
          "start": 0.40, "end": 0.65 },
        { "name": "Sweet Spot", "type": "interval", "duration": 720,
          "start": 0.90, "end": 0.90, "cadence": 90 },
        { "name": "Recovery", "type": "recovery", "duration": 300,
          "start": 0.55, "end": 0.55, "rpe": 0 }
      ]
    }
  ]
}''')
    s.table(["Field", "Meaning"], [
        [C("format"), f"Always {C('artilody-watts-trainer/workout')}."],
        [C("version"), "Format version, currently 1."],
        [C("exportedAt") + ", " + C("app"), "Time of export (ISO 8601, UTC) and the exporting app – "
                                             "informational only."],
        [C("workouts"), "List of workouts (at most 500 per file)."],
        [C("name"), "Workout name (required)."],
        [C("sport"), f"{C('cycling')} or {C('rowing')} (optional, default {C('cycling')}). Rowing targets are "
                     "fractions of the rowing FTP."],
        [C("category"), "Endurance, Intervals, Recovery or FTP Tests. Other values are replaced on import."],
        [C("focus") + ", " + C("description"), "Short focus text and description (optional)."],
        [C("intervals"), "The blocks in order (at least one, at most 2000)."],
        [C("type"), f"{C('warmup')}, {C('steady')}, {C('interval')}, {C('recovery')}, {C('cooldown')} or "
                    f"{C('ramp')}."],
        [C("duration"), "Block length in seconds (5 – 21600)."],
        [C("start") + ", " + C("end"), "Target at the start and end of the block as a <b>fraction of FTP</b> "
                                         "(0.90 = 90 % FTP; maximum 4.0). Equal values give a steady block, "
                                         "different values a ramp."],
        [C("cadence"), "Optional cadence target in RPM (omitted = no target)."],
        [C("rpe"), "Optional RPE zone 0 – 5 (Z1 – Z6); omitted = derived from the intensity."],
    ], [1.6, 4.2])
    s.p("Because intensities are fractions of FTP, the same file produces the right watts for every rider. "
        f"Files with the extension {C('.json')} are accepted as well, and a file may also contain a single "
        "workout object or a plain list of workouts.")

    s.chapter("License & credits", appendix=True)
    s.p("Artilody Watts Trainer is free software: you can redistribute it and/or modify it under the terms of "
        "the <b>GNU General Public License version 3</b> (GPL-3.0) as published by the Free Software "
        "Foundation. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without "
        "even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the "
        f"{C('LICENSE')} file for details.")
    s.bullets([
        "Built with the Qt framework (Qt Quick, Qt Bluetooth, Qt Multimedia, Qt SQL), used under the LGPL/GPL.",
        "Data storage by SQLite.",
        "Uses the Bluetooth SIG's open Fitness Machine and Heart Rate service specifications and Concept2's "
        "public PM5 Bluetooth Smart Interface Definition. Concept2, PM5 and RowErg are trademarks of "
        "Concept2, Inc.; this project is not affiliated with Concept2.",
        "The training metrics NP, IF and TSS follow the widely used definitions by Dr. Andrew Coggan.",
    ])
    s.p(f"Source code, releases and issue tracker: {L(REPO)}")
    s.p(f"Found a bug or have an idea? Please open an issue: {L(REPO + '/issues')}")
    s.spacer(10)
    s.p(f'<font color="#5b6485">This manual describes Artilody Watts Trainer version {V} (October 2026). '
        "Screenshots show the app with the virtual trainer, heart-rate sensor and rower and demo data; the profile dialog "
        "illustration is a design mockup.</font>")
