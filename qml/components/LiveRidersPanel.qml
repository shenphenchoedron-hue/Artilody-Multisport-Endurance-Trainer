import QtQuick

// Riding together (LiveRide): the other stations' riders and runners on this
// route - each with its jersey colour (as drawn in 3D), bike or running icon,
// how far ahead (+) / behind (−) and, on a loop, whole laps, speed and watts.
// Hidden when nobody else is on the route.
//
//   LiveRidersPanel { live: page.session.live; showWatts: true }
Rectangle {
    id: panel
    property var live: null
    property bool showWatts: true
    readonly property var riders: live ? live.riders : []

    visible: riders.length > 0
    width: 290
    height: col.implicitHeight + 16
    radius: 12
    color: Qt.rgba(0, 0, 0, 0.58)

    function gapText(m, laps) {
        const a = Math.abs(m)
        const v = a >= 1000 ? (a / 1000).toFixed(1) + " km" : Math.round(a) + " m"
        const near = a < 5 ? "with you" : (m > 0 ? "+" : "−") + v
        // a loop: whole laps ahead / behind first ("+1 lap")
        return laps === 0 || laps === undefined ? near
             : (laps > 0 ? "+" : "−") + Math.abs(laps) + (Math.abs(laps) === 1 ? " lap" : " laps")
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
                Icon {
                    // (rides or runs)
                    name: modelData.runner ? "run" : "bike"
                    size: 16
                    color: Qt.rgba(1, 1, 1, 0.85)
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    width: parent.width - 18 - 24 - gap.width - stats.width - 24
                    text: modelData.name
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: "white"
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    id: gap
                    width: 74
                    horizontalAlignment: Text.AlignRight
                    text: panel.gapText(modelData.gapM, modelData.lapsAhead)
                    readonly property real ahead: modelData.lapsAhead !== 0 ? modelData.lapsAhead : modelData.gapM
                    color: ahead > (modelData.lapsAhead !== 0 ? 0 : 5) ? "#ff9b8a" : ahead < (modelData.lapsAhead !== 0 ? 0 : -5) ? "#8fe3a5" : "white"
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    id: stats
                    width: 66
                    horizontalAlignment: Text.AlignRight
                    // (standing: waiting at the start, or paused)
                    text: modelData.speedKmh < 0.5 && modelData.watts <= 0 ? "ready"
                          : panel.showWatts && modelData.watts > 0 && !modelData.runner ? modelData.watts + " W"
                          : modelData.speedKmh.toFixed(1) + " km/h"
                    color: Qt.rgba(1, 1, 1, 0.8)
                    font.pixelSize: 13
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
