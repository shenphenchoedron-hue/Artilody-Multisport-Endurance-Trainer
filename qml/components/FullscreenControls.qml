import QtQuick

// The full-screen button in a corner of a ride picture, and in full screen
// the bar that appears when the mouse moves (pause / resume, HUD, exit).
// The page owns the state: fullscreen, hudVisible; it gets the signals.
// Double-click to toggle: FullscreenTapLayer, placed under the picture's
// other controls (a handler up here would also see clicks on them).
Item {
    id: fc
    property bool fullscreen: false
    property bool hudVisible: true
    property bool canPause: false
    property bool paused: false
    property real buttonRightMargin: 14   // clear of other corner controls (Video Ride's sound button)
    signal toggleFullscreen()
    signal toggleHud()
    signal togglePause()

    anchors.fill: parent
    // shown on mouse movement in full screen, hidden again after a while
    property bool awake: true
    function wake() {
        awake = true
        sleepTimer.restart()
    }
    Timer {
        id: sleepTimer
        interval: 2500
        onTriggered: fc.awake = false
    }
    onFullscreenChanged: wake()
    HoverHandler {
        id: hover
        onPointChanged: fc.wake()
    }

    // the expand / collapse button (bottom right)
    Rectangle {
        anchors.right: parent.right
        anchors.rightMargin: fc.buttonRightMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 42
        height: 42
        radius: 12
        visible: !fc.fullscreen || fc.awake
        opacity: btnMouse.containsMouse ? 1 : 0.8
        color: Qt.rgba(0, 0, 0, 0.55)
        border.color: Qt.rgba(1, 1, 1, 0.2)
        Icon {
            anchors.centerIn: parent
            name: fc.fullscreen ? "collapse" : "expand"
            size: 22
            color: "white"
        }
        MouseArea {
            id: btnMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: fc.toggleFullscreen()
        }
    }

    // full screen: pause / resume and HUD on / off (top centre: the bottom
    // holds the chips and the treadmill's workout target)
    Row {
        visible: fc.fullscreen && fc.awake
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 14
        spacing: 10
        Repeater {
            model: 2
            Rectangle {
                readonly property bool pauseButton: index === 0
                visible: !pauseButton || fc.canPause
                height: 42
                width: barRow.implicitWidth + 26
                radius: 21
                color: Qt.rgba(0, 0, 0, 0.6)
                border.color: Qt.rgba(1, 1, 1, 0.2)
                Row {
                    id: barRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: pauseButton ? (fc.paused ? "play" : "pause") : "chart"
                        size: 18
                        color: "white"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: pauseButton ? (fc.paused ? "Resume" : "Pause") + "  (Space)"
                                          : (fc.hudVisible ? "Hide numbers" : "Show numbers") + "  (H)"
                        color: "white"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: pauseButton ? fc.togglePause() : fc.toggleHud()
                }
            }
        }
    }
}
