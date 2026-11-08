import QtQuick

// Full-screen ride HUD (Video Ride, GPX Ride, Treadmill Run): the ride's
// numbers in a half-transparent column on the left, a line for the current
// workout step and a mini elevation profile at the bottom.
//   metrics: [{ label, value, unit, color }]   (value as text)
Rectangle {
    id: hud
    property var metrics: []
    property string stepText: ""          // workout step (empty: none)
    property color stepColor: "#ffd23f"
    // mini profile
    property var profile: []
    property real total: 0
    property real minEle: 0
    property real maxEle: 0
    property real position: 0
    property real elevation: 0
    property var segments: []
    property var gravel: []

    width: 236
    height: col.implicitHeight + 24
    radius: 14
    color: Qt.rgba(0, 0, 0, 0.5)
    border.color: Qt.rgba(1, 1, 1, 0.12)

    Column {
        id: col
        x: 16
        y: 12
        width: hud.width - 32
        spacing: 6
        Repeater {
            model: hud.metrics
            Item {
                width: col.width
                height: 44
                Text {
                    y: 2
                    text: modelData.label
                    color: Qt.rgba(1, 1, 1, 0.65)
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    font.capitalization: Font.AllUppercase
                }
                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: 5
                    Text {
                        anchors.baseline: unitText.baseline
                        text: modelData.value
                        color: modelData.color !== undefined ? modelData.color : "white"
                        font.pixelSize: 30
                        font.weight: Font.Bold
                    }
                    Text {
                        id: unitText
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        text: modelData.unit !== undefined ? modelData.unit : ""
                        color: Qt.rgba(1, 1, 1, 0.65)
                        font.pixelSize: 14
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -3
                    width: parent.width
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.08)
                    visible: index < hud.metrics.length - 1
                }
            }
        }
        Text {
            visible: hud.stepText.length > 0
            width: col.width
            topPadding: 4
            text: hud.stepText
            color: hud.stepColor
            font.pixelSize: 15
            font.weight: Font.Bold
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
        Text {
            visible: hud.total > 0
            width: col.width
            topPadding: 4
            text: (hud.position / 1000).toFixed(1) + " / " + (hud.total / 1000).toFixed(1) + " km"
            color: Qt.rgba(1, 1, 1, 0.7)
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
        RouteProfile {
            visible: hud.total > 0 && hud.profile && hud.profile.length > 1
            width: col.width
            height: 70
            showKmLabels: false
            profile: hud.profile
            total: hud.total
            minEle: hud.minEle
            maxEle: hud.maxEle
            position: hud.position
            startDistance: 0
            elevation: hud.elevation
            informations: []
            currentInformation: -1
            interactive: false
            showPositionLabel: false
            segments: hud.segments
            gravel: hud.gravel
        }
    }
}
