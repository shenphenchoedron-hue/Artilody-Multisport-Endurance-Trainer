#!/usr/bin/env bash
# Self-contained Raspberry Pi OS package: the app with its Qt (Debian 12's Qt
# 6.4, built for OpenGL ES like the Pi's GPU wants), GStreamer and every other
# library it needs inside /opt/artilody-watts-trainer, so installing it never
# downloads anything.
# Only what every Raspberry Pi OS (64-bit, Bookworm) desktop already has is
# left to the system: the C/C++ runtime, the graphics driver (Mesa: GL / EGL,
# X11 / Wayland client libraries), fonts, D-Bus and BlueZ (bluetoothd). Those
# are the package's Depends (worked out below from the libraries the bundle
# still links to), so apt finds them installed.
#
#   packaging/linux/pi-bundle.sh <build dir> <version> <out .deb>
#
# Run in a Debian 12 (Bookworm) arm64 environment after building with its Qt
# packages (CI: .github/workflows/release.yml, job "pi-deb"; it changes that
# environment's Qt plugin folder). Uses linuxdeploy and its Qt plugin
# (plugins, QML modules, rpaths, Qt's own libraries).
set -euxo pipefail

build="$(realpath "${1:?build dir}")"
version="${2:?version}"
out="$(realpath -m "${3:?output .deb}")"
qmake="$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)"
qtplugins="$("$qmake" -query QT_INSTALL_PLUGINS)"
multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"
src="$(realpath "$(dirname "$0")/../..")"

name=artilody-watts-trainer
prefix=/opt/$name
work="$(mktemp -d)"
root="$work/root"
appdir="$root$prefix"

extra_qml=()

# 1. The app as installed (bin, music, desktop file, icons) under /opt.
DESTDIR="$appdir" cmake --install "$build" --prefix /usr

# 2. Database plugins other than SQLite would drag in client libraries:
#    leave them out.
for p in "$qtplugins"/sqldrivers/*.so; do
    case "$(basename "$p")" in libqsqlite.so) ;; *) rm -f "$p" ;; esac
done

# 3. GStreamer (Qt 6.4 Multimedia plays through it): the plugins music and
#    the Real Life Videos need, and the plugin scanner. The launcher points
#    GStreamer at them (step 5).
gstsrc="/usr/lib/$multiarch/gstreamer-1.0"
gstdst="$appdir/usr/lib/gstreamer-1.0"
mkdir -p "$gstdst" "$appdir/usr/libexec"
for g in coreelements \
         app audioconvert audioresample audiorate playback typefindfunctions videoconvertscale videorate \
         volume alsa ogg vorbis opus theora pbtypes rawparse subparse gio opengl overlaycomposition \
         autodetect pulseaudio isomp4 audioparsers id3demux apetag mpg123 matroska wavparse video4linux2 \
         deinterlace videofilter icydemux flac \
         libav videoparsersbad; do
    if [ -f "$gstsrc/libgst$g.so" ]; then cp "$gstsrc/libgst$g.so" "$gstdst/"; else echo "::warning::GStreamer plugin $g not found"; fi
done
cp "/usr/lib/$multiarch/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner" "$appdir/usr/libexec/"

# Wayland (the Raspberry Pi OS desktop on a Pi 4 / 5): the EGL client
# integration, without which Qt on Wayland cannot create an OpenGL context
# (linuxdeploy-plugin-qt deploys the shell and decoration plugins only).
wgi="$qtplugins/wayland-graphics-integration-client"
test -d "$wgi" || { echo "::error::$wgi missing (qt6-wayland)"; exit 1; }
mkdir -p "$appdir/usr/plugins"
cp -r "$wgi" "$appdir/usr/plugins/"

# QML modules linuxdeploy's scan misses: Qt Quick Dialogs' own file and
# folder dialogs load Qt.labs.folderlistmodel (an "import ... auto" in their
# qmldir); without it opening a file or a folder crashes. QtQuick.Shapes:
# imported by Qt's controls.
qmlsrc="$("$qmake" -query QT_INSTALL_QML)"
for m in Qt/labs/folderlistmodel QtQuick/Shapes; do
    test -d "$qmlsrc/$m" || { echo "::error::QML module $m missing in $qmlsrc"; exit 1; }
    mkdir -p "$appdir/usr/qml/$(dirname "$m")"
    cp -r "$qmlsrc/$m" "$appdir/usr/qml/$m"
    extra_qml+=(--deploy-deps-only "$appdir/usr/qml/$m")
done

# 4. linuxdeploy + Qt plugin (extracted: no FUSE in containers).
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
export QMAKE="$qmake"
export QML_SOURCES_PATHS="$src/qml"
# X11 (xcb, deployed by default), Wayland (the Pi 4/5 desktop) and offscreen
# (CI smoke tests, headless checks)
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so"
export DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1
linuxdeploy --appdir "$appdir" --executable "$appdir/usr/bin/$name" --plugin qt \
    --deploy-deps-only "$gstdst" --deploy-deps-only "$appdir/usr/libexec" \
    --deploy-deps-only "$appdir/usr/plugins/wayland-graphics-integration-client" "${extra_qml[@]}"

# The app uses the Basic style only (main.cpp): the other Qt Quick Controls
# styles stay out.
for style in Material Universal Imagine Fusion; do
    rm -rf "$appdir/usr/qml/QtQuick/Controls/$style"
done

# 5. System-visible pieces: the command (a launcher: GStreamer uses the
#    bundled plugins, its registry lives in the user's cache), the menu entry
#    and icons.
mkdir -p "$root/usr/bin" "$root/usr/share"
cat > "$root/usr/bin/$name" <<EOF
#!/bin/sh
# Artilody Multisport Endurance Trainer (self-contained, $prefix)
d=$prefix/usr
# (the app puts the original back for the programs it starts: main.cpp)
export ARTILODY_BUNDLE=1
export ARTILODY_ORIGINAL_LD_LIBRARY_PATH="\${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="\$d/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export GST_PLUGIN_SYSTEM_PATH_1_0="\$d/lib/gstreamer-1.0"
export GST_PLUGIN_PATH_1_0="\$d/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER_1_0="\$d/libexec/gst-plugin-scanner"
export GST_REGISTRY_1_0="\${XDG_CACHE_HOME:-\$HOME/.cache}/$name/gstreamer-1.0-registry.bin"
exec "\$d/bin/$name" "\$@"
EOF
chmod 755 "$root/usr/bin/$name"
mv "$appdir/usr/share/applications" "$root/usr/share/applications"
mv "$appdir/usr/share/icons" "$root/usr/share/icons"
rm -f "$appdir"/*.desktop "$appdir"/*.png "$appdir"/.DirIcon "$appdir"/AppRun 2>/dev/null || true

# 6. Depends: the packages of the libraries the bundle still links to
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

# 7. The package.
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
 This Raspberry Pi package is self-contained: Qt, GStreamer and the
 libraries the app needs are included (in /opt/artilody-watts-trainer); it
 only uses what Raspberry Pi OS already has.
EOF
dpkg-deb --root-owner-group --build "$root" "$out"
dpkg-deb --info "$out"
