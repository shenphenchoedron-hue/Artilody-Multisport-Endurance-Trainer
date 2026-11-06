#!/usr/bin/env bash
# Self-contained Raspberry Pi OS package: the app with its own Qt (the same
# Qt as the Windows and macOS builds) and every library it needs inside
# /opt/artilody-watts-trainer, so installing it never downloads anything.
# Only what every Raspberry Pi OS (64-bit, Bookworm) desktop already has is
# left to the system: the C/C++ runtime, the graphics driver (Mesa: GL / EGL,
# X11 / Wayland client libraries), fonts, D-Bus and BlueZ (bluetoothd). Those
# are the package's Depends (worked out below from the libraries the bundle
# still links to), so apt finds them installed.
#
#   packaging/linux/pi-bundle.sh <build dir> <Qt dir> <version> <out .deb>
#
# Run in a Debian 12 (Bookworm) arm64 environment after building with that Qt
# (CI: .github/workflows/release.yml, job "pi-deb"). Uses linuxdeploy and its
# Qt plugin (plugins, QML modules, rpaths, Qt's own libraries).
set -euxo pipefail

build="$(realpath "${1:?build dir}")"
qt="$(realpath "${2:?Qt dir}")"
version="${3:?version}"
out="$(realpath -m "${4:?output .deb}")"
src="$(realpath "$(dirname "$0")/../..")"

name=artilody-watts-trainer
prefix=/opt/$name
work="$(mktemp -d)"
root="$work/root"
appdir="$root$prefix"

# 1. The app as installed (bin, music, desktop file, icons) under /opt.
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# 2. Qt plugins whose libraries are not shipped (databases, GStreamer: the
#    FFmpeg media backend is used) would make linuxdeploy fail: leave them out.
for p in "$qt"/plugins/sqldrivers/*.so; do
    case "$(basename "$p")" in libqsqlite.so) ;; *) rm -f "$p" ;; esac
done
rm -f "$qt"/plugins/multimedia/libgstreamermediaplugin.so

# 3. linuxdeploy + Qt plugin (extracted: no FUSE in containers).
tools="$work/tools"
mkdir -p "$tools/bin"
for t in linuxdeploy linuxdeploy-plugin-qt; do
    curl -fsSL -o "$tools/$t.AppImage" \
        "https://github.com/linuxdeploy/$t/releases/download/continuous/$t-aarch64.AppImage"
    chmod +x "$tools/$t.AppImage"
    (cd "$tools" && "./$t.AppImage" --appimage-extract >/dev/null && mv squashfs-root "$t")
    printf '#!/bin/sh\nexec "%s/%s/AppRun" "$@"\n' "$tools" "$t" > "$tools/bin/$t"
    chmod +x "$tools/bin/$t"
done
export PATH="$tools/bin:$PATH"
export QMAKE="$qt/bin/qmake"
export LD_LIBRARY_PATH="$qt/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QML_SOURCES_PATHS="$src/qml"
# X11 (xcb, deployed by default), Wayland (the Pi 4/5 desktop) and offscreen
# (CI smoke tests, headless checks)
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1
linuxdeploy --appdir "$appdir" --executable "$appdir/usr/bin/$name" --plugin qt

# 4. System-visible pieces: the command, the menu entry and icons.
mkdir -p "$root/usr/bin" "$root/usr/share"
ln -s "$prefix/usr/bin/$name" "$root/usr/bin/$name"
mv "$appdir/usr/share/applications" "$root/usr/share/applications"
mv "$appdir/usr/share/icons" "$root/usr/share/icons"
rm -f "$appdir"/*.desktop "$appdir"/*.png "$appdir"/.DirIcon "$appdir"/AppRun 2>/dev/null || true

# 5. Depends: the packages of the libraries the bundle still links to
#    (nothing may be missing).
libs="$work/libs.txt"
: > "$libs"
while IFS= read -r -d '' f; do
    file -b "$f" | grep -q '^ELF' || continue
    out_ldd="$(LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$f" 2>&1 || true)"
    if grep -q 'not found' <<<"$out_ldd"; then
        echo "::error::$f: missing libraries:"; grep 'not found' <<<"$out_ldd"; exit 1
    fi
    awk '/=> \//{print $3}' <<<"$out_ldd" >> "$libs"
done < <(find "$appdir" -type f -print0)
pkgs="$work/pkgs.txt"
: > "$pkgs"
sort -u "$libs" | while read -r l; do
    case "$l" in "$appdir"/*) continue ;; esac
    p="$(dpkg -S "$l" 2>/dev/null || dpkg -S "$(realpath "$l")" 2>/dev/null \
         || dpkg -S "${l/#\/usr\/lib/\/lib}" 2>/dev/null || dpkg -S "${l/#\/lib/\/usr\/lib}" 2>/dev/null || true)"
    if [ -z "$p" ]; then echo "::error::no package provides $l"; exit 1; fi
    echo "${p%%:*}" >> "$pkgs"
done
# (dpkg -S may print "pkg1, pkg2: path")
depends="$(tr ',' '\n' < "$pkgs" | sed 's/^ *//;s/ *$//;s/:.*$//' | grep -v '^$' | sort -u | paste -sd, - | sed 's/,/, /g')"
depends="$depends, bluez, dbus"
echo "Depends: $depends"

# 6. The package.
mkdir -p "$root/DEBIAN"
size="$(du -sk --exclude=DEBIAN "$root" | cut -f1)"
cat > "$root/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: arm64
Maintainer: Artilody <noreply@artilody.app>
Installed-Size: $size
Depends: $depends
Section: misc
Priority: optional
Homepage: https://github.com/shenphenchoedron-hue/Artilody-Multisport-Endurance-Trainer
Description: Indoor cycling, rowing and running training (FTMS trainers, Concept2 PM5, heart rate)
 Artilody Multisport Endurance Trainer is a desktop app for structured indoor
 cycling, rowing and treadmill running. It connects to Bluetooth LE smart
 trainers (FTMS, ERG mode), treadmills, Concept2 PM5 rowing monitors and
 heart-rate sensors, records every session to a local SQLite database and
 analyses it. Built with Qt 6 / QML.
 .
 This Raspberry Pi package is self-contained: Qt and the libraries the app
 needs are included (in /opt/artilody-watts-trainer); it only uses what
 Raspberry Pi OS already has.
EOF
dpkg-deb --root-owner-group --build "$root" "$out"
dpkg-deb --info "$out"
