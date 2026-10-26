set(QML_ROOT ${PROJECT_SOURCE_DIR}/qml)

set(APP_QML_FILES
    ${QML_ROOT}/Main.qml
    ${QML_ROOT}/Theme.qml
    # reusable components
    ${QML_ROOT}/components/Icon.qml
    ${QML_ROOT}/components/Card.qml
    ${QML_ROOT}/components/NavItem.qml
    ${QML_ROOT}/components/Sidebar.qml
    ${QML_ROOT}/components/TopBar.qml
    ${QML_ROOT}/components/PageHeader.qml
    ${QML_ROOT}/components/AppButton.qml
    ${QML_ROOT}/components/IconButton.qml
    ${QML_ROOT}/components/ToggleSwitch.qml
    ${QML_ROOT}/components/AppTextField.qml
    ${QML_ROOT}/components/AppComboBox.qml
    ${QML_ROOT}/components/AppMenu.qml
    ${QML_ROOT}/components/AppMenuItem.qml
    ${QML_ROOT}/components/AppMenuSeparator.qml
    ${QML_ROOT}/components/AppSlider.qml
    ${QML_ROOT}/components/StepperField.qml
    ${QML_ROOT}/components/UnitSpinBox.qml
    ${QML_ROOT}/components/CategoryBadge.qml
    ${QML_ROOT}/components/Avatar.qml
    ${QML_ROOT}/components/MetricCard.qml
    ${QML_ROOT}/components/PowerAvgBadge.qml
    ${QML_ROOT}/components/StatTile.qml
    ${QML_ROOT}/components/ZoneLegend.qml
    ${QML_ROOT}/components/WorkoutChart.qml
    ${QML_ROOT}/components/BarChart.qml
    ${QML_ROOT}/components/StatusDot.qml
    ${QML_ROOT}/components/DevicesSummaryCard.qml
    ${QML_ROOT}/components/MusicCard.qml
    ${QML_ROOT}/components/AppDialog.qml
    ${QML_ROOT}/components/Toast.qml
    ${QML_ROOT}/components/SportBadge.qml
    ${QML_ROOT}/components/DatePickerDialog.qml
    ${QML_ROOT}/components/SegmentedControl.qml
    ${QML_ROOT}/components/BlockPreview.qml
    # rowing
    ${QML_ROOT}/components/RowingBoat.qml
    ${QML_ROOT}/components/RowingMetric.qml
    ${QML_ROOT}/components/RowingChart.qml
    ${QML_ROOT}/components/RowerTrack.qml
    ${QML_ROOT}/components/GhostRaceTrack.qml
    ${QML_ROOT}/components/RowingIntervalChart.qml
    ${QML_ROOT}/components/ModeOption.qml
    # free ride
    ${QML_ROOT}/components/GearDisplay.qml
    ${QML_ROOT}/components/ShiftButton.qml
    ${QML_ROOT}/components/ErgControl.qml
    ${QML_ROOT}/components/FreeRideChart.qml
    # real life video
    ${QML_ROOT}/components/RouteProfile.qml
    # pages
    ${QML_ROOT}/pages/DashboardPage.qml
    ${QML_ROOT}/pages/FreeRidePage.qml
    ${QML_ROOT}/pages/VideoRidePage.qml
    ${QML_ROOT}/pages/WorkoutsPage.qml
    ${QML_ROOT}/pages/CalendarPage.qml
    ${QML_ROOT}/pages/BuilderPage.qml
    ${QML_ROOT}/pages/RowingPage.qml
    ${QML_ROOT}/pages/HistoryPage.qml
    ${QML_ROOT}/pages/DevicesPage.qml
    # dialogs
    ${QML_ROOT}/dialogs/ProfileDialog.qml
    ${QML_ROOT}/dialogs/MusicLibraryDialog.qml
    ${QML_ROOT}/dialogs/SettingsDialog.qml
    ${QML_ROOT}/dialogs/WorkoutDetailsDialog.qml
    ${QML_ROOT}/dialogs/ConfirmDialog.qml
    ${QML_ROOT}/dialogs/FtpResultDialog.qml
    ${QML_ROOT}/dialogs/RampDialog.qml
    ${QML_ROOT}/dialogs/IntervalSetDialog.qml
    ${QML_ROOT}/dialogs/GearSetupDialog.qml
)

# All QML files live flat in the module root inside the resource system so they
# can use each other without extra imports, while the source tree stays tidy.
foreach(qml_file ${APP_QML_FILES})
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()
set_source_files_properties(${QML_ROOT}/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(APP_ICON_DIR ${PROJECT_SOURCE_DIR}/assets/icons)

qt_add_executable(ArtilodyWattTrainer
    main.cpp
    CoverImageProvider.h CoverImageProvider.cpp
)

# --- application icon ------------------------------------------------------
if(APPLE)
    set(APP_ICON_MACOS ${APP_ICON_DIR}/AppIcon.icns)
    target_sources(ArtilodyWattTrainer PRIVATE ${APP_ICON_MACOS})
    set_source_files_properties(${APP_ICON_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
if(WIN32)
    target_sources(ArtilodyWattTrainer PRIVATE app-icon.rc)
endif()
# Runtime window icon (title bar / taskbar on Linux and Windows).
qt_add_resources(ArtilodyWattTrainer "appicon"
    PREFIX "/icons"
    BASE ${APP_ICON_DIR}
    FILES ${APP_ICON_DIR}/app-icon-256.png
)

qt_add_qml_module(ArtilodyWattTrainer
    URI Artilody
    VERSION 1.0
    QML_FILES ${APP_QML_FILES}
)

target_compile_definitions(ArtilodyWattTrainer PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(ArtilodyWattTrainer PRIVATE
    watt_core watt_ble watt_rowing watt_media
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)

set_target_properties(ArtilodyWattTrainer PROPERTIES
    OUTPUT_NAME "Artilody Watts Trainer"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER app.artilody.watttrainer
    MACOSX_BUNDLE_BUNDLE_NAME "Artilody Watts Trainer"
    MACOSX_BUNDLE_ICON_FILE AppIcon.icns
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    WIN32_EXECUTABLE TRUE
)
# Linux: a conventional lower-case binary name without spaces (/usr/bin/artilody-watts-trainer).
# macOS (bundle) and Windows keep the user-facing name above.
if(UNIX AND NOT APPLE)
    set_target_properties(ArtilodyWattTrainer PROPERTIES OUTPUT_NAME "artilody-watts-trainer")
endif()

# Ship the sample music next to the executable (or inside the macOS bundle).
file(GLOB SAMPLE_MUSIC ${PROJECT_SOURCE_DIR}/music/*)
if(SAMPLE_MUSIC)
    if(APPLE)
        target_sources(ArtilodyWattTrainer PRIVATE ${SAMPLE_MUSIC})
        set_source_files_properties(${SAMPLE_MUSIC} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/music)
    else()
        add_custom_command(TARGET ArtilodyWattTrainer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:ArtilodyWattTrainer>/music
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SAMPLE_MUSIC} $<TARGET_FILE_DIR:ArtilodyWattTrainer>/music
            COMMENT "Copying sample music"
            VERBATIM)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS ArtilodyWattTrainer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
    # FHS layout used by the .deb package (see packaging/linux/CPackDeb.cmake).
    # main.cpp looks for <bindir>/../share/artilody-watts-trainer/music.
    if(SAMPLE_MUSIC)
        install(FILES ${SAMPLE_MUSIC}
                DESTINATION ${CMAKE_INSTALL_DATADIR}/artilody-watts-trainer/music)
    endif()
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/artilody-watts-trainer.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${APP_ICON_DIR}/app-icon-256.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME artilody-watts-trainer.png)
    foreach(icon_size 48 64 128 512)
        install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/icons/${icon_size}x${icon_size}/artilody-watts-trainer.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps)
    endforeach()
elseif(SAMPLE_MUSIC AND NOT APPLE)
    install(FILES ${SAMPLE_MUSIC} DESTINATION ${CMAKE_INSTALL_BINDIR}/music)
endif()
