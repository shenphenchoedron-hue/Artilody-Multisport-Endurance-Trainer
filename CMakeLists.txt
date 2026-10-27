cmake_minimum_required(VERSION 3.21)

project(ArtilodyWattTrainer
    VERSION 1.3.1
    DESCRIPTION "Indoor cycling power training (Qt 6 / QML)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(WATT_BUILD_TESTS "Build the unit tests" ON)

# Qt 6.4 is the minimum so the app also builds with the Qt packages shipped
# with Raspberry Pi OS (Bookworm).
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql Bluetooth Multimedia)

qt_standard_project_setup()
if(QT_KNOWN_POLICY_QTP0001)   # Qt >= 6.5: modules live under qrc:/qt/qml/
    qt_policy(SET QTP0001 NEW)
endif()

# Modular libraries – no library below depends on QML/UI code.
add_subdirectory(src/core)    # domain model, SQLite, analysis, training engine
add_subdirectory(src/ble)     # Bluetooth LE: FTMS trainers + HRS heart-rate sensors
add_subdirectory(src/media)   # local music player + playlists
option(WATT_BUILD_APP "Build the QML application" ON)
if(WATT_BUILD_APP)
    add_subdirectory(src/app) # executable + QML user interface
endif()

if(WATT_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# Linux .deb packaging (cpack -G DEB). Only configured when building on Linux;
# macOS (.app/.dmg) and Windows (windeployqt/Inno Setup) are packaged in CI.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND WATT_BUILD_APP)
    include(${PROJECT_SOURCE_DIR}/packaging/linux/CPackDeb.cmake)
endif()
