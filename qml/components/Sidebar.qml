import QtQuick
import Artilody.Backend

Rectangle {
    id: sidebar
    property int currentIndex: 0
    property bool profileOpen: false
    // Page names in StackLayout order (Main.qml window.pageNames).
    property var pageNames: ["dashboard", "freeride", "rowing", "workouts", "calendar", "builder", "history", "devices"]
    signal navigate(int index)
    signal profileRequested()

    color: Theme.sidebar

    readonly property var items: [
        { label: "Dashboard", icon: "dashboard", page: "dashboard" },
        { label: "Free Ride", icon: "freeRide", page: "freeride" },
        { label: "Rowing", icon: "rowing", page: "rowing" },
        { label: "Workouts", icon: "calendar", page: "workouts" },
        { label: "Calendar", icon: "calendarDays", page: "calendar" },
        { label: "Builder", icon: "plus", page: "builder" },
        { label: "History", icon: "history", page: "history" },
        { label: "Devices", icon: "bluetooth", page: "devices" }
    ]

    function indexOfPage(name) {
        return Math.max(0, pageNames.indexOf(name))
    }

    Column {
        x: 12
        y: 66
        width: parent.width - 24
        spacing: 9

        Repeater {
            model: sidebar.items
            NavItem {
                readonly property int pageIndex: sidebar.indexOfPage(modelData.page)
                width: parent.width
                label: modelData.label
                iconName: modelData.icon
                selected: !sidebar.profileOpen && sidebar.currentIndex === pageIndex
                onClicked: sidebar.navigate(pageIndex)
            }
        }
        NavItem {
            width: parent.width
            label: "Profile"
            iconName: "profile"
            selected: sidebar.profileOpen
            onClicked: sidebar.profileRequested()
        }
    }

    // Live ride / free ride / row indicator at the bottom of the sidebar
    Rectangle {
        id: live
        // A cycling workout wins, then a free ride (running or waiting for the
        // first pedal stroke); otherwise a running row, or a set-up row that a
        // stroke on the connected rower would start, is shown.
        readonly property bool freeRide: !Training.armedOrActive && (FreeRide.active || FreeRide.waiting)
        readonly property bool rowing: !Training.armedOrActive && !freeRide
                                       && (Rowing.active || (Rowing.state === "ready" && Rowing.rowerConnected))
        readonly property bool waiting: rowing ? Rowing.state === "ready" : freeRide ? FreeRide.waiting : Training.waiting
        readonly property bool paused: rowing ? Rowing.paused : freeRide ? FreeRide.paused : Training.paused
        readonly property bool moving: rowing ? Rowing.running : freeRide ? FreeRide.running : Training.running

        visible: Training.armedOrActive || freeRide || rowing
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        x: 12
        width: parent.width - 24
        height: 64
        radius: 10
        color: Theme.hover
        Column {
            anchors.centerIn: parent
            width: parent.width - 16
            spacing: 2
            Row {
                spacing: 8
                anchors.horizontalCenter: parent.horizontalCenter
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: live.waiting ? Theme.success : (live.paused ? Theme.warning : Theme.danger)
                    SequentialAnimation on opacity {
                        running: live.moving || live.waiting
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 700 }
                        NumberAnimation { to: 1; duration: 700 }
                    }
                }
                Text {
                    text: live.rowing ? (live.waiting ? "Rowing ready" : (live.paused ? "Paused" : "Rowing"))
                          : live.freeRide ? (live.waiting ? "Free Ride ready" : (live.paused ? "Free Ride paused" : "Free Ride"))
                          : (live.waiting ? "Waiting…" : (live.paused ? "Paused" : "Riding"))
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: live.rowing
                      ? (live.waiting ? "Take the first stroke"
                                      : Theme.clock(Rowing.elapsedSeconds) + "  •  "
                                        + (Rowing.heartRate > 0 ? Rowing.heartRate + " BPM" : Rowing.watts + " W"))
                      : live.freeRide
                        ? (live.waiting ? "Start pedaling"
                                        : Theme.clock(FreeRide.elapsedSeconds) + " • "
                                          + (FreeRide.mode === "gears" ? FreeRide.gearLabel + " • "
                                             : FreeRide.mode === "erg" ? "ERG • " : "")
                                          + Sensors.displayPower + " W")
                        : (Training.waiting ? "Start pedaling"
                                            : Theme.clock(Training.elapsedSec) + "  •  " + Sensors.displayPower + " W")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sidebar.navigate(sidebar.indexOfPage(live.rowing ? "rowing" : live.freeRide ? "freeride" : "dashboard"))
        }
    }
}
