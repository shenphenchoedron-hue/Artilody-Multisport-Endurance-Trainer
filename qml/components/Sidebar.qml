import QtQuick
import Artilody.Backend

Rectangle {
    id: sidebar
    property int currentIndex: 0
    property bool profileOpen: false
    signal navigate(int index)
    signal profileRequested()

    color: Theme.sidebar

    readonly property var items: [
        { label: "Dashboard", icon: "dashboard" },
        { label: "Workouts", icon: "calendar" },
        { label: "Builder", icon: "plus" },
        { label: "History", icon: "history" },
        { label: "Devices", icon: "bluetooth" }
    ]

    Column {
        x: 12
        y: 66
        width: parent.width - 24
        spacing: 9

        Repeater {
            model: sidebar.items
            NavItem {
                width: parent.width
                label: modelData.label
                iconName: modelData.icon
                selected: !sidebar.profileOpen && sidebar.currentIndex === index
                onClicked: sidebar.navigate(index)
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

    // Live ride indicator at the bottom of the sidebar
    Rectangle {
        visible: Training.armedOrActive
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
                    color: Training.waiting ? Theme.success : (Training.paused ? Theme.warning : Theme.danger)
                    SequentialAnimation on opacity {
                        running: Training.running || Training.waiting
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.25; duration: 700 }
                        NumberAnimation { to: 1; duration: 700 }
                    }
                }
                Text {
                    text: Training.waiting ? "Waiting…" : (Training.paused ? "Paused" : "Riding")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Training.waiting ? "Start pedaling"
                                       : Theme.clock(Training.elapsedSec) + "  •  " + Training.power + " W"
                color: Theme.textSecondary
                font.pixelSize: 13
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: sidebar.navigate(0)
        }
    }
}
