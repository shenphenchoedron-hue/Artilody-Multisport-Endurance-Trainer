# Building Artilody Multisport Endurance Trainer from source

This guide explains how to build, test and package Artilody Multisport Endurance Trainer on each supported platform. For a short overview, see the [README](../README.md#building-from-source).

The official release packages are built by the GitHub Actions workflow [`.github/workflows/release.yml`](../.github/workflows/release.yml). It is the reference for the exact commands shown below.

## Requirements

| | Minimum |
|---|---|
| CMake | 3.21 |
| Compiler | C++17 (GCC, Clang, MSVC 2019+ or MinGW) |
| Qt | **6.4** or newer (CI uses 6.4.2 on Linux and 6.8.3 on Windows/macOS) |
| Qt modules | Core, Gui, OpenGL, Qml, Quick, QuickControls2, Sql (SQLite driver), Bluetooth (`qtconnectivity`), Multimedia (`qtmultimedia`), and Test for the unit tests |
| Linux Bluetooth | BlueZ ≥ 5.48 |

Ninja is optional but recommended (`-G Ninja`).

## Generic build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=<path-to-Qt>/6.x/<platform>     # not needed with distro Qt
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The executable ends up in `build/src/app/` (on Windows in `build/`, next to the DLLs):

| Platform | Output |
|---|---|
| Linux | `build/src/app/artilody-watts-trainer` |
| macOS | `build/src/app/Artilody Multisport Endurance Trainer.app` |
| Windows | `build/Artilody Multisport Endurance Trainer.exe` |

The sample music in [`music/`](../music) is copied next to the executable (or into the macOS bundle) during the build.

### CMake options

| Option | Default | Description |
|---|---|---|
| `WATT_BUILD_TESTS` | `ON` | Build the QtTest unit tests in `tests/` |
| `WATT_BUILD_APP` | `ON` | Build the QML application. Turn it off to build only the libraries and tests. |

## Running the tests

```bash
ctest --test-dir build --output-on-failure
```

The tests run with `QT_QPA_PLATFORM=offscreen`, so they don't need a display or Bluetooth hardware. They cover workouts, the workout engine, ride analysis, the database, the training session, the builder and the FTMS/HRS protocol code.

## Debian 12, Ubuntu 24.04 and Raspberry Pi OS (Bookworm)

These distributions ship Qt 6.4 or newer, so you can build against the system packages. This is the package list used by the release workflow:

```bash
sudo apt install --no-install-recommends \
    build-essential cmake ninja-build dpkg-dev file git ca-certificates \
    libgl-dev \
    qt6-base-dev qt6-declarative-dev qt6-connectivity-dev qt6-multimedia-dev \
    libqt6sql6-sqlite qt6-qpa-plugins \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtquick-templates \
    qml6-module-qtqml-workerscript qml6-module-qtqml-models \
    qml6-module-qt-labs-folderlistmodel qml6-module-qtmultimedia \
    gstreamer1.0-plugins-base gstreamer1.0-plugins-good \
    fonts-dejavu-core
```

To use Bluetooth at runtime you also need BlueZ (`sudo apt install bluez`, then `sudo systemctl enable --now bluetooth`).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/src/app/artilody-watts-trainer
```

### Installing and packaging (.deb)

`cmake --install build` installs the app in the standard FHS layout: the binary, the `.desktop` file, the icons and the sample music under `share/artilody-watts-trainer/music`.

To build an installable Debian package with CPack (configured in [`packaging/linux/CPackDeb.cmake`](../packaging/linux/CPackDeb.cmake)):

```bash
cd build && cpack -G DEB
sudo apt install ./artilody-watts-trainer_*.deb
```

The package's library dependencies are computed automatically with `dpkg-shlibdeps`. QML modules, plugins and BlueZ are declared explicitly. Build on the same distribution and architecture you are targeting. For Raspberry Pi OS, build natively on a 64-bit Pi (arm64).

Ubuntu 22.04 and older ship Qt 6.2, which is too old.

### Raspberry Pi notes

- The 64-bit edition of Raspberry Pi OS Bookworm is required. A Pi 4 or 5 is recommended, both for building and for a smooth UI.
- The UI is designed for 1584 × 992 and scales down automatically on smaller screens, such as the official 7" display (800 × 480).

## macOS

1. Install Qt 6 (6.4+) with the **Qt Online Installer** (select the *Qt Connectivity* and *Qt Multimedia* modules), or with [aqtinstall](https://github.com/miurahr/aqtinstall):
   ```bash
   pip install aqtinstall
   aqt install-qt mac desktop 6.8.3 clang_64 -m qtconnectivity qtmultimedia -O ~/Qt
   ```
2. Configure and build. The release workflow builds a universal binary for macOS 12+:
   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos \
         "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
         -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
3. Bundle the Qt frameworks and QML modules into the app and sign it ad hoc:
   ```bash
   macdeployqt "build/src/app/Artilody Multisport Endurance Trainer.app" -qmldir=qml
   codesign --force --deep --sign - "build/src/app/Artilody Multisport Endurance Trainer.app"
   ```
4. Optional: create a drag-and-drop DMG:
   ```bash
   mkdir -p dmg && ditto "build/src/app/Artilody Multisport Endurance Trainer.app" "dmg/Artilody Multisport Endurance Trainer.app"
   ln -s /Applications dmg/Applications
   hdiutil create -volname "Artilody Multisport Endurance Trainer" -srcfolder dmg -ov -format UDZO ArtilodyWattsTrainer.dmg
   ```

`Info.plist` contains `NSBluetoothAlwaysUsageDescription`, so macOS asks for Bluetooth permission the first time you scan.

## Windows

1. Install Visual Studio 2019 or newer with the *Desktop development with C++* workload. MinGW also works.
2. Install Qt 6 (6.4+) for MSVC 64-bit with the *Qt Connectivity* and *Qt Multimedia* modules, using the Qt Online Installer or aqtinstall:
   ```powershell
   pip install aqtinstall
   aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtconnectivity qtmultimedia -O C:\Qt
   ```
3. Build from a **Developer PowerShell / x64 Native Tools prompt**:
   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
4. Deploy the Qt runtime next to the executable:
   ```powershell
   windeployqt --release --qmldir qml "build\Artilody Multisport Endurance Trainer.exe"
   ```

The release installer is built with Inno Setup from [`packaging/windows/installer.iss`](../packaging/windows/installer.iss). Bluetooth LE requires Windows 10 or newer.

## Developer tips

| Environment variable | Effect |
|---|---|
| `WATT_DB_PATH=<file>` | Use a different SQLite database instead of the default `watt-trainer.sqlite` in the user's app-data folder |
| `WATT_SCREENSHOT_DIR=<dir>` | Render every page to PNG in `<dir>`, then quit (used for screenshots and smoke tests) |
| `WATT_SCREENSHOT_STEP_MS=<ms>` | Delay between pages in screenshot mode (default 1500 ms) |
| `WATT_GPX_DIR=<folder>` | Add a folder of `.gpx` files to the GPX Ride library for this run only |
| `WATT_NO_OPENGL=1` | Keep Qt Quick's default graphics backend (no OpenGL; the GPX Ride 3D view is then unavailable) |

- You don't need hardware to develop the app. Connect the **Virtual Trainer** and **Virtual HR Sensor** on the Devices page.
- To run the app without a display (CI), set `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`. See [`packaging/ci/smoke-test.sh`](../packaging/ci/smoke-test.sh).
