import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Artilody.Backend

ApplicationWindow {
    id: window

    // Design size of the mockups. Smaller windows (e.g. a 7" Raspberry Pi
    // display) scale the whole UI down uniformly so the layout is preserved.
    readonly property real designWidth: 1584
    readonly property real designHeight: 992
    readonly property real uiScale: Math.min(1, Math.min(width / designWidth, height / designHeight))

    width: Math.min(designWidth, Screen.desktopAvailableWidth)
    height: Math.min(designHeight, Screen.desktopAvailableHeight)
    minimumWidth: 800
    minimumHeight: 480
    visible: true
    title: "Artilody Watts Trainer"
    color: Theme.bgTop

    // Page order of the StackLayout below (and of the sidebar entries).
    readonly property var pageNames: ["dashboard", "freeride", "video", "rowing", "workouts", "calendar", "builder", "history", "devices"]

    function navigate(index) {
        sidebar.currentIndex = index
    }

    // Navigation API for every page (component ids of Main.qml are not
    // visible from other files):
    //   ApplicationWindow.window.showPage("rowing")   (import QtQuick.Controls)
    // Names: see pageNames. Returns false for an unknown name.
    function showPage(name) {
        var i = pageNames.indexOf(name)
        if (i < 0) {
            console.warn("showPage: unknown page", name)
            return false
        }
        navigate(i)
        return true
    }

    function pageIndex(name) {
        return pageNames.indexOf(name)
    }

    readonly property string currentPage: pageNames[sidebar.currentIndex] || ""

    // Scaled item that hosts every modal AppDialog (pages parent theirs here so
    // they cover the whole window, sidebar included).
    readonly property Item dialogHost: root

    // True while the keyboard belongs to something else: an AppDialog, a popup /
    // menu (they live in the overlay) or a text field. Global shortcuts such as
    // Space (pause) must not fire then.
    readonly property bool keyboardBusy: {
        var it = window.activeFocusItem
        while (it) {
            if (it === window.overlay)
                return true
            if (it.cursorPosition !== undefined && it.readOnly !== true)   // TextInput / TextEdit
                return true
            if (it.panelWidth !== undefined && it.closable !== undefined && it.visible)   // AppDialog
                return true
            it = it.parent
        }
        return false
    }
    readonly property bool anyDialogOpen: keyboardBusy || profileDialog.visible || musicLibrary.visible
                                          || settingsDialog.visible || workoutDetails.visible || stopDialog.visible
                                          || rowingFinishDialog.visible || ftpResultDialog.visible
                                          || freeRideFinishDialog.visible || gearSetupDialog.visible
                                          || videoFinishDialog.visible

    // Developer aid: start with "--light" to open in the light theme (screenshots).
    Component.onCompleted: if (Qt.application.arguments.indexOf("--light") >= 0) Theme.dark = false

    // Screenshot aid (WATT_SCREENSHOT_MENU, see main.cpp): open one of the menus.
    property var screenshotMenu: null
    function openScreenshotMenu(name) {
        closeScreenshotMenu()
        switch (name) {
        case "calendar": showPage("calendar"); screenshotMenu = calendarPage.openScreenshotMenu(); break
        case "workouts": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("more"); break
        case "new": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("new"); break
        case "workouts-export": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("export"); break
        case "builder": showPage("builder"); screenshotMenu = builderPage.openScreenshotMenu(); break
        case "history": showPage("history"); screenshotMenu = historyPage.openScreenshotMenu(); break
        case "profile": showPage("dashboard"); screenshotMenu = topBar.openScreenshotMenu(); break
        case "music": showPage("dashboard"); musicLibrary.open(); break
        case "gearsetup": showPage("dashboard"); gearSetupDialog.openDialog(); break
        case "gearsetup-advanced": showPage("dashboard"); gearSetupDialog.openDialog(); gearSetupDialog.advancedOpen = true; break
        case "profiledialog": showPage("dashboard"); profileDialog.openFor(false); break
        case "videofinish": showPage("video"); videoFinishDialog.open(); break
        case "settings": showPage("dashboard"); settingsDialog.open(); break
        default: console.warn("openScreenshotMenu: unknown menu", name)
        }
        // show the keyboard highlight (the destructive entry on the calendar menu)
        if (screenshotMenu)
            screenshotMenu.currentIndex = name === "calendar" ? screenshotMenu.count - 1 : 0
    }
    function closeScreenshotMenu() {
        if (musicLibrary.visible)
            musicLibrary.close()
        if (gearSetupDialog.visible)
            gearSetupDialog.close()
        if (profileDialog.visible)
            profileDialog.close()
        if (videoFinishDialog.visible)
            videoFinishDialog.close()
        if (settingsDialog.visible)
            settingsDialog.close()
        if (screenshotMenu)
            screenshotMenu.close()
        screenshotMenu = null
    }

    function showToast(text) {
        toast.show(text)
    }

    // Screenshot aid (WATT_SCREENSHOT_CLICK, see main.cpp): scroll the Devices
    // page to the controllers and the virtual gears card.
    function revealControllers() {
        devicesPage.revealControllers()
    }

    // The Zwift Click routing (VirtualGears::controllerShift) depends on the
    // open page: a ride page sends +/- to its session, elsewhere it shifts.
    Binding {
        target: Gears
        property: "activePage"
        value: window.currentPage
    }
    Connections {
        target: Gears
        function onNotice(text) { window.showToast(text) }
    }

    function editWorkout(id) {
        if (id >= 0)
            Builder.loadWorkout(id)
        showPage("builder")
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.bgTop }
            GradientStop { position: 1; color: Theme.bgBottom }
        }
    }

    Item {
        id: root
        width: window.width / window.uiScale
        height: window.height / window.uiScale
        scale: window.uiScale
        transformOrigin: Item.TopLeft

        Sidebar {
            id: sidebar
            width: 196
            height: parent.height
            profileOpen: profileDialog.visible
            pageNames: window.pageNames
            onNavigate: (index) => sidebar.currentIndex = index
            onProfileRequested: profileDialog.openFor(false)
        }

        Item {
            id: content
            anchors.left: sidebar.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom

            StackLayout {
                id: pages
                anchors.fill: parent
                currentIndex: sidebar.currentIndex

                // keep in the order of window.pageNames
                DashboardPage {
                    onOpenDevices: window.showPage("devices")
                    onEditWorkout: (id) => window.editWorkout(id)
                    onShowDetails: workoutDetails.open()
                    onOpenMusicLibrary: musicLibrary.open()
                    onRequestStop: stopDialog.open()
                    onOpenHistory: window.showPage("history")
                }
                FreeRidePage {
                    onOpenDevices: window.showPage("devices")
                    onRequestFinish: freeRideFinishDialog.open()
                }
                VideoRidePage {
                    onOpenDevices: window.showPage("devices")
                    onRequestFinish: videoFinishDialog.open()
                }
                RowingPage {
                    onOpenDevices: window.showPage("devices")
                    onOpenBuilder: {
                        Builder.newWorkout("rowing")
                        window.showPage("builder")
                    }
                    onRequestFinish: rowingFinishDialog.open()
                    onMessage: (text) => window.showToast(text)
                }
                WorkoutsPage {
                    id: workoutsPage
                    onStarted: window.showPage("dashboard")
                    onEditWorkout: (id) => window.editWorkout(id)
                    onNewWorkout: {
                        Builder.newWorkout()
                        window.showPage("builder")
                    }
                    onMessage: (text) => window.showToast(text)
                }
                CalendarPage { id: calendarPage }
                BuilderPage {
                    id: builderPage
                    onMessage: (text) => window.showToast(text)
                }
                HistoryPage {
                    id: historyPage
                    onMessage: (text) => window.showToast(text)
                }
                DevicesPage {
                    id: devicesPage
                    onOpenGearSetup: gearSetupDialog.openDialog()
                    onOpenProfile: profileDialog.openFor(false)
                }
            }

            TopBar {
                id: topBar
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.rightMargin: 24
                anchors.topMargin: 2
                onSettingsRequested: settingsDialog.open()
                onEditProfileRequested: profileDialog.openFor(false)
                onNewProfileRequested: profileDialog.openFor(true)
            }
        }

        // --- dialogs (inside the scaled root) -------------------------------
        ProfileDialog { id: profileDialog }
        MusicLibraryDialog { id: musicLibrary }
        SettingsDialog { id: settingsDialog }
        WorkoutDetailsDialog { id: workoutDetails }

        ConfirmDialog {
            id: stopDialog
            title: "Finish workout?"
            message: "Your ride will be saved to History. Choose Discard to throw it away."
            acceptText: "Save & Finish"
            secondaryText: "Discard"
            onAccepted: {
                var id = Training.stop()
                window.showToast(id >= 0 ? "Ride saved – see History for the analysis" : "Ride too short to save")
            }
            onSecondary: {
                Training.discard()
                window.showToast("Ride discarded")
            }
        }

        ConfirmDialog {
            id: rowingFinishDialog
            title: "Finish row?"
            message: "Your row will be saved to History. Choose Discard to throw it away."
            acceptText: "Save & Finish"
            secondaryText: "Discard"
            onAccepted: {
                if (!Rowing.active)
                    return
                var id = Rowing.finish(true)
                window.showToast(id >= 0 ? "Row saved – see History for the analysis" : "Row too short to save")
            }
            onSecondary: {
                if (!Rowing.active)
                    return
                Rowing.finish(false)
                window.showToast("Row discarded")
            }
        }

        ConfirmDialog {
            id: freeRideFinishDialog
            title: "Finish free ride?"
            message: "Your ride will be saved to History. Choose Discard to throw it away."
            acceptText: "Save & Finish"
            secondaryText: "Discard"
            onAccepted: {
                if (!FreeRide.active)
                    return
                var id = FreeRide.finish(true)
                window.showToast(id >= 0 ? "Ride saved – see History for the analysis" : "Ride too short to save")
            }
            onSecondary: {
                if (!FreeRide.active)
                    return
                FreeRide.discard()
                window.showToast("Ride discarded")
            }
        }

        ConfirmDialog {
            id: videoFinishDialog
            title: "Finish video ride?"
            message: "Your ride will be saved to History. Choose Discard to throw it away."
            acceptText: "Save & Finish"
            secondaryText: "Discard"
            onAccepted: {
                if (!Rlv.active)
                    return
                var id = Rlv.finish(true)
                window.showToast(id >= 0 ? "Ride saved – see History for the analysis" : "Ride too short to save")
            }
            onSecondary: {
                if (!Rlv.active)
                    return
                Rlv.discard()
                window.showToast("Ride discarded")
            }
        }

        GearSetupDialog { id: gearSetupDialog }

        // Above every other dialog: a ride can finish while one is open.
        FtpResultDialog {
            id: ftpResultDialog
            z: 1001
            onMessage: (text) => window.showToast(text)
        }

        Toast {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 30
        }
    }

    Connections {
        target: Training
        function onFinished() {
            // the workout may complete while the stop dialog is open
            if (stopDialog.visible)
                stopDialog.close()
            if (Training.lastRideId >= 0)
                window.showToast("Workout complete – ride saved to History")
        }
        // Emitted after onFinished, so the stop dialog is already closed.
        function onFtpTestCompleted(rideId, estimatedFtp, previousFtp, best1minPower) {
            ftpResultDialog.openWith(estimatedFtp, previousFtp, best1minPower)
        }
        function onStartRefused() {
            window.showToast(FreeRide.active ? "A free ride is in progress – finish it before starting a workout"
                             : Rlv.active || Rlv.waiting ? "A video ride is in progress – finish it before starting a workout"
                             : "A row is in progress – finish it before riding")
        }
        function onRowingWorkoutRefused(workoutId, name) {
            window.showToast("\"" + name + "\" is a rowing workout – start it on the Rowing page")
        }
    }

    Connections {
        target: Rowing
        function onFinished() {
            if (rowingFinishDialog.visible)
                rowingFinishDialog.close()
        }
    }

    Connections {
        target: FreeRide
        function onFinished() {
            if (freeRideFinishDialog.visible)
                freeRideFinishDialog.close()
        }
        function onStartRefused(reason) {
            window.showToast(reason)
        }
    }

    Connections {
        target: Rlv
        function onFinished() {
            if (videoFinishDialog.visible)
                videoFinishDialog.close()
            // the end of the route finishes and saves the ride by itself
            if (Rlv.completed)
                window.showToast(Rlv.lastRideId >= 0 ? "Route complete – ride saved to History" : "Route complete")
        }
        function onStartRefused(reason) {
            window.showToast(reason)
        }
    }

    // keyboard shortcuts for the trainer room
    Shortcut {
        sequence: "Space"
        enabled: window.currentPage === "dashboard" && !window.anyDialogOpen
        onActivated: Training.togglePause()
    }
    Shortcut {
        sequence: "Space"
        enabled: window.currentPage === "rowing" && Rowing.active && !window.anyDialogOpen
        onActivated: Rowing.togglePause()
    }

    // Free Ride page: shift / ERG target / grade / pause (not while typing or
    // while a dialog or menu has the keyboard).
    readonly property bool freeRideKeys: currentPage === "freeride" && !anyDialogOpen
    // ↑ / ↓: shift harder / easier (Ride with virtual gears), ±5 W (ERG)
    function freeRideStep(direction) {
        if (FreeRide.mode === "erg")
            FreeRide.adjustTargetWatts(5 * direction)
        else if (FreeRide.virtualGears)
            direction > 0 ? Gears.shiftUp() : Gears.shiftDown()
    }
    Shortcut {
        sequences: ["Space"]
        enabled: window.freeRideKeys && (FreeRide.trainerConnected || FreeRide.active)
        onActivated: FreeRide.togglePause()
    }
    Shortcut {
        sequences: ["Up", "+", "="]
        enabled: window.freeRideKeys
        onActivated: window.freeRideStep(1)
    }
    Shortcut {
        sequences: ["Down", "-"]
        enabled: window.freeRideKeys
        onActivated: window.freeRideStep(-1)
    }
    Shortcut {
        sequence: "Shift+Up"
        enabled: window.freeRideKeys && FreeRide.virtualGears
        onActivated: Gears.shiftFrontUp()
    }
    Shortcut {
        sequence: "Shift+Down"
        enabled: window.freeRideKeys && FreeRide.virtualGears
        onActivated: Gears.shiftFrontDown()
    }
    Shortcut {
        sequence: "PgUp"
        enabled: window.freeRideKeys
        onActivated: FreeRide.mode === "erg" ? FreeRide.adjustTargetWatts(10) : FreeRide.adjustGrade(0.5)
    }
    Shortcut {
        sequence: "PgDown"
        enabled: window.freeRideKeys
        onActivated: FreeRide.mode === "erg" ? FreeRide.adjustTargetWatts(-10) : FreeRide.adjustGrade(-0.5)
    }

    // Video Ride page:
    //   Simulation: ↑ / ↓ shift (virtual gears on), Shift + ↑ / ↓ front shift,
    //               PgUp / PgDn difficulty ±10 %
    //   ERG:        ↑ / ↓ target ±5 W, PgUp / PgDn ±10 W
    // Space start / pause / resume (not while typing or while a dialog or
    // menu has the keyboard).
    readonly property bool videoKeys: currentPage === "video" && !anyDialogOpen
    Shortcut {
        sequences: ["Space"]
        enabled: window.videoKeys && (Rlv.active || (Rlv.hasRoute && Rlv.trainerConnected && !Rlv.waiting))
        onActivated: Rlv.togglePause()
    }
    Shortcut {
        sequences: ["Up", "+", "="]
        enabled: window.videoKeys && (Rlv.mode === "erg" || Rlv.virtualGears)
        onActivated: Rlv.mode === "erg" ? Rlv.adjustTargetWatts(5) : Gears.shiftUp()
    }
    Shortcut {
        sequences: ["Down", "-"]
        enabled: window.videoKeys && (Rlv.mode === "erg" || Rlv.virtualGears)
        onActivated: Rlv.mode === "erg" ? Rlv.adjustTargetWatts(-5) : Gears.shiftDown()
    }
    Shortcut {
        sequence: "Shift+Up"
        enabled: window.videoKeys && Rlv.virtualGears
        onActivated: Gears.shiftFrontUp()
    }
    Shortcut {
        sequence: "Shift+Down"
        enabled: window.videoKeys && Rlv.virtualGears
        onActivated: Gears.shiftFrontDown()
    }
    Shortcut {
        sequence: "PgUp"
        enabled: window.videoKeys
        onActivated: Rlv.mode === "erg" ? Rlv.adjustTargetWatts(10) : Rlv.adjustDifficulty(10)
    }
    Shortcut {
        sequence: "PgDown"
        enabled: window.videoKeys
        onActivated: Rlv.mode === "erg" ? Rlv.adjustTargetWatts(-10) : Rlv.adjustDifficulty(-10)
    }
}
