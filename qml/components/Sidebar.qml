import QtQuick
import Artilody.Backend

Rectangle {
    id: sidebar
    property int currentIndex: 0
    property bool profileOpen: false
    // Page names in StackLayout order (Main.qml window.pageNames).
    property var pageNames: ["dashboard", "freeride", "video", "gpx", "rowing", "workouts", "calendar", "builder", "history", "devices"]
    signal navigate(int index)
    signal profileRequested()

    color: Theme.sidebar

    readonly property var items: [
        // internal page name stays "dashboard" (showPage() compatibility)
        { label: "Watt Training", icon: "bike", page: "dashboard" },
        { label: "Free Ride", icon: "freeRide", page: "freeride" },
        { label: "Video Ride", icon: "video", page: "video" },
        { label: "GPX Ride", icon: "mapPin", page: "gpx" },
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
        // Only sessions that were started (running or paused) are shown: a
        // cycling workout wins, then a free ride, a video ride, a row.
        // Nothing starts by pedaling or rowing - only Start / Space.
        readonly property bool freeRide: !Training.active && FreeRide.active
        // a video ride or a GPX ride (the same session class: Rlv / Gpx)
        readonly property bool videoRide: !Training.active && !freeRide && Rlv.active
        readonly property bool gpxRide: !Training.active && !freeRide && !videoRide && Gpx.active
        readonly property bool video: videoRide || gpxRide
        readonly property var ride: gpxRide ? Gpx : Rlv
        readonly property string rideName: gpxRide ? "GPX" : "Video"
        // a workout ridden during the video: "Video 12:34" / "Sweet Spot 2/6 • 230 W"
        readonly property bool videoWorkout: video && ride.hasWorkout && !ride.workoutFinished
                                             && ride.currentIntervalIndex >= 0
        readonly property bool rowing: !Training.active && !freeRide && !video && Rowing.active
        readonly property bool paused: rowing ? Rowing.paused : freeRide ? FreeRide.paused : video ? ride.paused : Training.paused
        readonly property bool moving: rowing ? Rowing.running : freeRide ? FreeRide.running : video ? ride.running : Training.running

        visible: Training.active || freeRide || video || rowing
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
                    color: live.paused ? Theme.warning : Theme.danger
                    SequentialAnimation on opacity {
                        running: live.moving
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 700 }
                        NumberAnimation { to: 1; duration: 700 }
                    }
                }
                Text {
                    text: live.rowing ? (live.paused ? "Paused" : "Rowing")
                          : live.freeRide ? (live.paused ? "Free Ride paused" : "Free Ride")
                          : live.video ? (live.paused ? live.rideName + " paused" : live.rideName)
                                         + (live.videoWorkout ? "  " + Theme.clock(live.ride.elapsedSeconds) : "")
                          : (live.paused ? "Paused" : "Riding")
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
                      ? Theme.clock(Rowing.elapsedSeconds) + "  •  "
                        + (Rowing.heartRate > 0 ? Rowing.heartRate + " BPM" : Rowing.watts + " W")
                      : live.freeRide
                        ? Theme.clock(FreeRide.elapsedSeconds) + " • "
                          + (FreeRide.mode === "erg" ? "ERG • "
                             : FreeRide.virtualGears ? "Gear " + Gears.gearLabel + " • " : "")
                          + Sensors.displayPower + " W"
                        : live.video
                          ? (live.videoWorkout
                             ? live.ride.currentIntervalName + " " + (live.ride.currentIntervalIndex + 1) + "/"
                               + live.ride.workoutIntervals.length + " • " + Sensors.displayPower + " W"
                             : Theme.clock(live.ride.elapsedSeconds) + " • " + (live.ride.riddenMeters / 1000).toFixed(1)
                               + " km • " + Sensors.displayPower + " W")
                        : Theme.clock(Training.elapsedSec) + "  •  " + Sensors.displayPower + " W"
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sidebar.navigate(sidebar.indexOfPage(live.rowing ? "rowing" : live.freeRide ? "freeride" : live.gpxRide ? "gpx" : live.video ? "video" : "dashboard"))
        }
    }
}
