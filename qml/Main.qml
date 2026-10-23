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
    readonly property var pageNames: ["dashboard", "workouts", "calendar", "builder", "rowing", "history", "devices"]

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

    // Screenshot aid (WATT_SCREENSHOT_MENU, see main.cpp): open one of the menus.
    property var screenshotMenu: null
    function openScreenshotMenu(name) {
        closeScreenshotMenu()
        switch (name) {
        case "calendar": showPage("calendar"); screenshotMenu = calendarPage.openScreenshotMenu(); break
        case "workouts": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("more"); break
        case "new": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("new"); break
        case "workouts-export": showPage("workouts"); screenshotMenu = workoutsPage.openScreenshotMenu("export"); break
        case "history": showPage("history"); screenshotMenu = historyPage.openScreenshotMenu(); break
        case "profile": showPage("dashboard"); screenshotMenu = topBar.openScreenshotMenu(); break
        default: console.warn("openScreenshotMenu: unknown menu", name)
        }
        // show the keyboard highlight (the destructive entry on the calendar menu)
        if (screenshotMenu)
            screenshotMenu.currentIndex = name === "calendar" ? screenshotMenu.count - 1 : 0
    }
    function closeScreenshotMenu() {
        if (screenshotMenu)
            screenshotMenu.close()
        screenshotMenu = null
    }

    function showToast(text) {
        toast.show(text)
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
                    onMessage: (text) => window.showToast(text)
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
                HistoryPage {
                    id: historyPage
                    onMessage: (text) => window.showToast(text)
                }
                DevicesPage {}
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
            window.showToast("A row is in progress – finish it before riding")
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

    // keyboard shortcuts for the trainer room
    Shortcut {
        sequence: "Space"
        enabled: window.currentPage === "dashboard" && !profileDialog.visible && !ftpResultDialog.visible
        onActivated: Training.togglePause()
    }
    Shortcut {
        sequence: "Space"
        enabled: window.currentPage === "rowing" && Rowing.active && !profileDialog.visible
                 && !rowingFinishDialog.visible
        onActivated: Rowing.togglePause()
    }
}
