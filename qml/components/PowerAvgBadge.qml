import QtQuick
import Artilody.Backend

// Small "3 s avg" pill on the live power tiles: shows whether the displayed
// cycling power is the rolling average (Settings: "Smooth power") or the raw
// trainer value. Clicking it toggles the smoothing.
Rectangle {
    id: badge
    property real fontSize: 13
    property bool compact: false     // narrow tiles: "3 s" / "raw"
    readonly property bool on: Sensors.powerSmoothing

    implicitWidth: label.implicitWidth + (compact ? 12 : 16)
    implicitHeight: label.implicitHeight + 6
    radius: height / 2
    color: on ? Qt.rgba(0.13, 0.47, 1.0, area.containsMouse ? 0.32 : 0.2)
              : (area.containsMouse ? Theme.hover : "transparent")
    border.width: 1
    border.color: on ? Qt.rgba(0.13, 0.47, 1.0, 0.55) : Theme.fieldBorder

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.on ? Sensors.powerSmoothingSeconds + (badge.compact ? " s" : " s avg")
                       : (badge.compact ? "raw" : "Instant")
        color: badge.on ? Theme.accentText : Theme.textSecondary
        font.pixelSize: badge.fontSize
        font.weight: Font.DemiBold
    }
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: Sensors.powerSmoothing = !Sensors.powerSmoothing
    }
}
