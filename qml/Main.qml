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

    function navigate(index) {
        sidebar.currentIndex = index
    }

    function showToast(text) {
        toast.show(text)
    }

    function editWorkout(id) {
        if (id >= 0)
            Builder.loadWorkout(id)
        navigate(2)
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

                DashboardPage {
                    onOpenDevices: window.navigate(4)
                    onEditWorkout: (id) => window.editWorkout(id)
                    onShowDetails: workoutDetails.open()
                    onOpenMusicLibrary: musicLibrary.open()
                    onRequestStop: stopDialog.open()
                    onOpenHistory: window.navigate(3)
                }
                WorkoutsPage {
                    onStarted: window.navigate(0)
                    onEditWorkout: (id) => window.editWorkout(id)
                    onNewWorkout: {
                        Builder.newWorkout()
                        window.navigate(2)
                    }
                    onMessage: (text) => window.showToast(text)
                }
                BuilderPage {
                    onMessage: (text) => window.showToast(text)
                }
                HistoryPage {
                    onMessage: (text) => window.showToast(text)
                }
                DevicesPage {}
            }

            TopBar {
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
    }

    // keyboard shortcuts for the trainer room
    Shortcut {
        sequence: "Space"
        enabled: sidebar.currentIndex === 0 && !profileDialog.visible && !ftpResultDialog.visible
        onActivated: Training.togglePause()
    }
}
