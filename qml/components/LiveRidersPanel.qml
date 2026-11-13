import QtQuick

// Riding together (LiveRide): the other stations' riders on this route -
// each with its jersey colour (as drawn in 3D), how far ahead (+) / behind
// (−), speed and watts. Hidden when nobody else rides the route.
//
//   LiveRidersPanel { live: page.session.live; showWatts: true }
Rectangle {
    id: panel
    property var live: null
    property bool showWatts: true
    readonly property var riders: live ? live.riders : []

    visible: riders.length > 0
    width: 250
    height: col.implicitHeight + 16
    radius: 12
    color: Qt.rgba(0, 0, 0, 0.58)

    function gapText(m) {
        const a = Math.abs(m)
        const v = a >= 1000 ? (a / 1000).toFixed(1) + " km" : Math.round(a) + " m"
        return a < 5 ? "with you" : (m > 0 ? "+" : "−") + v
    }

    Column {
        id: col
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 4
        Text {
            text: "Riding with"
            color: Qt.rgba(1, 1, 1, 0.7)
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        Repeater {
            model: panel.riders
            delegate: Row {
                required property var modelData
                required property int index
                width: col.width
                spacing: 8
                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: modelData.jersey ? modelData.jersey : "#9aa5c8"   // (its jersey; the figure's own: grey)
                    border.color: Qt.rgba(1, 1, 1, 0.6)
                    border.width: 1
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - 18 - gap.width - stats.width - 16
                    text: modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: "white"
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    id: gap
                    width: 70
                    horizontalAlignment: Text.AlignRight
                    text: panel.gapText(modelData.gapM)
                    color: modelData.gapM > 5 ? "#ff9b8a" : modelData.gapM < -5 ? "#8fe3a5" : "white"
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    id: stats
                    width: 60
                    horizontalAlignment: Text.AlignRight
                    // (standing: waiting at the start, or paused)
                    text: modelData.speedKmh < 0.5 && modelData.watts <= 0 ? "ready"
                          : panel.showWatts && modelData.watts > 0 ? modelData.watts + " W" : modelData.speedKmh.toFixed(1) + " km/h"
                    color: Qt.rgba(1, 1, 1, 0.8)
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
