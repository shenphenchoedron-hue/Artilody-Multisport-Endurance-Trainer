import QtQuick

// Riding together: a small name sign over each other station's rider in the
// 3D view (Route3DView.liveLabels: only when two or more others are on the
// route), with a stripe in the rider's jersey colour. Placed over the view
// (the same size and position).
//
//   LiveNameSigns { anchors.fill: sceneLoader; labels: sceneLoader.item ? sceneLoader.item.liveLabels : [] }
Item {
    id: signs
    property var labels: []

    Repeater {
        model: signs.labels
        delegate: Rectangle {
            required property var modelData
            readonly property real s: modelData.scale
            z: s   // (the nearer on top)
            x: modelData.x - width / 2
            y: modelData.y - height
            width: nameText.implicitWidth + 16 * s + 6
            height: (24 * s) + 4
            radius: 6 * s
            color: Qt.rgba(0, 0, 0, 0.62)
            // (a thin edge in the jersey colour)
            readonly property color tone: modelData.jersey ? modelData.jersey : "#9aa5c8"
            border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.75)
            border.width: 1
            Text {
                id: nameText
                anchors.centerIn: parent
                text: modelData.name
                textFormat: Text.PlainText
                color: "white"
                font.pixelSize: Math.max(10, 15 * parent.s)
                font.weight: Font.Bold
            }
            // (a little pointer down to the rider)
            Rectangle {
                width: 6 * parent.s
                height: width
                rotation: 45
                color: Qt.rgba(0, 0, 0, 0.62)
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height - height / 2
                z: -1
            }
        }
    }
}
