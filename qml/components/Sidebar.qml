import QtQuick
import Artilody.Backend

Rectangle {
    id: sidebar
    property int currentIndex: 0
    property bool profileOpen: false
    // Page names in StackLayout order (Main.qml window.pageNames).
    property var pageNames: ["dashboard", "workouts", "calendar", "builder", "rowing", "history", "devices"]
    signal navigate(int index)
    signal profileRequested()

    color: Theme.sidebar

    readonly property var items: [
        { label: "Dashboard", icon: "dashboard", page: "dashboard" },
        { label: "Workouts", icon: "calendar", page: "workouts" },
        { label: "Calendar", icon: "calendarDays", page: "calendar" },
        { label: "Builder", icon: "plus", page: "builder" },
        { label: "Rowing", icon: "rowing", page: "rowing" },
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

    // Live ride / row indicator at the bottom of the sidebar
    Rectangle {
        id: live
        // A cycling ride wins; otherwise a running row, or a set-up row that a
        // stroke on the connected rower would start, is shown.
        readonly property bool rowing: !Training.armedOrActive
                                       && (Rowing.active || (Rowing.state === "ready" && Rowing.rowerConnected))
        readonly property bool waiting: rowing ? Rowing.state === "ready" : Training.waiting
        readonly property bool paused: rowing ? Rowing.paused : Training.paused
        readonly property bool moving: rowing ? Rowing.running : Training.running

        visible: Training.armedOrActive || rowing
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        x: 12
        width: parent.width - 24
        height: 64
        radius: 10
        color: Theme.hover
        Column {
            anchors.centerIn: parent
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
                                      : (live.waiting ? "Waiting…" : (live.paused ? "Paused" : "Riding"))
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: live.rowing
                      ? (live.waiting ? "Take the first stroke"
                                      : Theme.clock(Rowing.elapsedSeconds) + "  •  "
                                        + (Rowing.heartRate > 0 ? Rowing.heartRate + " BPM" : Rowing.watts + " W"))
                      : (Training.waiting ? "Start pedaling"
                                          : Theme.clock(Training.elapsedSec) + "  •  " + Training.power + " W")
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sidebar.navigate(sidebar.indexOfPage(live.rowing ? "rowing" : "dashboard"))
        }
    }
}
