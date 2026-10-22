# CPack configuration for the Debian/Ubuntu/Raspberry Pi OS package.
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   cd build && cpack -G DEB
#
# Produces artilody-watts-trainer_<version>_<arch>.deb (DEB-DEFAULT naming).
# Included from the root CMakeLists.txt only when CMAKE_SYSTEM_NAME is Linux.

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "artilody-watts-trainer")
set(CPACK_PACKAGE_VENDOR "Artilody")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Indoor cycling power training with smart trainer (FTMS) and heart-rate support")
set(CPACK_PACKAGE_DESCRIPTION
"Artilody Watts Trainer is a desktop app for structured indoor cycling
workouts. It connects to Bluetooth LE smart trainers (FTMS, ERG mode) and
heart-rate sensors, drives interval workouts by power, records rides to a
local SQLite database, analyses them (NP, TSS, zones) and plays local music
while you ride. Built with Qt 6 / QML.")
set(CPACK_PACKAGE_CONTACT "Artilody <noreply@artilody.app>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/shenphenchoedron-hue/Artilody-Watts-Trainer")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)
if(EXISTS "${PROJECT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
endif()

set(CPACK_DEBIAN_PACKAGE_NAME "artilody-watts-trainer")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Artilody <noreply@artilody.app>")
set(CPACK_DEBIAN_PACKAGE_SECTION "misc")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Library dependencies (libQt6*, libstdc++, ...) are computed with dpkg-shlibdeps.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Runtime pieces dpkg-shlibdeps cannot see: QML modules, plugins, BlueZ.
# Package names verified for Debian 12 (bookworm) and Ubuntu 24.04 (noble).
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qml6-module-qtqml-models, qml6-module-qt-labs-folderlistmodel, qml6-module-qtmultimedia, libqt6sql6-sqlite, qt6-qpa-plugins, bluez")
# GStreamer plugins decode MP3 for the Qt 6.4 multimedia backend on Debian/Ubuntu.
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "gstreamer1.0-plugins-good, gstreamer1.0-plugins-base, qt6-wayland")

include(CPack)
